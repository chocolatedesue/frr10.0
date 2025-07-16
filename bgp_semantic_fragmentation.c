#include <zebra.h>
#include "stream.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"

/* BGP消息语义感知分包实现 */

/* BGP UPDATE消息组件结构 */
typedef struct {
    uint16_t withdrawn_routes_length;
    const uint8_t *withdrawn_routes;
    uint16_t total_path_attr_length;
    const uint8_t *path_attributes;
    size_t nlri_length;
    const uint8_t *nlri;
} bgp_update_components_t;

/* BGP NLRI前缀结构 */
typedef struct {
    uint8_t prefix_len;
    uint8_t prefix_bytes;
    const uint8_t *prefix_data;
} bgp_nlri_prefix_t;

/* 解析单个NLRI前缀 */
static int parse_nlri_prefix(const uint8_t *data, size_t data_len,
                            bgp_nlri_prefix_t *prefix)
{
    if (data_len < 1) return -1;
    
    prefix->prefix_len = data[0];
    prefix->prefix_bytes = (prefix->prefix_len + 7) / 8;
    
    if (1 + prefix->prefix_bytes > data_len) return -1;
    
    prefix->prefix_data = data + 1;
    
    return 1 + prefix->prefix_bytes; /* 返回消耗的字节数 */
}

/* 解析NLRI列表 */
static int parse_nlri_list(const uint8_t *nlri_data, size_t nlri_len,
                          bgp_nlri_prefix_t *prefixes, size_t max_prefixes,
                          size_t *prefix_count)
{
    const uint8_t *ptr = nlri_data;
    size_t remaining = nlri_len;
    size_t count = 0;
    
    while (remaining > 0 && count < max_prefixes) {
        int consumed = parse_nlri_prefix(ptr, remaining, &prefixes[count]);
        if (consumed < 0) break;
        
        ptr += consumed;
        remaining -= consumed;
        count++;
    }
    
    *prefix_count = count;
    return (remaining == 0) ? 0 : -1;
}

/* BGP路径属性结构 */
typedef struct {
    uint8_t flags;
    uint8_t type;
    uint16_t length;
    const uint8_t *data;
} bgp_path_attr_t;

/* 解析单个路径属性 */
static int parse_path_attribute(const uint8_t *data, size_t data_len,
                               bgp_path_attr_t *attr)
{
    if (data_len < 3) return -1;
    
    attr->flags = data[0];
    attr->type = data[1];
    
    size_t header_len = 2;
    
    if (attr->flags & BGP_ATTR_FLAG_EXTLEN) {
        if (data_len < 4) return -1;
        attr->length = ntohs(*(uint16_t *)(data + 2));
        header_len = 4;
    } else {
        attr->length = data[2];
        header_len = 3;
    }
    
    if (header_len + attr->length > data_len) return -1;
    
    attr->data = data + header_len;
    
    return header_len + attr->length;
}

/* 智能分割NLRI - 保持前缀完整性 */
static size_t smart_split_nlri(const uint8_t *nlri_data, size_t nlri_len,
                              size_t max_size, size_t *prefix_count)
{
    const uint8_t *ptr = nlri_data;
    size_t consumed = 0;
    size_t count = 0;
    
    while (consumed < nlri_len && consumed < max_size) {
        if (consumed >= nlri_len) break;
        
        uint8_t prefix_len = ptr[consumed];
        size_t prefix_bytes = (prefix_len + 7) / 8;
        size_t prefix_entry_size = 1 + prefix_bytes;
        
        if (consumed + prefix_entry_size > max_size) {
            break; /* 这个前缀会超出限制 */
        }
        
        consumed += prefix_entry_size;
        count++;
    }
    
    if (prefix_count) *prefix_count = count;
    return consumed;
}

