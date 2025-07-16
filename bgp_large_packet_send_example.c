#include <zebra.h>
#include "prefix.h"
#include "stream.h"
#include "sockunion.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_packet.h"

/* BGP 大包发送实现示例 */

/* 检查peer是否支持扩展消息 */
static bool peer_supports_extended_messages(struct peer *peer)
{
    return (CHECK_FLAG(peer->cap, PEER_CAP_EXTENDED_MESSAGE_RCV) &&
            CHECK_FLAG(peer->cap, PEER_CAP_EXTENDED_MESSAGE_ADV));
}

/* 获取peer的有效载荷大小 */
static size_t get_peer_max_payload_size(struct peer *peer)
{
    return peer->max_packet_size - BGP_HEADER_SIZE;
}

/* 创建带有自定义数据的BGP包 */
static struct stream *create_bgp_packet_with_data(uint8_t msg_type,
                                                  const void *data,
                                                  size_t data_len,
                                                  size_t total_packet_size)
{
    struct stream *s;
    
    /* 创建流 */
    s = stream_new(total_packet_size);
    if (!s) {
        zlog_err("Failed to create stream for BGP packet");
        return NULL;
    }
    
    /* 设置BGP头部 */
    bgp_packet_set_marker(s, msg_type);
    
    /* 写入数据 */
    if (data && data_len > 0) {
        stream_put(s, data, data_len);
    }
    
    /* 设置包大小 */
    bgp_packet_set_size(s);
    
    return s;
}

/* 单包发送 - 适用于小于最大包大小的数据 */
void bgp_send_single_large_packet(struct peer_connection *connection,
                                  uint8_t msg_type,
                                  const void *data,
                                  size_t data_len)
{
    struct peer *peer = connection->peer;
    struct stream *s;
    size_t total_size;
    
    /* 检查数据大小 */
    total_size = BGP_HEADER_SIZE + data_len;
    if (total_size > peer->max_packet_size) {
        zlog_err("Data too large for single packet: %zu > %d",
                 total_size, peer->max_packet_size);
        return;
    }
    
    /* 创建数据包 */
    s = create_bgp_packet_with_data(msg_type, data, data_len, total_size);
    if (!s) {
        return;
    }
    
    /* 添加到发送队列 */
    bgp_packet_add(connection, peer, s);
    
    /* 触发写事件 */
    bgp_writes_on(connection);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Sent single large packet to %s, size: %zu",
                   peer->host, total_size);
    }
}

/* BGP UPDATE消息结构解析 */
typedef struct {
    uint16_t withdrawn_routes_length;
    const uint8_t *withdrawn_routes;
    uint16_t total_path_attr_length;
    const uint8_t *path_attributes;
    size_t nlri_length;
    const uint8_t *nlri;
} bgp_update_components_t;

/* 解析BGP UPDATE消息结构 */
static int parse_bgp_update_message(const uint8_t *data, size_t data_len,
                                    bgp_update_components_t *components)
{
    const uint8_t *ptr = data;
    size_t remaining = data_len;
    
    if (remaining < 4) { /* 至少需要withdrawn_length + attr_length */
        return -1;
    }
    
    /* 解析withdrawn routes */
    components->withdrawn_routes_length = ntohs(*(uint16_t *)ptr);
    ptr += 2;
    remaining -= 2;
    
    if (remaining < components->withdrawn_routes_length + 2) {
        return -1;
    }
    
    components->withdrawn_routes = ptr;
    ptr += components->withdrawn_routes_length;
    remaining -= components->withdrawn_routes_length;
    
    /* 解析path attributes */
    components->total_path_attr_length = ntohs(*(uint16_t *)ptr);
    ptr += 2;
    remaining -= 2;
    
    if (remaining < components->total_path_attr_length) {
        return -1;
    }
    
    components->path_attributes = ptr;
    ptr += components->total_path_attr_length;
    remaining -= components->total_path_attr_length;
    
    /* 剩余部分是NLRI */
    components->nlri_length = remaining;
    components->nlri = ptr;
    
    return 0;
}

