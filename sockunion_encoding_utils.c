/* 
 * Sockunion 地址编码实用函数
 * 用于将 union sockunion 地址编码到数据包中
 */

#include <zebra.h>
#include "sockunion.h"

/* 地址编码类型定义 */
#define ADDR_TYPE_IPV4  0x01
#define ADDR_TYPE_IPV6  0x02

/* 将 sockunion 地址编码到缓冲区 */
static int encode_sockunion_address(uint8_t *buffer, size_t buffer_size, 
                                   const union sockunion *su, size_t *encoded_len)
{
    size_t offset = 0;
    
    if (!buffer || !su || !encoded_len) {
        return -1;
    }
    
    switch (su->sa.sa_family) {
    case AF_INET:
        /* IPv4: Type(1) + Length(1) + Address(4) = 6 bytes */
        if (buffer_size < 6) {
            return -1;
        }
        
        buffer[offset++] = ADDR_TYPE_IPV4;  /* 类型标识 */
        buffer[offset++] = 4;               /* 地址长度 */
        memcpy(buffer + offset, &su->sin.sin_addr.s_addr, 4);
        offset += 4;
        break;
        
    case AF_INET6:
        /* IPv6: Type(1) + Length(1) + Address(16) = 18 bytes */
        if (buffer_size < 18) {
            return -1;
        }
        
        buffer[offset++] = ADDR_TYPE_IPV6;  /* 类型标识 */
        buffer[offset++] = 16;              /* 地址长度 */
        memcpy(buffer + offset, &su->sin6.sin6_addr.s6_addr, 16);
        offset += 16;
        break;
        
    default:
        return -1;  /* 不支持的地址族 */
    }
    
    *encoded_len = offset;
    return 0;
}

/* 从缓冲区解码 sockunion 地址 */
static int decode_sockunion_address(const uint8_t *buffer, size_t buffer_size,
                                   union sockunion *su, size_t *decoded_len)
{
    size_t offset = 0;
    
    if (!buffer || !su || !decoded_len || buffer_size < 2) {
        return -1;
    }
    
    memset(su, 0, sizeof(union sockunion));
    
    uint8_t type = buffer[offset++];
    uint8_t length = buffer[offset++];
    
    if (offset + length > buffer_size) {
        return -1;  /* 缓冲区溢出 */
    }
    
    switch (type) {
    case ADDR_TYPE_IPV4:
        if (length != 4) {
            return -1;
        }
        su->sa.sa_family = AF_INET;
        memcpy(&su->sin.sin_addr.s_addr, buffer + offset, 4);
        offset += 4;
        break;
        
    case ADDR_TYPE_IPV6:
        if (length != 16) {
            return -1;
        }
        su->sa.sa_family = AF_INET6;
        memcpy(&su->sin6.sin6_addr.s6_addr, buffer + offset, 16);
        offset += 16;
        break;
        
    default:
        return -1;  /* 未知类型 */
    }
    
    *decoded_len = offset;
    return 0;
}

/* 获取编码后地址所需的缓冲区大小 */
static size_t get_encoded_address_size(const union sockunion *su)
{
    if (!su) {
        return 0;
    }
    
    switch (su->sa.sa_family) {
    case AF_INET:
        return 6;   /* 1 + 1 + 4 */
    case AF_INET6:
        return 18;  /* 1 + 1 + 16 */
    default:
        return 0;
    }
}

/* 调试用：打印编码后的地址信息 */
static void debug_encoded_address(const uint8_t *buffer, size_t length)
{
    if (!buffer || length < 2) {
        return;
    }
    
    uint8_t type = buffer[0];
    uint8_t addr_len = buffer[1];
    
    printf("编码地址信息:\n");
    printf("  类型: %s\n", (type == ADDR_TYPE_IPV4) ? "IPv4" : "IPv6");
    printf("  长度: %u 字节\n", addr_len);
    printf("  总长度: %zu 字节\n", length);
    
    printf("  原始数据: ");
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
}
