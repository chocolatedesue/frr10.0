#include <zebra.h>
#include "stream.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_packet.h"
#include "bgpd/bgp_debug.h"

/* BGP 大包接收和解析实现示例 */

/* 分片重组数据结构 */
struct bgp_fragment_reassembly {
    uint16_t fragment_id;
    uint16_t total_fragments;
    uint16_t received_fragments;
    uint8_t *data_buffer;
    size_t buffer_size;
    size_t current_size;
    bool *fragment_received;
    time_t first_fragment_time;
    struct bgp_fragment_reassembly *next;
};

/* 全局分片重组列表 */
static struct bgp_fragment_reassembly *fragment_list = NULL;

/* 分片头结构 */
typedef struct {
    uint16_t fragment_id;
    uint16_t sequence_num;
    uint16_t total_fragments;
    uint8_t flags;
} bgp_fragment_header_t;

#define BGP_FRAGMENT_FLAG_MORE  0x01
#define BGP_FRAGMENT_FLAG_FIRST 0x02
#define BGP_FRAGMENT_TIMEOUT    30  /* 30秒超时 */

/* 验证BGP包长度 */
static int bgp_validate_packet_length(struct stream *s, struct peer *peer)
{
    bgp_size_t length;
    
    if (stream_get_endp(s) < BGP_HEADER_SIZE) {
        zlog_err("BGP packet too short from %s: %zu < %d",
                 peer->host, stream_get_endp(s), BGP_HEADER_SIZE);
        return -1;
    }
    
    /* 读取长度字段 */
    stream_set_getp(s, BGP_MARKER_SIZE);
    length = stream_getw(s);
    stream_set_getp(s, 0);
    
    /* 验证长度范围 */
    if (length < BGP_HEADER_SIZE || length > peer->max_packet_size) {
        zlog_err("BGP packet length invalid from %s: %d (max: %d)",
                 peer->host, length, peer->max_packet_size);
        return -1;
    }
    
    /* 检查实际数据长度 */
    if (stream_get_endp(s) < length) {
        zlog_err("BGP packet incomplete from %s: %zu < %d",
                 peer->host, stream_get_endp(s), length);
        return -1;
    }
    
    return 0;
}

/* 创建分片重组结构 */
static struct bgp_fragment_reassembly *create_fragment_reassembly(
    uint16_t fragment_id, uint16_t total_fragments)
{
    struct bgp_fragment_reassembly *reassembly;
    
    reassembly = XCALLOC(MTYPE_BGP_PACKET, sizeof(struct bgp_fragment_reassembly));
    if (!reassembly) {
        return NULL;
    }
    
    reassembly->fragment_id = fragment_id;
    reassembly->total_fragments = total_fragments;
    reassembly->received_fragments = 0;
    reassembly->buffer_size = BGP_EXTENDED_MESSAGE_MAX_PACKET_SIZE;
    reassembly->data_buffer = XCALLOC(MTYPE_BGP_PACKET, reassembly->buffer_size);
    reassembly->fragment_received = XCALLOC(MTYPE_BGP_PACKET, 
                                           total_fragments * sizeof(bool));
    reassembly->first_fragment_time = time(NULL);
    
    if (!reassembly->data_buffer || !reassembly->fragment_received) {
        XFREE(MTYPE_BGP_PACKET, reassembly->data_buffer);
        XFREE(MTYPE_BGP_PACKET, reassembly->fragment_received);
        XFREE(MTYPE_BGP_PACKET, reassembly);
        return NULL;
    }
    
    return reassembly;
}

/* 释放分片重组结构 */
static void free_fragment_reassembly(struct bgp_fragment_reassembly *reassembly)
{
    if (reassembly) {
        XFREE(MTYPE_BGP_PACKET, reassembly->data_buffer);
        XFREE(MTYPE_BGP_PACKET, reassembly->fragment_received);
        XFREE(MTYPE_BGP_PACKET, reassembly);
    }
}

/* 查找分片重组结构 */
static struct bgp_fragment_reassembly *find_fragment_reassembly(uint16_t fragment_id)
{
    struct bgp_fragment_reassembly *current = fragment_list;
    
