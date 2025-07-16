# Sockunion 地址编码到包中的实用方案

## 快速总结

针对您想要将 `union sockunion *peer_addr` 编码到数据包中的需求，推荐使用 **TLV (Type-Length-Value)** 编码方式：

### 核心编码方式

```c
/* 基本 TLV 格式 */
struct address_tlv {
    uint8_t type;      /* 0x01=IPv4, 0x02=IPv6 */
    uint8_t length;    /* 地址长度: 4 或 16 */
    uint8_t address[]; /* 实际地址数据 */
};
```

### 编码函数（核心实现）

```c
/* 编码 sockunion 地址到缓冲区 */
static int encode_peer_address(uint8_t *buffer, size_t max_size, 
                              const union sockunion *su, size_t *out_len)
{
    if (!buffer || !su || !out_len) return -1;
    
    size_t pos = 0;
    
    switch (su->sa.sa_family) {
    case AF_INET:
        if (max_size < 6) return -1;  /* 需要 6 字节 */
        buffer[pos++] = 0x01;         /* IPv4 类型 */
        buffer[pos++] = 4;            /* 长度 */
        memcpy(buffer + pos, &su->sin.sin_addr.s_addr, 4);
        pos += 4;
        break;
        
    case AF_INET6:
        if (max_size < 18) return -1; /* 需要 18 字节 */
        buffer[pos++] = 0x02;         /* IPv6 类型 */
        buffer[pos++] = 16;           /* 长度 */
        memcpy(buffer + pos, &su->sin6.sin6_addr.s6_addr, 16);
        pos += 16;
        break;
        
    default:
        return -1;
    }
    
    *out_len = pos;
    return 0;
}
```

### 在您的代码中集成（最简单方式）

```c
/* 在 bgp_fsm.c 的循环中，替换这部分： */
if (tmp_peer->connection->status == Established) {
    union sockunion *peer_addr = &tmp_peer->connection->su;
    
    /* 原有的调试代码... */
    char addr_str[SU_ADDRSTRLEN];
    sockunion2str(peer_addr, addr_str, sizeof(addr_str));
    // ...调试信息处理...
    
    /* === 新增：地址编码 === */
    uint8_t addr_buffer[18];  /* 最大 18 字节 (IPv6) */
    size_t addr_len;
    
    if (encode_peer_address(addr_buffer, sizeof(addr_buffer), 
                           peer_addr, &addr_len) == 0) {
        
        /* 方案 A: 创建新的扩展包 */
        size_t total_size = sizeof(data) + addr_len;
        uint8_t *extended_data = malloc(total_size);
        
        if (extended_data) {
            memcpy(extended_data, data, sizeof(data));
            memcpy(extended_data + sizeof(data), addr_buffer, addr_len);
            
            bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE,
                               extended_data, total_size);
            free(extended_data);
        }
    } else {
        /* 编码失败，发送原始包 */
        bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE,
                           data, sizeof(data));
    }
}
```

### 方案 B：在原始数据包中预留空间

如果您可以修改原始 `data` 数组的定义：

```c
/* 原来可能是： */
// uint8_t data[SOME_SIZE];

/* 改为： */
uint8_t data[ORIGINAL_SIZE + 18];  /* 为地址预留最大 18 字节 */

/* 然后在填充数据时： */
size_t base_size = ORIGINAL_SIZE;  /* 原始数据大小 */
size_t addr_len;

/* 填充原始 TVR 数据到 data[0..base_size-1] */
// ...现有的数据填充代码...

/* 在循环中编码地址 */
if (tmp_peer->connection->status == Established) {
    union sockunion *peer_addr = &tmp_peer->connection->su;
    
    if (encode_peer_address(data + base_size, 18, peer_addr, &addr_len) == 0) {
        /* 发送包含地址的完整包 */
        bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE,
                           data, base_size + addr_len);
    } else {
        /* 只发送原始数据 */
        bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE,
                           data, base_size);
    }
}
```

### 解码函数（接收端使用）

```c
/* 从包中解码地址 */
static int decode_peer_address(const uint8_t *buffer, size_t buffer_size,
                              union sockunion *su, size_t *consumed)
{
    if (!buffer || !su || !consumed || buffer_size < 2) return -1;
    
    uint8_t type = buffer[0];
    uint8_t length = buffer[1];
    
    if (2 + length > buffer_size) return -1;
    
    memset(su, 0, sizeof(*su));
    
    switch (type) {
    case 0x01:  /* IPv4 */
        if (length != 4) return -1;
        su->sa.sa_family = AF_INET;
        memcpy(&su->sin.sin_addr.s_addr, buffer + 2, 4);
        *consumed = 6;
        break;
        
    case 0x02:  /* IPv6 */
        if (length != 16) return -1;
        su->sa.sa_family = AF_INET6;
        memcpy(&su->sin6.sin6_addr.s6_addr, buffer + 2, 16);
        *consumed = 18;
        break;
        
    default:
        return -1;
    }
    
    return 0;
}
```

### 使用建议

1. **推荐方案 A**：如果包格式比较固定，动态分配扩展包
2. **推荐方案 B**：如果您可以修改数据结构，预留地址空间更高效
3. **空间需求**：IPv4 需要 6 字节，IPv6 需要 18 字节
4. **兼容性**：TLV 格式天然支持向后兼容，接收端可以选择性解析

### 调试技巧

```c
/* 添加调试函数 */
static void debug_encoded_address(const uint8_t *buffer, size_t len)
{
    if (len < 2) return;
    
    printf("编码地址: 类型=%s, 长度=%u, 总字节=%zu\n",
           (buffer[0] == 0x01) ? "IPv4" : "IPv6",
           buffer[1], len);
}
```

这种方法简单、高效，并且完全兼容 IPv4 和 IPv6。
