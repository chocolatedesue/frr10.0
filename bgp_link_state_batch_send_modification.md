# BGP Link State 消息分批发送修改说明

## 修改目标
将原来一次性发送所有link_state消息的逻辑改为分批发送，每批最多发送60个消息。

## 修改涉及的文件
1. **bgp_packet.c**: BGP消息接收处理时的转发逻辑
2. **bgp_fsm.c**: BGP连接建立时的link_state数据发送逻辑

## 修改1: bgp_packet.c - 消息转发处理

### 修改前的逻辑
```c
// 原始代码一次性发送所有消息
uint8_t forward_data[8 + link_nlri_count * 39];
write_uint64_be(forward_data, link_nlri_count);

// 读取所有NLRI数据
for (uint64_t i = 0; i < link_nlri_count; i++) {
    // 处理每个NLRI...
}

// 一次性发送
bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, forward_data, sizeof(forward_data));
```

### 修改后的逻辑
```c
// 分批发送link_state消息，每批最多60个
const uint64_t MAX_BATCH_SIZE = 60;
uint64_t remaining_count = link_nlri_count;
uint64_t current_batch_start = 0;

while (remaining_count > 0) {
    // 计算当前批次大小
    uint64_t current_batch_size = (remaining_count > MAX_BATCH_SIZE) ? MAX_BATCH_SIZE : remaining_count;
    
    // 创建当前批次的数据包
    uint8_t forward_data[8 + current_batch_size * 39];
    write_uint64_be(forward_data, current_batch_size);
    
    // 重新定位到当前批次的起始位置
    stream_set_getp(s, data_start_pos + 8 + current_batch_start * 39);
    
    // 复制当前批次的NLRI数据
    for (uint64_t i = 0; i < current_batch_size; i++) {
        // 处理每个NLRI...
    }
    
    // 发送当前批次
    bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, forward_data, sizeof(forward_data));
    
    // 更新计数器
    current_batch_start += current_batch_size;
    remaining_count -= current_batch_size;
}
```

## 修改2: bgp_fsm.c - 连接建立时的数据发送

### 修改前的逻辑
```c
// 一次性发送所有link_nlri数据
uint8_t data[link_nlri_len * 39 + 8];
write_uint64_be(data, link_nlri_len);

// 构建所有NLRI数据...

// 一次性发送
bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, data, sizeof data);
```

### 修改后的逻辑
```c
// 分批发送link_state消息，每批最多60个
const uint64_t MAX_BATCH_SIZE = 60;
uint64_t remaining_count = link_nlri_len;
uint64_t current_batch_start = 0;

while (remaining_count > 0) {
    // 计算当前批次的大小
    uint64_t current_batch_size = (remaining_count > MAX_BATCH_SIZE) ? MAX_BATCH_SIZE : remaining_count;
    
    // 创建当前批次的数据包
    uint8_t batch_data[8 + current_batch_size * 39];
    write_uint64_be(batch_data, current_batch_size);
    
    // 复制当前批次的数据
    memcpy(batch_data + 8, data + 8 + current_batch_start * 39, current_batch_size * 39);
    
    // 发送当前批次
    bgp_link_state_send(tmp_peer->connection, BGP_MSG_LINK_STATE, batch_data, sizeof(batch_data));
    
    // 更新计数器
    current_batch_start += current_batch_size;
    remaining_count -= current_batch_size;
}
```

## 数据包格式说明

### NLRI数据结构（39字节）
```
偏移量  长度  字段说明
0       4     local_node (本地节点ID)
4       4     remote_node (远程节点ID)
8       1     tlv_type (TLV类型)
9       1     tlv_length (TLV长度)
10      16    ipv6_addr (IPv6地址)
26      8     seq_num (序列号)
34      1     spf_status (SPF状态)
35      4     ifindex (接口索引)
```

### 数据包头部（8字节）
```
偏移量  长度  字段说明
0       8     当前批次的NLRI数量
```

## 性能考虑

### 1. 内存使用
- **修改前**: 一次性分配 `8 + link_nlri_count * 39` 字节
- **修改后**: 每批次最多分配 `8 + 60 * 39 = 2348` 字节

### 2. 网络传输
- **修改前**: 一个大包可能超过MTU，需要IP层分片
- **修改后**: 每个包大小可控，减少网络层分片

### 3. 处理延迟
- **修改前**: 需要等待所有数据准备完成才能发送
- **修改后**: 可以逐批发送，减少接收端的等待时间

## 发送流程示例

假设有150个link_state消息：

```
批次1: 发送消息 1-60   (60个)
批次2: 发送消息 61-120 (60个)  
批次3: 发送消息 121-150 (30个)
```

每个批次都会调用一次 `bgp_link_state_send()` 函数。

## 优势

1. **内存效率**: 减少单次内存分配大小
2. **网络友好**: 避免超大包的传输问题
3. **处理平滑**: 接收端可以逐批处理，不会因为大包阻塞
4. **可配置性**: `MAX_BATCH_SIZE` 可以根据需要调整
5. **调试便利**: 每个批次都有独立的日志记录

## 注意事项

1. **流位置管理**: 确保每个批次开始时正确设置stream位置
2. **数据完整性**: 每个批次的数据包头部必须正确写入当前批次的数量
3. **错误处理**: 如果某个批次发送失败，需要考虑重发机制（当前实现中未包含）
4. **性能调优**: 可以根据实际网络情况调整 `MAX_BATCH_SIZE` 的值

## 两种场景的区别

### 场景1: BGP消息接收转发 (bgp_packet.c)
- **触发时机**: 当BGP节点接收到link_state消息时
- **数据来源**: 从stream中读取接收到的NLRI数据
- **处理方式**: 需要重新定位stream指针来读取不同批次的数据
- **用途**: 将接收到的消息转发给其他连接的peer

### 场景2: BGP连接建立 (bgp_fsm.c)
- **触发时机**: 当BGP连接刚建立时
- **数据来源**: 从本地数据库中读取所有link_nlri数据
- **处理方式**: 数据已经在内存中，直接使用memcpy复制
- **用途**: 向新建立的连接发送完整的拓扑状态

## 关键技术差异

### 数据访问方式
```c
// 场景1: 从stream读取 (需要重新定位)
stream_set_getp(s, data_start_pos + 8 + current_batch_start * 39);
for (uint64_t i = 0; i < current_batch_size; i++) {
    uint32_t local_node = stream_getl(s);
    uint32_t remote_node = stream_getl(s);
    // ... 逐个读取字段
}

// 场景2: 从内存复制 (直接复制)
memcpy(batch_data + 8, data + 8 + current_batch_start * 39, current_batch_size * 39);
```

### 性能考量
- **场景1**: 需要重新解析数据，CPU开销稍高
- **场景2**: 直接内存复制，性能更好

## 统一的调试输出格式

### 场景1日志格式
```
[本地路由器ID] send batch to [目标peer], batch_size: X, remaining: Y
```

### 场景2日志格式  
```
[本地路由器ID] send establish batch to [目标peer], batch_size: X, remaining: Y
```

通过日志前缀区分是"转发批次"还是"建立连接批次"。

## 总结

这两个修改虽然实现相同的分批发送功能，但针对不同的使用场景：
- **bgp_packet.c**: 处理动态的消息转发
- **bgp_fsm.c**: 处理连接建立时的状态同步

两者都遵循相同的批次大小限制（60个消息），确保了网络传输的一致性和可靠性。