/* 解析NLRI前缀列表，返回前缀边界 */
static size_t find_nlri_split_point(const uint8_t *nlri, size_t nlri_len, 
                                    size_t max_size)
{
    const uint8_t *ptr = nlri;
    size_t consumed = 0;
    
    while (consumed < nlri_len && consumed < max_size) {
        if (consumed >= nlri_len) break;
        
        uint8_t prefix_len = ptr[consumed];
        size_t prefix_bytes = (prefix_len + 7) / 8; /* 向上取整 */
        size_t prefix_entry_size = 1 + prefix_bytes; /* 长度字段 + 前缀 */
        
        if (consumed + prefix_entry_size > max_size) {
            break; /* 这个前缀会超出限制 */
        }
        
        consumed += prefix_entry_size;
    }
    
    return consumed;
}

/* 解析路径属性列表，返回属性边界 */
static size_t find_path_attr_split_point(const uint8_t *attr_data, size_t attr_len,
                                         size_t max_size)
{
    const uint8_t *ptr = attr_data;
    size_t consumed = 0;
    
    while (consumed < attr_len && consumed < max_size) {
        if (consumed + 3 > attr_len) break; /* 至少需要flags + type + length */
        
        uint8_t flags = ptr[consumed];
        uint8_t type = ptr[consumed + 1];
        size_t attr_len_size = (flags & 0x10) ? 2 : 1; /* Extended length flag */
        
        if (consumed + 2 + attr_len_size > attr_len) break;
        
        uint16_t attr_length;
        if (attr_len_size == 1) {
            attr_length = ptr[consumed + 2];
        } else {
            attr_length = ntohs(*(uint16_t *)(ptr + consumed + 2));
        }
        
        size_t total_attr_size = 2 + attr_len_size + attr_length;
        
        if (consumed + total_attr_size > max_size) {
            break; /* 这个属性会超出限制 */
        }
        
        consumed += total_attr_size;
    }
    
    return consumed;
}

/* 创建BGP UPDATE消息 */
static struct stream *create_bgp_update_message(const uint8_t *withdrawn_routes,
                                               uint16_t withdrawn_len,
                                               const uint8_t *path_attrs,
                                               uint16_t path_attr_len,
                                               const uint8_t *nlri,
                                               size_t nlri_len)
{
    size_t total_size = BGP_HEADER_SIZE + 2 + withdrawn_len + 2 + path_attr_len + nlri_len;
    struct stream *s = stream_new(total_size);
    
    if (!s) return NULL;
    
    /* BGP头部 */
    bgp_packet_set_marker(s, BGP_MSG_UPDATE);
    
    /* Withdrawn routes length */
    stream_putw(s, withdrawn_len);
    
    /* Withdrawn routes */
    if (withdrawn_routes && withdrawn_len > 0) {
        stream_put(s, withdrawn_routes, withdrawn_len);
    }
    
    /* Total path attribute length */
    stream_putw(s, path_attr_len);
    
    /* Path attributes */
    if (path_attrs && path_attr_len > 0) {
        stream_put(s, path_attrs, path_attr_len);
    }
    
    /* NLRI */
    if (nlri && nlri_len > 0) {
        stream_put(s, nlri, nlri_len);
    }
    
    /* 设置包大小 */
    bgp_packet_set_size(s);
    
    return s;
}