    while (current) {
        if (current->fragment_id == fragment_id) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

/* 添加分片重组结构到列表 */
static void add_fragment_reassembly(struct bgp_fragment_reassembly *reassembly)
{
    reassembly->next = fragment_list;
    fragment_list = reassembly;
}

/* 从列表中移除分片重组结构 */
static void remove_fragment_reassembly(uint16_t fragment_id)
{
    struct bgp_fragment_reassembly *current = fragment_list;
    struct bgp_fragment_reassembly *prev = NULL;
    
    while (current) {
        if (current->fragment_id == fragment_id) {
            if (prev) {
                prev->next = current->next;
            } else {
                fragment_list = current->next;
            }
            free_fragment_reassembly(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

/* 清理超时的分片重组 */
static void cleanup_expired_fragments(void)
{
    struct bgp_fragment_reassembly *current = fragment_list;
    struct bgp_fragment_reassembly *prev = NULL;
    time_t now = time(NULL);
    
    while (current) {
        if (now - current->first_fragment_time > BGP_FRAGMENT_TIMEOUT) {
            struct bgp_fragment_reassembly *to_remove = current;
            
            if (prev) {
                prev->next = current->next;
            } else {
                fragment_list = current->next;
            }
            
            current = current->next;
            
            zlog_warn("BGP: Fragment reassembly timeout for ID %u",
                      to_remove->fragment_id);
            free_fragment_reassembly(to_remove);
        } else {
            prev = current;
            current = current->next;
        }
    }
}

/* 处理分片数据 */
static int process_fragmented_packet(struct peer *peer, struct stream *s)
{
    bgp_fragment_header_t frag_header;
    struct bgp_fragment_reassembly *reassembly;
    uint16_t fragment_id, sequence_num, total_fragments;
    uint8_t flags;
    size_t data_len;
    uint8_t *data_ptr;
    
    /* 读取分片头 */
    if (stream_get_endp(s) < BGP_HEADER_SIZE + sizeof(frag_header)) {
        zlog_err("BGP: Fragment header incomplete from %s", peer->host);
        return -1;
    }
    
    stream_set_getp(s, BGP_HEADER_SIZE);
    stream_get(&frag_header, s, sizeof(frag_header));
    
    fragment_id = ntohs(frag_header.fragment_id);
    sequence_num = ntohs(frag_header.sequence_num);
    total_fragments = ntohs(frag_header.total_fragments);
    flags = frag_header.flags;
    
    /* 计算数据长度 */
    data_len = stream_get_endp(s) - stream_get_getp(s);
    data_ptr = stream_pnt(s);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Received fragment from %s, ID: %u, seq: %u/%u, "
                   "size: %zu, flags: 0x%02x",
                   peer->host, fragment_id, sequence_num, total_fragments,
                   data_len, flags);
    }
    
    /* 清理过期的分片 */
    cleanup_expired_fragments();
    
    /* 查找或创建重组结构 */
    reassembly = find_fragment_reassembly(fragment_id);
    if (!reassembly) {
        if (!(flags & BGP_FRAGMENT_FLAG_FIRST)) {
            zlog_warn("BGP: Received non-first fragment %u from %s without context",
                      fragment_id, peer->host);
            return -1;
        }
        
        reassembly = create_fragment_reassembly(fragment_id, total_fragments);
        if (!reassembly) {
            zlog_err("BGP: Failed to create fragment reassembly for %s", peer->host);
            return -1;
        }
        
        add_fragment_reassembly(reassembly);
    }
    
    /* 验证分片信息 */
    if (reassembly->total_fragments != total_fragments) {
        zlog_err("BGP: Fragment count mismatch for ID %u from %s: %u != %u",
                 fragment_id, peer->host, total_fragments, reassembly->total_fragments);
        return -1;
    }
    
    if (sequence_num >= total_fragments) {
        zlog_err("BGP: Invalid sequence number %u for fragment ID %u from %s",
                 sequence_num, fragment_id, peer->host);
        return -1;
    }
    
    /* 检查是否已接收此分片 */
    if (reassembly->fragment_received[sequence_num]) {
        zlog_warn("BGP: Duplicate fragment %u/%u for ID %u from %s",
                  sequence_num, total_fragments, fragment_id, peer->host);
        return 0; /* 忽略重复分片 */
    }
    
    /* 检查缓冲区空间 */
    if (reassembly->current_size + data_len > reassembly->buffer_size) {
        zlog_err("BGP: Fragment reassembly buffer overflow for ID %u from %s",
                 fragment_id, peer->host);
        remove_fragment_reassembly(fragment_id);
        return -1;
    }
    
    /* 存储分片数据 */
    memcpy(reassembly->data_buffer + reassembly->current_size, data_ptr, data_len);
    reassembly->current_size += data_len;
    reassembly->fragment_received[sequence_num] = true;
    reassembly->received_fragments++;
    
    /* 检查是否接收完所有分片 */
    if (reassembly->received_fragments == reassembly->total_fragments) {
        /* 重组完成，处理完整数据 */
        struct stream *complete_stream;
        
        if (bgp_debug_update(peer, NULL, NULL, 0)) {
            zlog_debug("BGP: Fragment reassembly complete for ID %u from %s, "
                       "total size: %zu", fragment_id, peer->host, 
                       reassembly->current_size);
        }
        
        /* 创建完整数据流 */
        complete_stream = stream_new(reassembly->current_size);
        if (complete_stream) {
            stream_put(complete_stream, reassembly->data_buffer, 
                      reassembly->current_size);
            stream_set_getp(complete_stream, 0);
            
            /* 处理完整数据 */
            int result = process_complete_large_packet(peer, complete_stream);
            
            stream_free(complete_stream);
            remove_fragment_reassembly(fragment_id);
            
            return result;
        } else {
            zlog_err("BGP: Failed to create complete stream for ID %u from %s",
                     fragment_id, peer->host);
            remove_fragment_reassembly(fragment_id);
            return -1;
        }
    }
    
    return 0; /* 等待更多分片 */
}

/* 处理完整的大包数据 */
int process_complete_large_packet(struct peer *peer, struct stream *s)
{
    uint8_t type;
    size_t data_len;
    
    if (stream_get_endp(s) < 1) {
        zlog_err("BGP: Empty large packet from %s", peer->host);
        return -1;
    }
    
    /* 读取数据类型（如果有的话） */
    type = stream_getc(s);
    data_len = stream_get_endp(s) - stream_get_getp(s);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Processing complete large packet from %s, "
                   "type: 0x%02x, data_len: %zu", peer->host, type, data_len);
    }
    
    /* 根据类型处理数据 */
    switch (type) {
        case 0x01: /* 示例类型：路由更新 */
            return process_large_route_update(peer, s, data_len);
            
        case 0x02: /* 示例类型：链路状态 */
            return process_large_link_state(peer, s, data_len);
            
        default:
            zlog_warn("BGP: Unknown large packet type 0x%02x from %s",
                      type, peer->host);
            return -1;
    }
}

/* 处理大型路由更新 */
int process_large_route_update(struct peer *peer, struct stream *s, size_t data_len)
{
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Processing large route update from %s, size: %zu",
                   peer->host, data_len);
    }
    
