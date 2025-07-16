# BGP FSM 发送函数 TLV 编码改进

## 修改概述

已按照 TLV (Type-Length-Value) 思路成功调整了 `bgp_fsm.c` 中的发送函数，将 peer 地址信息编码到数据包中。

## 主要修改内容

### 1. 添加的 TLV 定义和函数

```c
/* TLV 类型定义 */
#define TLV_TYPE_PEER_ADDR_IPV4  0x01
#define TLV_TYPE_PEER_ADDR_IPV6  0x02

/* 核心编码函数 */
static int encode_peer_address_tlv(uint8_t *buffer, size_t max_size, 
                                  const union sockunion *su, size_t *encoded_len);

/* 解码函数（接收端使用） */
static int decode_peer_address_tlv(const uint8_t *buffer, size_t buffer_size,
                                  union sockunion *su, size_t *decoded_len);

/* 辅助函数 */
static size_t get_peer_address_tlv_size(const union sockunion *su);
static bool packet_has_peer_address_tlv(const uint8_t *packet, size_t packet_size, 
                                        size_t base_data_size);
```

### 2. 发送逻辑改进

原来的发送方式：
```c
bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, data, sizeof data);
```

改进后的发送方式：
```c
/* 1. 编码 peer 地址为 TLV 格式 */
uint8_t peer_addr_tlv[18];  /* 最大 18 字节用于 IPv6 */
size_t tlv_len = 0;

if (encode_peer_address_tlv(peer_addr_tlv, sizeof(peer_addr_tlv), 
                           peer_addr, &tlv_len) == 0) {
    /* 2. 创建扩展数据包 */
    size_t total_packet_size = sizeof(data) + tlv_len;
    uint8_t *extended_packet = malloc(total_packet_size);
    
    /* 3. 复制原始数据 + 追加 TLV 地址 */
    memcpy(extended_packet, data, sizeof(data));
    memcpy(extended_packet + sizeof(data), peer_addr_tlv, tlv_len);
    
    /* 4. 发送扩展包 */
    bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, 
                       extended_packet, total_packet_size);
    
    free(extended_packet);
}
```

## TLV 编码格式

### IPv4 地址编码 (6 字节)
```
+------+------+--------+--------+--------+--------+
| Type | Len  |    IPv4 Address (4 bytes)       |
| 0x01 | 0x04 |  a.b.c.d                       |
+------+------+--------+--------+--------+--------+
```

### IPv6 地址编码 (18 字节)
```
+------+------+--------------------------------+
| Type | Len  |    IPv6 Address (16 bytes)     |
| 0x02 | 0x10 |  xxxx:xxxx:xxxx:xxxx:...       |
+------+------+--------------------------------+
```

## 兼容性特性

### 1. 自动适应地址类型
- **IPv4**：自动使用 6 字节编码 (Type=0x01, Len=4)
- **IPv6**：自动使用 18 字节编码 (Type=0x02, Len=16)

### 2. 向后兼容
- 编码失败时自动回退到发送原始数据包
- 接收端可以选择性解析 TLV 扩展信息

### 3. 错误处理
- 内存分配失败：发送原始包
- TLV 编码失败：发送原始包
- 提供详细的调试日志

## 数据包结构

### 修改前
```
[ TVR Link State Data (固定大小) ]
```

### 修改后
```
[ TVR Link State Data (固定大小) ] [ TLV Peer Address (6或18字节) ]
```

## 使用示例

### 发送端（已集成到代码中）
发送函数会自动：
1. 检测 peer 地址类型（IPv4/IPv6）
2. 编码为对应的 TLV 格式
3. 创建扩展数据包并发送

### 接收端使用示例
```c
/* 接收端解析 TLV 编码的 peer 地址 */
void handle_received_packet(const uint8_t *packet, size_t packet_size, 
                           size_t expected_tvr_size) {
    /* 处理原始 TVR 数据 */
    // ... 处理 packet[0..expected_tvr_size-1] ...
    
    /* 检查是否包含 TLV 编码的 peer 地址 */
    if (packet_has_peer_address_tlv(packet, packet_size, expected_tvr_size)) {
        union sockunion peer_addr;
        size_t decoded_len;
        
        if (decode_peer_address_tlv(packet + expected_tvr_size, 
                                   packet_size - expected_tvr_size,
                                   &peer_addr, &decoded_len) == 0) {
            char addr_str[SU_ADDRSTRLEN];
            sockunion2str(&peer_addr, addr_str, sizeof(addr_str));
            printf("收到包含 peer 地址: %s\n", addr_str);
        }
    }
}
```

## 调试信息

修改后的代码包含详细的调试日志：
```
BGP: 发送包含 TLV 编码 peer 地址的扩展包 (TVR数据: X字节, TLV地址: Y字节, 总计: Z字节, 地址类型: IPv4/IPv6)
```

## 性能影响

- **空间开销**：每个 peer 增加 6-18 字节
- **时间开销**：TLV 编码/解码时间可忽略
- **内存使用**：临时分配扩展包内存（用后即释放）

## 扩展性

TLV 格式便于未来扩展：
- 可以添加新的 TLV 类型（如端口号、AS 号等）
- 接收端可以跳过不认识的 TLV 类型
- 保持向后兼容性

这种改进使得数据包既包含原始的 TVR 链路状态信息，又能携带 peer 连接地址信息，实现了 IPv4/IPv6 的完全兼容。