/* 语义感知的分片发送 - 保持BGP消息语义完整性 */
void bgp_send_fragmented_large_packet(struct peer_connection *connection,
                                      uint8_t msg_type,
                                      const void *data,
                                      size_t data_len)
{
    struct peer *peer = connection->peer;
    
    if (msg_type != BGP_MSG_UPDATE) {
        /* 对于非UPDATE消息，使用简单分片 */
        bgp_send_simple_fragmented_packet(connection, msg_type, data, data_len);
        return;
    }
    
    /* 解析UPDATE消息结构 */
    bgp_update_components_t components;
    if (parse_bgp_update_message((const uint8_t *)data, data_len, &components) < 0) {
        zlog_err("BGP: Failed to parse UPDATE message structure");
        return;
    }
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Semantic fragmentation for %s - withdrawn: %u, "
                   "path_attrs: %u, nlri: %zu",
                   peer->host, components.withdrawn_routes_length,
                   components.total_path_attr_length, components.nlri_length);
    }
    
    /* 如果NLRI较小，尝试发送单个包 */
    if (components.nlri_length <= get_peer_max_payload_size(peer) - 4 - 
        components.withdrawn_routes_length - components.total_path_attr_length) {
        
        struct stream *s = create_bgp_update_message(
            components.withdrawn_routes, components.withdrawn_routes_length,
            components.path_attributes, components.total_path_attr_length,
            components.nlri, components.nlri_length);
        
        if (s) {
            bgp_packet_add(connection, peer, s);
            bgp_writes_on(connection);
            return;
        }
    }
    
    /* 需要分片发送NLRI */
    const uint8_t *nlri_ptr = components.nlri;
    size_t nlri_remaining = components.nlri_length;
    size_t fragment_count = 0;
    
    /* 计算每个分片中NLRI的最大大小 */
    size_t max_nlri_per_fragment = get_peer_max_payload_size(peer) - 4 - 
                                  components.withdrawn_routes_length -
                                  components.total_path_attr_length;
    
    while (nlri_remaining > 0) {
        /* 找到NLRI的合适分割点 */
        size_t nlri_chunk_size = find_nlri_split_point(nlri_ptr, nlri_remaining,
                                                      max_nlri_per_fragment);
        
        if (nlri_chunk_size == 0) {
            zlog_err("BGP: Cannot split NLRI - single prefix too large");
            break;
        }
        
        /* 创建分片UPDATE消息 */
        struct stream *s = create_bgp_update_message(
            (fragment_count == 0) ? components.withdrawn_routes : NULL,
            (fragment_count == 0) ? components.withdrawn_routes_length : 0,
            (fragment_count == 0) ? components.path_attributes : NULL,
            (fragment_count == 0) ? components.total_path_attr_length : 0,
            nlri_ptr, nlri_chunk_size);
        
        if (!s) {
            zlog_err("Failed to create UPDATE fragment %zu", fragment_count);
            break;
        }
        
        /* 添加到发送队列 */
        bgp_packet_add(connection, peer, s);
        
        /* 更新指针和剩余大小 */
        nlri_ptr += nlri_chunk_size;
        nlri_remaining -= nlri_chunk_size;
        fragment_count++;
        
        if (bgp_debug_update(peer, NULL, NULL, 0)) {
            zlog_debug("BGP: Sent UPDATE fragment %zu to %s, NLRI size: %zu",
                       fragment_count, peer->host, nlri_chunk_size);
        }
    }
    
    /* 触发写事件 */
    bgp_writes_on(connection);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Completed semantic fragmentation to %s, "
                   "total fragments: %zu", peer->host, fragment_count);
    }
}

/* 简单分片发送 - 用于非UPDATE消息 */
void bgp_send_simple_fragmented_packet(struct peer_connection *connection,
                                      uint8_t msg_type,
                                      const void *data,
                                      size_t data_len)
{
    struct peer *peer = connection->peer;
    const uint8_t *data_ptr = (const uint8_t *)data;
    size_t remaining = data_len;
    size_t max_payload_size = get_peer_max_payload_size(peer);
    size_t fragment_count = 0;
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Simple fragmentation for %s, total size: %zu, "
                   "max payload: %zu", peer->host, data_len, max_payload_size);
    }
    
    while (remaining > 0) {
        size_t chunk_size = (remaining > max_payload_size) 
                           ? max_payload_size 
                           : remaining;
        struct stream *s;
        
        /* 创建分片 */
        s = create_bgp_packet_with_data(msg_type, data_ptr, chunk_size,
                                       BGP_HEADER_SIZE + chunk_size);
        if (!s) {
            zlog_err("Failed to create fragment %zu", fragment_count);
            break;
        }
        
        /* 添加到发送队列 */
        bgp_packet_add(connection, peer, s);
        
        /* 更新指针和剩余大小 */
        data_ptr += chunk_size;
        remaining -= chunk_size;
        fragment_count++;
        
        if (bgp_debug_update(peer, NULL, NULL, 0)) {
            zlog_debug("BGP: Sent simple fragment %zu to %s, size: %zu",
                       fragment_count, peer->host, chunk_size);
        }
    }
    
    /* 触发写事件 */
    bgp_writes_on(connection);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Completed simple fragmentation to %s, "
                   "total fragments: %zu", peer->host, fragment_count);
    }
}