/* 智能分割路径属性 - 保持属性完整性 */
static size_t smart_split_path_attrs(const uint8_t *attr_data, size_t attr_len,
                                    size_t max_size, size_t *attr_count)
{
    const uint8_t *ptr = attr_data;
    size_t consumed = 0;
    size_t count = 0;
    
    while (consumed < attr_len && consumed < max_size) {
        if (consumed + 3 > attr_len) break;
        
        uint8_t flags = ptr[consumed];
        size_t attr_header_len = (flags & BGP_ATTR_FLAG_EXTLEN) ? 4 : 3;
        
        if (consumed + attr_header_len > attr_len) break;
        
        uint16_t attr_length;
        if (flags & BGP_ATTR_FLAG_EXTLEN) {
            attr_length = ntohs(*(uint16_t *)(ptr + consumed + 2));
        } else {
            attr_length = ptr[consumed + 2];
        }
        
        size_t total_attr_size = attr_header_len + attr_length;
        
        if (consumed + total_attr_size > max_size) {
            break; /* 这个属性会超出限制 */
        }
        
        consumed += total_attr_size;
        count++;
    }
    
    if (attr_count) *attr_count = count;
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
    size_t total_size = BGP_HEADER_SIZE + 4 + withdrawn_len + path_attr_len + nlri_len;
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

/* 检查必需的路径属性 */
static bool has_mandatory_path_attrs(const uint8_t *attr_data, size_t attr_len)
{
    bool has_origin = false;
    bool has_as_path = false;
    bool has_next_hop = false;
    
    const uint8_t *ptr = attr_data;
    size_t consumed = 0;
    
    while (consumed < attr_len) {
        if (consumed + 3 > attr_len) break;
        
        uint8_t flags = ptr[consumed];
        uint8_t type = ptr[consumed + 1];
        size_t attr_header_len = (flags & BGP_ATTR_FLAG_EXTLEN) ? 4 : 3;
        
        if (consumed + attr_header_len > attr_len) break;
        
        uint16_t attr_length;
        if (flags & BGP_ATTR_FLAG_EXTLEN) {
            attr_length = ntohs(*(uint16_t *)(ptr + consumed + 2));
        } else {
            attr_length = ptr[consumed + 2];
        }
        
        switch (type) {
            case BGP_ATTR_ORIGIN:
                has_origin = true;
                break;
            case BGP_ATTR_AS_PATH:
                has_as_path = true;
                break;
            case BGP_ATTR_NEXT_HOP:
                has_next_hop = true;
                break;
        }
        
        consumed += attr_header_len + attr_length;
    }
    
    return has_origin && has_as_path && has_next_hop;
}

/* 语义感知的BGP UPDATE分包 */
void bgp_send_semantic_update_fragments(struct peer_connection *connection,
                                       const uint8_t *update_data,
                                       size_t update_len)
{
    struct peer *peer = connection->peer;
    
    if (update_len < 4) {
        zlog_err("BGP: Invalid UPDATE message length: %zu", update_len);
        return;
    }
    
    /* 解析UPDATE消息结构 */
    const uint8_t *ptr = update_data;
    uint16_t withdrawn_len = ntohs(*(uint16_t *)ptr);
    ptr += 2;
    
    const uint8_t *withdrawn_routes = ptr;
    ptr += withdrawn_len;
    
    uint16_t path_attr_len = ntohs(*(uint16_t *)ptr);
    ptr += 2;
    
    const uint8_t *path_attrs = ptr;
    ptr += path_attr_len;
    
    size_t nlri_len = update_len - 4 - withdrawn_len - path_attr_len;
    const uint8_t *nlri_data = ptr;
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Semantic UPDATE fragmentation for %s - "
                   "withdrawn: %u, attrs: %u, nlri: %zu",
                   peer->host, withdrawn_len, path_attr_len, nlri_len);
    }
    
    /* 计算可用的有效载荷空间 */
    size_t max_payload = peer->max_packet_size - BGP_HEADER_SIZE - 4;
    
    /* 如果整个消息能放入一个包 */
    if (update_len <= max_payload) {
        struct stream *s = create_bgp_update_message(
            withdrawn_routes, withdrawn_len,
            path_attrs, path_attr_len,
            nlri_data, nlri_len);
        
        if (s) {
            bgp_packet_add(connection, peer, s);
            bgp_writes_on(connection);
            return;
        }
    }
    
    /* 需要分包处理 */
    
    /* 策略1: 如果只有NLRI太大，分割NLRI */
    if (4 + withdrawn_len + path_attr_len <= max_payload) {
        const uint8_t *nlri_ptr = nlri_data;
        size_t nlri_remaining = nlri_len;
        size_t fragment_count = 0;
        
        size_t max_nlri_per_fragment = max_payload - 4 - withdrawn_len - path_attr_len;
        
        while (nlri_remaining > 0) {
            size_t nlri_chunk_size = smart_split_nlri(nlri_ptr, nlri_remaining,
                                                     max_nlri_per_fragment, NULL);
            
            if (nlri_chunk_size == 0) {
                zlog_err("BGP: Cannot split NLRI - single prefix too large");
                break;
            }
            
            /* 第一个分片包含withdrawn routes和path attributes */
            struct stream *s = create_bgp_update_message(
                (fragment_count == 0) ? withdrawn_routes : NULL,
                (fragment_count == 0) ? withdrawn_len : 0,
                (fragment_count == 0) ? path_attrs : NULL,
                (fragment_count == 0) ? path_attr_len : 0,
                nlri_ptr, nlri_chunk_size);
            
            if (!s) {
                zlog_err("Failed to create UPDATE fragment %zu", fragment_count);
                break;
            }
            
            bgp_packet_add(connection, peer, s);
            
            nlri_ptr += nlri_chunk_size;
            nlri_remaining -= nlri_chunk_size;
            fragment_count++;
            
            if (bgp_debug_update(peer, NULL, NULL, 0)) {
                zlog_debug("BGP: Sent UPDATE fragment %zu to %s, NLRI size: %zu",
                           fragment_count, peer->host, nlri_chunk_size);
            }
        }
        
        bgp_writes_on(connection);
        return;
    }
    
    /* 策略2: 如果路径属性太大，分别发送 */
    if (path_attr_len > max_payload - 4) {
        /* 先发送withdrawn routes */
        if (withdrawn_len > 0) {
            struct stream *s = create_bgp_update_message(
                withdrawn_routes, withdrawn_len, NULL, 0, NULL, 0);
            if (s) {
                bgp_packet_add(connection, peer, s);
            }
        }
        
        /* 分段发送路径属性和NLRI */
        /* 这里需要更复杂的逻辑来处理MP_REACH_NLRI等复杂属性 */
        
        /* 简化处理：发送错误通知 */
        zlog_err("BGP: Path attributes too large for fragmentation");
        return;
    }
    
    /* 策略3: 复杂情况 - 需要更精细的分割 */
    zlog_warn("BGP: Complex UPDATE fragmentation not implemented");
}

