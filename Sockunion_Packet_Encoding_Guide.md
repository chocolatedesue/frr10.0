# Sockunion 地址编码到包中的方法

## 问题背景
需要将 `union sockunion *peer_addr` 地址编码到手动构造的数据包中，要求兼容 IPv4 和 IPv6。

## 方法一：类型长度值 (TLV) 编码方式 【推荐】

### 设计思路
使用 TLV (Type-Length-Value) 格式，自动适应不同地址类型：

```c
/* 地址编码函数 */
static int encode_sockunion_to_packet(uint8_t *buffer, size_t buffer_size, 
                                     const union sockunion *su, size_t *encoded_len)
{
    size_t offset = 0;
    
    if (!buffer || !su || !encoded_len) {
        return -1;
    }
    
    /* TLV Header: Type(1 byte) + Length(1 byte) + Value(variable) */
    switch (su->sa.sa_family) {
    case AF_INET:
        if (buffer_size < 6) return -1;  /* 1+1+4 */
        
        buffer[offset++] = 0x01;  /* Type: IPv4 */
        buffer[offset++] = 4;     /* Length: 4 bytes */
        memcpy(buffer + offset, &su->sin.sin_addr.s_addr, 4);
        offset += 4;
        break;
        
    case AF_INET6:
        if (buffer_size < 18) return -1;  /* 1+1+16 */
        
        buffer[offset++] = 0x02;  /* Type: IPv6 */
        buffer[offset++] = 16;    /* Length: 16 bytes */
        memcpy(buffer + offset, &su->sin6.sin6_addr.s6_addr, 16);
        offset += 16;
        break;
        
    default:
        return -1;  /* Unsupported family */
    }
    
    *encoded_len = offset;
    return 0;
}

/* 地址解码函数 */
static int decode_sockunion_from_packet(const uint8_t *buffer, size_t buffer_size,
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
        return -1;  /* Buffer overflow */
    }
    
    switch (type) {
    case 0x01:  /* IPv4 */
        if (length != 4) return -1;
        su->sa.sa_family = AF_INET;
        memcpy(&su->sin.sin_addr.s_addr, buffer + offset, 4);
        offset += 4;
        break;
        
    case 0x02:  /* IPv6 */
        if (length != 16) return -1;
        su->sa.sa_family = AF_INET6;
        memcpy(&su->sin6.sin6_addr.s6_addr, buffer + offset, 16);
        offset += 16;
        break;
        
    default:
        return -1;  /* Unknown type */
    }
    
    *decoded_len = offset;
    return 0;
}
```

### 在您的代码中使用
```c
/* 在 bgp_fsm.c 中的应用 */
if (tmp_peer->connection->status == Established) {
    union sockunion *peer_addr = &tmp_peer->connection->su;
    
    /* 计算需要的空间 */
    size_t addr_encoded_len;
    uint8_t addr_buffer[18];  /* 最大 IPv6 需要 18 字节 (2+16) */
    
    /* 编码地址 */
    if (encode_sockunion_to_packet(addr_buffer, sizeof(addr_buffer), 
                                  peer_addr, &addr_encoded_len) == 0) {
        
        /* 创建包含地址的扩展数据包 */
        size_t total_packet_size = sizeof(data) + addr_encoded_len;
        uint8_t *extended_packet = malloc(total_packet_size);
        
        if (extended_packet) {
            /* 复制原始数据 */
            memcpy(extended_packet, data, sizeof(data));
            
            /* 添加编码的地址 */
            memcpy(extended_packet + sizeof(data), addr_buffer, addr_encoded_len);
            
            /* 发送扩展包 */
            bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, 
                               extended_packet, total_packet_size);
            
            free(extended_packet);
        }
    }
}
```

## 方法二：固定长度联合体编码方式

### 设计思路
使用固定 16 字节存储，IPv4 映射到 IPv6 格式：

```c
/* 固定长度地址编码 */
static void encode_sockunion_fixed(uint8_t addr_buffer[16], const union sockunion *su)
{
    memset(addr_buffer, 0, 16);
    
    switch (su->sa.sa_family) {
    case AF_INET:
        /* IPv4-mapped IPv6 format: ::ffff:a.b.c.d */
        addr_buffer[10] = 0xff;
        addr_buffer[11] = 0xff;
        memcpy(addr_buffer + 12, &su->sin.sin_addr.s_addr, 4);
        break;
        
    case AF_INET6:
        /* 直接复制 IPv6 地址 */
        memcpy(addr_buffer, &su->sin6.sin6_addr.s6_addr, 16);
        break;
    }
}

/* 固定长度地址解码 */
static int decode_sockunion_fixed(const uint8_t addr_buffer[16], union sockunion *su)
{
    memset(su, 0, sizeof(union sockunion));
    
    /* 检查是否为 IPv4-mapped IPv6 */
    if (addr_buffer[10] == 0xff && addr_buffer[11] == 0xff &&
        memcmp(addr_buffer, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 10) == 0) {
        /* 这是 IPv4 地址 */
        su->sa.sa_family = AF_INET;
        memcpy(&su->sin.sin_addr.s_addr, addr_buffer + 12, 4);
    } else {
        /* 这是 IPv6 地址 */
        su->sa.sa_family = AF_INET6;
        memcpy(&su->sin6.sin6_addr.s6_addr, addr_buffer, 16);
    }
    
    return 0;
}
```