/* 智能发送 - 根据数据大小自动选择发送方式 */
void bgp_send_large_packet_smart(struct peer_connection *connection,
                                 uint8_t msg_type,
                                 const void *data,
                                 size_t data_len)
{
    struct peer *peer = connection->peer;
    size_t total_size = BGP_HEADER_SIZE + data_len;
    
    if (!data || data_len == 0) {
        zlog_warn("BGP: Attempting to send empty data");
        return;
    }
    
    /* 检查peer连接状态 */
    if (peer->connection->status != Established) {
        zlog_warn("BGP: Peer %s not in Established state, cannot send data",
                  peer->host);
        return;
    }
    
    /* 根据数据大小选择发送方式 */
    if (total_size <= peer->max_packet_size) {
        /* 单包发送 */
        bgp_send_single_large_packet(connection, msg_type, data, data_len);
    } else if (peer_supports_extended_messages(peer)) {
        /* 对于支持扩展消息的peer，尝试更大的包 */
        if (total_size <= BGP_EXTENDED_MESSAGE_MAX_PACKET_SIZE) {
            bgp_send_single_large_packet(connection, msg_type, data, data_len);
        } else {
            /* 即使支持扩展消息也超过了限制，分片发送 */
            bgp_send_fragmented_large_packet(connection, msg_type, data, data_len);
        }
    } else {
        /* 不支持扩展消息，分片发送 */
        bgp_send_fragmented_large_packet(connection, msg_type, data, data_len);
    }
}

/* 带有重组序列号的分片发送 */
typedef struct {
    uint16_t fragment_id;
    uint16_t sequence_num;
    uint16_t total_fragments;
    uint8_t flags;
} bgp_fragment_header_t;

#define BGP_FRAGMENT_FLAG_MORE  0x01
#define BGP_FRAGMENT_FLAG_FIRST 0x02