/* 高级NLRI分包 - 支持不同地址族 */
void bgp_send_advanced_nlri_fragments(struct peer_connection *connection,
                                     afi_t afi, safi_t safi,
                                     const uint8_t *nlri_data, size_t nlri_len,
                                     struct attr *attr)
{
    struct peer *peer = connection->peer;
    
    /* 根据地址族处理不同的NLRI格式 */
    switch (afi) {
        case AFI_IP:
            if (safi == SAFI_UNICAST || safi == SAFI_MULTICAST) {
                /* IPv4 unicast/multicast NLRI */
                bgp_send_ipv4_nlri_fragments(connection, nlri_data, nlri_len, attr);
            } else if (safi == SAFI_MPLS_VPN) {
                /* VPNv4 NLRI */
                bgp_send_vpnv4_nlri_fragments(connection, nlri_data, nlri_len, attr);
            }
            break;
            
        case AFI_IP6:
            if (safi == SAFI_UNICAST) {
                /* IPv6 unicast NLRI */
                bgp_send_ipv6_nlri_fragments(connection, nlri_data, nlri_len, attr);
            }
            break;
            
        case AFI_L2VPN:
            if (safi == SAFI_EVPN) {
                /* EVPN NLRI */
                bgp_send_evpn_nlri_fragments(connection, nlri_data, nlri_len, attr);
            }
            break;
            
        default:
            zlog_warn("BGP: Unsupported AFI/SAFI for NLRI fragmentation: %d/%d",
                      afi, safi);
            return;
    }
}

/* IPv4 NLRI分片 */
void bgp_send_ipv4_nlri_fragments(struct peer_connection *connection,
                                 const uint8_t *nlri_data, size_t nlri_len,
                                 struct attr *attr)
{
    struct peer *peer = connection->peer;
    
    /* 序列化路径属性 */
    struct stream *attr_stream = stream_new(4096);
    if (!attr_stream) return;
    
    size_t attr_len = bgp_packet_attribute(NULL, peer, attr_stream, attr, 
                                          NULL, NULL, AFI_IP, SAFI_UNICAST,
                                          NULL, NULL, NULL, 0, 0, 0, NULL);
    
    if (attr_len == 0) {
        stream_free(attr_stream);
        return;
    }
    
    /* 分割NLRI */
    const uint8_t *nlri_ptr = nlri_data;
    size_t nlri_remaining = nlri_len;
    size_t fragment_count = 0;
    
    size_t max_nlri_per_fragment = peer->max_packet_size - BGP_HEADER_SIZE - 4 - attr_len;
    
    while (nlri_remaining > 0) {
        size_t prefix_count;
        size_t nlri_chunk_size = smart_split_nlri(nlri_ptr, nlri_remaining,
                                                 max_nlri_per_fragment, &prefix_count);
        
        if (nlri_chunk_size == 0) {
            zlog_err("BGP: Cannot split IPv4 NLRI");
            break;
        }
        
        /* 创建UPDATE消息 */
        struct stream *s = create_bgp_update_message(
            NULL, 0,
            stream_pnt(attr_stream), attr_len,
            nlri_ptr, nlri_chunk_size);
        
        if (!s) {
            zlog_err("Failed to create IPv4 NLRI fragment %zu", fragment_count);
            break;
        }
        
        bgp_packet_add(connection, peer, s);
        
        nlri_ptr += nlri_chunk_size;
        nlri_remaining -= nlri_chunk_size;
        fragment_count++;
        
        if (bgp_debug_update(peer, NULL, NULL, 0)) {
            zlog_debug("BGP: Sent IPv4 NLRI fragment %zu to %s, "
                       "prefixes: %zu, size: %zu",
                       fragment_count, peer->host, prefix_count, nlri_chunk_size);
        }
    }
    