    /* 这里实现具体的路由更新处理逻辑 */
    /* 示例：解析NLRI数据 */
    while (stream_get_getp(s) < stream_get_endp(s)) {
        /* 解析每个路由条目 */
        /* ... */
    }
    
    return 0;
}

/* 处理大型链路状态数据 */
int process_large_link_state(struct peer *peer, struct stream *s, size_t data_len)
{
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Processing large link state from %s, size: %zu",
                   peer->host, data_len);
    }
    
    /* 这里实现具体的链路状态处理逻辑 */
    /* ... */
    
    return 0;
}

/* 主要的BGP包接收处理函数 */
int bgp_receive_large_packet(struct peer_connection *connection, 
                             struct peer *peer, struct stream *s)
{
    uint8_t type;
    bgp_size_t length;
    
    /* 验证包长度 */
    if (bgp_validate_packet_length(s, peer) < 0) {
        return -1;
    }
    
    /* 读取BGP头部 */
    stream_set_getp(s, BGP_MARKER_SIZE);
    length = stream_getw(s);
    type = stream_getc(s);
    
    if (bgp_debug_update(peer, NULL, NULL, 0)) {
        zlog_debug("BGP: Received packet from %s, type: 0x%02x, length: %d",
                   peer->host, type, length);
    }
    
    /* 检查是否是分片包 */
    if (length > BGP_HEADER_SIZE + sizeof(bgp_fragment_header_t)) {
        /* 尝试解析分片头 */
        size_t saved_getp = stream_get_getp(s);
        bgp_fragment_header_t frag_header;
        
        if (stream_get_endp(s) >= stream_get_getp(s) + sizeof(frag_header)) {
            stream_get(&frag_header, s, sizeof(frag_header));
            
            /* 简单的分片包识别：检查fragment_id是否合理 */
            if (ntohs(frag_header.total_fragments) > 1 && 
                ntohs(frag_header.total_fragments) < 1000) {
                /* 可能是分片包，恢复位置并处理 */
                stream_set_getp(s, saved_getp);
                return process_fragmented_packet(peer, s);
            }
        }
        
        /* 恢复位置 */
        stream_set_getp(s, saved_getp);
    }
    
    /* 处理常规大包 */
    return process_complete_large_packet(peer, s);
}

/* 获取当前分片重组统计信息 */
void bgp_get_fragment_stats(size_t *active_reassemblies, 
                           size_t *total_memory_used)
{
    struct bgp_fragment_reassembly *current = fragment_list;
    size_t count = 0;
    size_t memory = 0;
    
    while (current) {
        count++;
        memory += current->buffer_size + 
                 current->total_fragments * sizeof(bool) +
                 sizeof(struct bgp_fragment_reassembly);
        current = current->next;
    }
    
    if (active_reassemblies) {
        *active_reassemblies = count;
    }
    if (total_memory_used) {
        *total_memory_used = memory;
    }
}

/* 清理所有分片重组 */
void bgp_cleanup_all_fragments(void)
{
    while (fragment_list) {
        struct bgp_fragment_reassembly *next = fragment_list->next;
        free_fragment_reassembly(fragment_list);
        fragment_list = next;
    }
}