## 方法三：标记位 + 地址编码方式

### 设计思路
使用 1 字节标记 + 实际地址长度：

```c
/* 标记位编码方式 */
static int encode_sockunion_flagged(uint8_t *buffer, size_t buffer_size,
                                   const union sockunion *su, size_t *encoded_len)
{
    size_t offset = 0;
    
    if (!buffer || !su || !encoded_len) {
        return -1;
    }
    
    switch (su->sa.sa_family) {
    case AF_INET:
        if (buffer_size < 5) return -1;  /* 1+4 */
        
        buffer[offset++] = 0x04;  /* IPv4 flag (4 bytes) */
        memcpy(buffer + offset, &su->sin.sin_addr.s_addr, 4);
        offset += 4;
        break;
        
    case AF_INET6:
        if (buffer_size < 17) return -1;  /* 1+16 */
        
        buffer[offset++] = 0x06;  /* IPv6 flag (6 = IPv6) */
        memcpy(buffer + offset, &su->sin6.sin6_addr.s6_addr, 16);
        offset += 16;
        break;
        
    default:
        return -1;
    }
    
    *encoded_len = offset;
    return 0;
}
```

## 方法四：BGP 风格的 AFI/SAFI 编码

### 设计思路
模仿 BGP 协议的地址族标识符方式：

```c
/* BGP 风格编码 */
static int encode_sockunion_bgp_style(uint8_t *buffer, size_t buffer_size,
                                     const union sockunion *su, size_t *encoded_len)
{
    size_t offset = 0;
    
    if (!buffer || !su || !encoded_len || buffer_size < 4) {
        return -1;
    }
    
    switch (su->sa.sa_family) {
    case AF_INET:
        if (buffer_size < 8) return -1;  /* 2+1+1+4 */
        
        buffer[offset++] = 0x00;  /* AFI = 1 (IPv4) - high byte */
        buffer[offset++] = 0x01;  /* AFI = 1 (IPv4) - low byte */
        buffer[offset++] = 0x01;  /* SAFI = 1 (Unicast) */
        buffer[offset++] = 4;     /* Address length */
        memcpy(buffer + offset, &su->sin.sin_addr.s_addr, 4);
        offset += 4;
        break;
        
    case AF_INET6:
        if (buffer_size < 20) return -1;  /* 2+1+1+16 */
        
        buffer[offset++] = 0x00;  /* AFI = 2 (IPv6) - high byte */
        buffer[offset++] = 0x02;  /* AFI = 2 (IPv6) - low byte */
        buffer[offset++] = 0x01;  /* SAFI = 1 (Unicast) */
        buffer[offset++] = 16;    /* Address length */
        memcpy(buffer + offset, &su->sin6.sin6_addr.s6_addr, 16);
        offset += 16;
        break;
        
    default:
        return -1;
    }
    
    *encoded_len = offset;
    return 0;
}
```

## 推荐使用方案

### 对于您的场景，推荐使用方法一（TLV 编码）：

1. **简单高效**：编码/解码逻辑清晰
2. **自适应长度**：不浪费空间
3. **易于扩展**：未来可以添加新的地址类型
4. **调试友好**：类型字段便于调试

### 在您现有代码中的集成示例：

```c
/* 在您的 bgp_fsm.c 中添加地址编码 */
if (tmp_peer->connection->status == Established) {
    union sockunion *peer_addr = &tmp_peer->connection->su;
    
    /* 编码地址到包中 */
    uint8_t addr_encoded[18];  /* 最大 18 字节 */
    size_t addr_len;
    
    if (encode_sockunion_to_packet(addr_encoded, sizeof(addr_encoded),
                                  peer_addr, &addr_len) == 0) {
        
        /* 创建新的数据包格式 */
        size_t new_packet_size = sizeof(data) + addr_len;
        uint8_t *new_packet = malloc(new_packet_size);
        
        if (new_packet) {
            /* 复制原始数据 */
            memcpy(new_packet, data, sizeof(data));
            
            /* 追加编码的地址 */
            memcpy(new_packet + sizeof(data), addr_encoded, addr_len);
            
            /* 发送包含地址的数据包 */
            bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE,
                               new_packet, new_packet_size);
            
            free(new_packet);
        }
    }
    
    /* ...其他代码... */
}
```

这种方法兼容 IPv4 和 IPv6，编码效率高，并且便于未来扩展。