    stream_free(attr_stream);
    bgp_writes_on(connection);
}

/* 占位符函数 - 实际实现需要根据具体的NLRI格式 */
void bgp_send_vpnv4_nlri_fragments(struct peer_connection *connection,
                                  const uint8_t *nlri_data, size_t nlri_len,
                                  struct attr *attr)
{
    zlog_debug("BGP: VPNv4 NLRI fragmentation not implemented");
}

void bgp_send_ipv6_nlri_fragments(struct peer_connection *connection,
                                 const uint8_t *nlri_data, size_t nlri_len,
                                 struct attr *attr)
{
    zlog_debug("BGP: IPv6 NLRI fragmentation not implemented");
}

void bgp_send_evpn_nlri_fragments(struct peer_connection *connection,
                                 const uint8_t *nlri_data, size_t nlri_len,
                                 struct attr *attr)
{
    zlog_debug("BGP: EVPN NLRI fragmentation not implemented");
}

/* 智能UPDATE发送 - 自动选择最佳分包策略 */
void bgp_send_smart_update(struct peer_connection *connection,
                          struct bgp_dest *dest,
                          struct attr *attr,
                          afi_t afi, safi_t safi)
{
    struct peer *peer = connection->peer;
    
    /* 根据路由表大小和属性复杂度选择策略 */
    size_t estimated_attr_size = bgp_packet_attribute_len(attr);
    size_t estimated_nlri_size = 5; /* 简化估算 */
    
    if (estimated_attr_size + estimated_nlri_size <= 
        peer->max_packet_size - BGP_HEADER_SIZE - 4) {
        
        /* 使用标准UPDATE消息 */
        struct stream *s = bgp_update_packet(peer, dest, attr, afi, safi);
        if (s) {
            bgp_packet_add(connection, peer, s);
            bgp_writes_on(connection);
        }
    } else {
        /* 使用分片发送 */
        zlog_debug("BGP: Using fragmented UPDATE for large message");
        /* 这里调用适当的分片函数 */
    }
}

/* 批量路由更新优化 */
void bgp_send_batched_updates(struct peer_connection *connection,
                             struct bgp_dest **destinations,
                             struct attr **attributes,
                             size_t count,
                             afi_t afi, safi_t safi)
{
    struct peer *peer = connection->peer;
    struct stream *batch_stream = stream_new(peer->max_packet_size);
    
    if (!batch_stream) return;
    
    size_t batched_count = 0;
    size_t current_batch_size = BGP_HEADER_SIZE + 4; /* UPDATE header */
    
    for (size_t i = 0; i < count; i++) {
        size_t route_size = estimate_update_size(destinations[i], attributes[i]);
        
        if (current_batch_size + route_size > peer->max_packet_size) {
            /* 发送当前批次 */
            if (batched_count > 0) {
                bgp_packet_set_size(batch_stream);
                bgp_packet_add(connection, peer, batch_stream);
                
                /* 开始新批次 */
                batch_stream = stream_new(peer->max_packet_size);
                current_batch_size = BGP_HEADER_SIZE + 4;
                batched_count = 0;
            }
        }
        
        /* 添加路由到批次 */
        if (add_route_to_batch(batch_stream, destinations[i], attributes[i], 
                              afi, safi) == 0) {
            current_batch_size += route_size;
            batched_count++;
        }
    }
    
    /* 发送最后一个批次 */
    if (batched_count > 0) {
        bgp_packet_set_size(batch_stream);
        bgp_packet_add(connection, peer, batch_stream);
    } else {
        stream_free(batch_stream);
    }
    
    bgp_writes_on(connection);
}

/* 估算UPDATE消息大小 */
static size_t estimate_update_size(struct bgp_dest *dest, struct attr *attr)
{
    size_t size = 0;
    
    /* 估算属性大小 */
    size += bgp_packet_attribute_len(attr);
    
    /* 估算NLRI大小 */
    const struct prefix *prefix = bgp_dest_get_prefix(dest);
    size += 1 + (prefix->prefixlen + 7) / 8; /* 前缀长度 + 前缀字节 */
    
    return size;
}

/* 添加路由到批次 */
static int add_route_to_batch(struct stream *s, struct bgp_dest *dest,
                             struct attr *attr, afi_t afi, safi_t safi)
{
    /* 简化实现 - 实际需要根据具体的UPDATE格式 */
    return 0;
}
