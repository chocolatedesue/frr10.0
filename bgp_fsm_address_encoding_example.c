/* 
 * 针对 bgp_fsm.c 中 2418-2445 行的地址编码集成示例
 * 在现有数据包中添加 peer 地址信息
 */

#include <zebra.h>
#include "sockunion.h"

/* 地址类型定义 */
#define ADDR_TYPE_IPV4  0x01
#define ADDR_TYPE_IPV6  0x02

/* 函数声明 */
static int encode_sockunion_address(uint8_t *buffer, size_t buffer_size, 
                                   const union sockunion *su, size_t *encoded_len);
static size_t get_encoded_address_size(const union sockunion *su);
static int decode_sockunion_address(const uint8_t *buffer, size_t buffer_size,
                                   union sockunion *su, size_t *decoded_len);

/* 修改后的代码段 - 放在适当的函数内部 */
static void handle_established_peer_with_address_encoding(struct peer *peer, 
                                                         struct peer *tmp_peer,
                                                         uint8_t *data, 
                                                         int fp1,
                                                         char *bgp_router_id_str)
{
if (tmp_peer->connection->status == Established) {
    /* 获取 peer 地址 */
    union sockunion *peer_addr = &tmp_peer->connection->su;
    char addr_str[SU_ADDRSTRLEN];
    sockunion2str(peer_addr, addr_str, sizeof(addr_str));
    
    /* 调试信息 */
    char debug_buf[512];
    char tmp_peer_remote_id_str[INET_ADDRSTRLEN];
    
    inet_ntop(AF_INET, &peer->bgp->router_id.s_addr, bgp_router_id_str,
              sizeof(bgp_router_id_str));
    inet_ntop(AF_INET, &tmp_peer->remote_id.s_addr, tmp_peer_remote_id_str,
              sizeof(tmp_peer_remote_id_str));
    
    snprintf(debug_buf, sizeof(debug_buf), 
             "BGP [%s] established; connected peer: [%s], "
             "directed_ip: %s, local_as: %u, remote_as: %u\n",
             bgp_router_id_str, tmp_peer_remote_id_str, addr_str,
             (unsigned int)peer->bgp->as, (unsigned int)tmp_peer->as);
    
    if (fp1 != -1) {
        write(fp1, debug_buf, strlen(debug_buf));
    }
    
    /* ===== 新增：地址编码和包扩展 ===== */
    
    /* 计算编码地址所需空间 */
    size_t addr_encoded_size = get_encoded_address_size(peer_addr);
    
    if (addr_encoded_size > 0) {
        /* 创建扩展数据包 */
        size_t total_packet_size = sizeof(data) + addr_encoded_size;
        uint8_t *extended_packet = malloc(total_packet_size);
        
        if (extended_packet) {
            /* 复制原始 TVR 数据 */
            memcpy(extended_packet, data, sizeof(data));
            
            /* 编码并添加 peer 地址 */
            size_t encoded_len;
            int encode_result = encode_sockunion_address(
                extended_packet + sizeof(data), 
                addr_encoded_size,
                peer_addr, 
                &encoded_len
            );
            
            if (encode_result == 0) {
                /* 成功编码，发送扩展包 */
                bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, 
                                   extended_packet, total_packet_size);
                
                /* 可选：添加调试信息 */
                if (IS_ZEBRA_DEBUG_EVENT) {
                    zlog_debug("BGP: 发送包含 peer 地址的扩展包 "
                              "(原始: %zu 字节, 地址: %zu 字节, 总计: %zu 字节)",
                              sizeof(data), encoded_len, total_packet_size);
                }
            } else {
                /* 编码失败，发送原始包 */
                bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, 
                                   data, sizeof(data));
                
                if (IS_ZEBRA_DEBUG_EVENT) {
                    zlog_debug("BGP: peer 地址编码失败，发送原始包");
                }
            }
            
            free(extended_packet);
        } else {
            /* 内存分配失败，发送原始包 */
            bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, 
                               data, sizeof(data));
        }
    } else {
        /* 无效地址，发送原始包 */
        bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, 
                           data, sizeof(data));
    }
}

/* ===== 编码函数实现 ===== */

static int encode_sockunion_address(uint8_t *buffer, size_t buffer_size, 
                                   const union sockunion *su, size_t *encoded_len)
{
    size_t offset = 0;
    
    if (!buffer || !su || !encoded_len) {
        return -1;
    }
    
    switch (su->sa.sa_family) {
    case AF_INET:
        if (buffer_size < 6) return -1;
        
        buffer[offset++] = 0x01;  /* IPv4 类型 */
        buffer[offset++] = 4;     /* 地址长度 */
        memcpy(buffer + offset, &su->sin.sin_addr.s_addr, 4);
        offset += 4;
        break;
        
    case AF_INET6:
        if (buffer_size < 18) return -1;
        
        buffer[offset++] = 0x02;  /* IPv6 类型 */
        buffer[offset++] = 16;    /* 地址长度 */
        memcpy(buffer + offset, &su->sin6.sin6_addr.s6_addr, 16);
        offset += 16;
        break;
        
    default:
        return -1;
    }
    
    *encoded_len = offset;
    return 0;
}

static size_t get_encoded_address_size(const union sockunion *su)
{
    if (!su) return 0;
    
    switch (su->sa.sa_family) {
    case AF_INET:  return 6;   /* 1+1+4 */
    case AF_INET6: return 18;  /* 1+1+16 */
    default:       return 0;
    }
}

/* ===== 接收端解码示例 ===== */

/* 在接收端解码地址的函数 */
static int decode_peer_address_from_packet(const uint8_t *packet, size_t packet_size,
                                          size_t tvr_data_size, union sockunion *peer_addr)
{
    /* 检查包是否包含地址信息 */
    if (packet_size <= tvr_data_size) {
        return -1;  /* 没有额外的地址数据 */
    }
    
    /* 地址数据从 TVR 数据之后开始 */
    const uint8_t *addr_data = packet + tvr_data_size;
    size_t addr_data_size = packet_size - tvr_data_size;
    
    /* 解码地址 */
    size_t decoded_len;
    return decode_sockunion_address(addr_data, addr_data_size, peer_addr, &decoded_len);
}

/* 使用示例 */
/*
void handle_received_packet(const uint8_t *packet, size_t packet_size) {
    // 处理原始 TVR 数据...
    
    // 尝试解码 peer 地址
    union sockunion peer_addr;
    if (decode_peer_address_from_packet(packet, packet_size, 
                                       EXPECTED_TVR_DATA_SIZE, &peer_addr) == 0) {
        char addr_str[SU_ADDRSTRLEN];
        sockunion2str(&peer_addr, addr_str, sizeof(addr_str));
        printf("收到包含 peer 地址: %s\n", addr_str);
    }
}
*/
