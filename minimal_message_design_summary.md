# 最小发送自定义消息函数的设计思路

## 核心思想

基于对 `bgp_notify_send_internal` 函数的深入分析，最小发送自定义消息函数的思路可以分解为以下层次：

## 1. 绝对最小版本（5行核心代码）

```c
int send_minimal(struct peer_connection *conn, uint8_t type, uint8_t *data, size_t len) {
    struct stream *s = stream_new(BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE);
    bgp_packet_set_marker(s, type);
    if (data) stream_write(s, data, len);
    bgp_packet_set_size(s);
    stream_fifo_push(conn->obuf, s); bgp_writes_on(conn);
    return 0;
}
```

**核心步骤：**
1. 分配内存流
2. 设置消息标记
3. 写入数据
4. 设置包大小  
5. 入队并触发发送

## 2. 实用最小版本（加入基础安全）

```c
int send_safe_minimal(struct peer_connection *conn, uint8_t type, uint8_t *data, size_t len) {
    // 基础验证
    if (!conn || !conn->peer) return -1;
    
    // 线程安全
    frr_mutex_lock_autounlock(&conn->io_mtx);
    
    // 核心5步骤
    struct stream *s = stream_new(conn->peer->max_packet_size);
    if (!s) return -1;
    
    bgp_packet_set_marker(s, type);
    if (data && len > 0) stream_write(s, data, len);
    bgp_packet_set_size(s);
    stream_fifo_push(conn->obuf, s);
    bgp_writes_on(conn);
    
    return 0;
}
```

## 3. 设计原则

### 3.1 最小化原则
- **只保留绝对必需的功能**
- **减少依赖和复杂性**
- **快速实现和验证**

### 3.2 渐进增强原则
```
最小核心 → 基础安全 → 生产级别 → 完全可配置
   ↓          ↓         ↓           ↓
 5行代码    错误检查    调试支持    选项控制
```

### 3.3 分层设计
```
应用层：    bgp_send_custom_message()
安全层：    参数验证 + 线程安全
核心层：    消息构建 + 发送
传输层：    stream 操作 + socket 写入
```

## 4. 关键技术要点

### 4.1 内存管理
```c
struct stream *s = stream_new(size);  // 分配
// ... 使用 ...
// stream 会在发送后自动释放，无需手动 free
```

### 4.2 线程安全
```c
frr_mutex_lock_autounlock(&connection->io_mtx);  // 自动解锁
```

### 4.3 消息格式
```c
bgp_packet_set_marker(s, msg_type);  // 设置BGP标准头部
stream_write(s, data, len);          // 写入自定义数据
bgp_packet_set_size(s);              // 自动计算并设置长度
```

### 4.4 异步发送
```c
stream_fifo_push(connection->obuf, s);  // 添加到发送队列
bgp_writes_on(connection);              // 触发异步发送
```

## 5. 渐进式实现策略

### 阶段1：概念验证（最小版本）
- 实现核心发送逻辑
- 验证消息格式正确性
- 测试基本通信

### 阶段2：基础加固（安全版本）  
- 添加参数验证
- 加入线程安全保护
- 处理内存分配失败

### 阶段3：生产准备（稳定版本）
- 完善错误处理
- 添加调试支持
- 集成统计功能

### 阶段4：功能完善（可配置版本）
- 提供选项控制
- 支持多种发送模式
- 扩展性考虑

## 6. 实际应用模板

### 6.1 快速原型模板
```c
// 用于快速测试新消息类型
int send_test_message(struct peer *peer, uint8_t *payload, size_t len) {
    return send_minimal(peer->connection, 250, payload, len);  // 使用自定义类型250
}
```

### 6.2 生产应用模板
```c
// 用于生产环境的自定义消息
int send_production_message(struct peer *peer, uint8_t msg_type, 
                           uint8_t *data, size_t len) {
    if (!peer || !peer->connection) return -EINVAL;
    if (!peer_established(peer->connection)) return -ENOTCONN;
    
    return send_safe_minimal(peer->connection, msg_type, data, len);
}
```

## 7. 性能考虑

### 7.1 最小化内存分配
- 复用 stream 对象（如果可能）
- 预分配固定大小的缓冲区
- 避免不必要的数据复制

### 7.2 减少系统调用
- 批量发送多个消息
- 使用异步 I/O
- 合并小消息

### 7.3 优化关键路径
```c
// 热路径优化版本
static inline int send_hot_path(struct peer_connection *conn, 
                               uint8_t type, uint32_t data) {
    struct stream *s = stream_new(BGP_HEADER_SIZE + 4);
    bgp_packet_set_marker(s, type);
    stream_putl(s, data);  // 直接写入4字节数据
    bgp_packet_set_size(s);
    stream_fifo_push(conn->obuf, s);
    bgp_writes_on(conn);
    return 0;
}
```

## 8. 错误处理策略

### 8.1 最小错误处理
```c
if (!conn || !s) return -1;  // 简单失败返回
```

### 8.2 详细错误处理
```c
if (!conn) return -EINVAL;           // 参数错误
if (!s) return -ENOMEM;              // 内存不足
if (len > MAX_SIZE) return -EMSGSIZE; // 消息过大
```

## 9. 调试支持

### 9.1 最小调试
```c
#ifdef DEBUG
printf("Sending message type %u, %zu bytes\n", type, len);
#endif
```

### 9.2 完整调试
```c
if (bgp_debug_neighbor_events(peer)) {
    zlog_debug("%s: Sending custom message type %u (%zu bytes)",
               peer->host, msg_type, total_len);
}
```

## 10. 总结

**最小发送自定义消息函数的核心思路：**

1. **从最简单的5行代码开始**
2. **逐步添加必要的安全检查**  
3. **保持核心逻辑不变**
4. **根据需求渐进增强**
5. **始终考虑性能和稳定性**

这种设计思路的优势是：
- 快速实现和验证
- 容易理解和维护
- 支持渐进式开发
- 适应不同复杂度需求
- 最小化出错风险