void bgp_send_sequenced_large_packet(struct peer_connection *connection,
                                     uint8_t msg_type,
                                     const void *data,
                                     size_t data_len)
{
    struct peer *peer = connection->peer;
    const uint8_t *data_ptr = (const uint8_t *)data;
    size_t remaining = data_len;
    size_t max_payload_size = get_peer_max_payload_size(peer) - sizeof(bgp_fragment_header_t);
    size_t total_fragments = (data_len + max_payload_size - 1) / max_payload_size;
    uint16_t fragment_id = (uint16_t)random(); /* 随机生成fragment ID */
    uint16_t sequence_num = 0;
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Sending sequenced large packet to %s, "
                   "total size: %zu, fragments: %zu, fragment_id: %u",
                   peer->host, data_len, total_fragments, fragment_id);
    }
    
    while (remaining > 0) {
        size_t chunk_size = (remaining > max_payload_size) 
                           ? max_payload_size 
                           : remaining;
        struct stream *s;
        bgp_fragment_header_t frag_header;
        size_t packet_size = BGP_HEADER_SIZE + sizeof(frag_header) + chunk_size;
        
        /* 创建分片头 */
        frag_header.fragment_id = htons(fragment_id);
        frag_header.sequence_num = htons(sequence_num);
        frag_header.total_fragments = htons(total_fragments);
        frag_header.flags = 0;
        
        if (sequence_num == 0) {
            frag_header.flags |= BGP_FRAGMENT_FLAG_FIRST;
        }
        if (remaining > chunk_size) {
            frag_header.flags |= BGP_FRAGMENT_FLAG_MORE;
        }
        
        /* 创建数据包 */
        s = stream_new(packet_size);
        if (!s) {
            zlog_err("Failed to create sequenced fragment %u", sequence_num);
            break;
        }
        
        /* 设置BGP头部 */
        bgp_packet_set_marker(s, msg_type);
        
        /* 写入分片头 */
        stream_put(s, &frag_header, sizeof(frag_header));
        
        /* 写入数据 */
        stream_put(s, data_ptr, chunk_size);
        
        /* 设置包大小 */
        bgp_packet_set_size(s);
        
        /* 添加到发送队列 */
        bgp_packet_add(connection, peer, s);
        
        /* 更新指针和剩余大小 */
        data_ptr += chunk_size;
        remaining -= chunk_size;
        sequence_num++;
        
        if (bgp_debug_update(peer, NULL, NULL, 0)) {
            zlog_debug("BGP: Sent sequenced fragment %u/%zu to %s, "
                       "size: %zu, flags: 0x%02x",
                       sequence_num, total_fragments, peer->host, 
                       chunk_size, frag_header.flags);
        }
    }
    
    /* 触发写事件 */
    bgp_writes_on(connection);
}

/* 批量发送多个数据项 */
void bgp_send_batch_data(struct peer_connection *connection,
                         uint8_t msg_type,
                         const void **data_items,
                         const size_t *data_sizes,
                         size_t item_count)
{
    struct peer *peer = connection->peer;
    struct stream *s;
    size_t total_size = BGP_HEADER_SIZE;
    size_t i;
    
    /* 计算总大小 */
    for (i = 0; i < item_count; i++) {
        total_size += data_sizes[i];
    }
    
    if (total_size > peer->max_packet_size) {
        zlog_warn("BGP: Batch data too large: %zu > %d",
                  total_size, peer->max_packet_size);
        return;
    }
    
    /* 创建数据包 */
    s = stream_new(total_size);
    if (!s) {
        zlog_err("Failed to create batch packet");
        return;
    }
    
    /* 设置BGP头部 */
    bgp_packet_set_marker(s, msg_type);
    
    /* 写入所有数据项 */
    for (i = 0; i < item_count; i++) {
        if (data_items[i] && data_sizes[i] > 0) {
            stream_put(s, data_items[i], data_sizes[i]);
        }
    }
    
    /* 设置包大小 */
    bgp_packet_set_size(s);
    
    /* 添加到发送队列 */
    bgp_packet_add(connection, peer, s);
    
    /* 触发写事件 */
    bgp_writes_on(connection);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Sent batch packet to %s, "
                   "items: %zu, total size: %zu",
                   peer->host, item_count, total_size);
    }
}

/* 使用示例 */
void example_send_large_route_table(struct peer_connection *connection)
{
    /* 示例：发送大型路由表数据 */
    char large_data[10000];
    size_t data_len = sizeof(large_data);
    
    /* 填充示例数据 */
    memset(large_data, 0xAA, data_len);
    
    /* 发送数据 */
    bgp_send_large_packet_smart(connection, BGP_MSG_UPDATE, 
                                large_data, data_len);
}

void example_send_multiple_nlri(struct peer_connection *connection)
{
    /* 示例：批量发送多个NLRI */
    const void *nlri_data[] = {
        "nlri1_data_here",
        "nlri2_data_here", 
        "nlri3_data_here"
    };
    const size_t nlri_sizes[] = {
        strlen("nlri1_data_here"),
        strlen("nlri2_data_here"),
        strlen("nlri3_data_here")
    };
    
    bgp_send_batch_data(connection, BGP_MSG_UPDATE,
                        nlri_data, nlri_sizes, 3);
}
