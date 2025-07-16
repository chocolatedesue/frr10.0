# BGP_NOTIFY_SEND_INTERNAL 函数分析

## 1. 函数概述
- **功能**: 创建并发送 BGP NOTIFICATION 消息
- **位置**: bgpd/bgp_packet.c:913-1049
- **类型**: 内部静态函数
- **线程安全**: 是（使用互斥锁保护 I/O 操作）

## 2. 函数参数
```c
struct peer_connection *connection  // BGP 连接对象
uint8_t code                       // BGP 错误代码
uint8_t sub_code                   // BGP 错误子代码
uint8_t *data                      // 附加数据
size_t datalen                     // 数据长度
bool use_curr                      // 是否使用当前包进行调试
```

## 3. 函数执行流程

### 3.1 初始化阶段
```
1. 获取 peer 对象
2. 检查是否需要发送硬重置通知
3. 加锁保护 I/O 操作
4. 分配新的数据流
```

### 3.2 消息构建阶段
```
1. 设置 BGP 消息标记（NOTIFICATION）
2. 根据硬重置标志选择消息格式：
   - 硬重置：封装原始通知到 CEASE/HARD_RESET 中
   - 普通通知：直接填充错误代码和数据
3. 设置包大小
```

### 3.3 输出准备阶段
```
1. 清空输出缓冲区
2. 保存调试信息（如果启用）
3. 设置对等体重置原因
```

### 3.4 发送阶段
```
1. 将包添加到输出队列
2. 更新优雅重启标志
3. 直接写入套接字
```

## 4. 关键技术点

### 4.1 线程安全
```c
frr_mutex_lock_autounlock(&connection->io_mtx);
```
- 使用自动解锁互斥量保护 I/O 操作
- 防止多线程同时操作输出缓冲区

### 4.2 硬重置处理
```c
bool hard_reset = bgp_notify_send_hard_reset(peer, code, sub_code);
if (hard_reset) {
    // 封装原始通知到硬重置消息中
    uint8_t *hard_reset_message = bgp_notify_encapsulate_hard_reset(
        code, sub_code, data, datalen);
    stream_putc(s, BGP_NOTIFY_CEASE);
    stream_putc(s, BGP_NOTIFY_CEASE_HARD_RESET);
    stream_write(s, hard_reset_message, datalen + 2);
}
```

### 4.3 调试信息处理
```c
if (use_curr && peer->curr) {
    // 保存最后一个包用于调试
    if (peer->last_reset_cause)
        stream_free(peer->last_reset_cause);
    peer->last_reset_cause = stream_dup(peer->curr);
}
```

### 4.4 数据流操作
```c
// 数据流创建和填充
s = stream_new(peer->max_packet_size);
bgp_packet_set_marker(s, BGP_MSG_NOTIFY);
stream_putc(s, code);
stream_putc(s, sub_code);
if (data)
    stream_write(s, data, datalen);
bgp_packet_set_size(s);
```

## 5. 错误处理和状态管理

### 5.1 重置原因设置
```c
if (code == BGP_NOTIFY_CEASE) {
    if (sub_code == BGP_NOTIFY_CEASE_ADMIN_RESET)
        peer->last_reset = PEER_DOWN_USER_RESET;
    else if (sub_code == BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN) {
        if (CHECK_FLAG(peer->sflags, PEER_STATUS_RTT_SHUTDOWN))
            peer->last_reset = PEER_DOWN_RTT_SHUTDOWN;
        else
            peer->last_reset = PEER_DOWN_USER_SHUTDOWN;
    } else
        peer->last_reset = PEER_DOWN_NOTIFY_SEND;
} else
    peer->last_reset = PEER_DOWN_NOTIFY_SEND;
```

### 5.2 优雅重启处理
```c
bgp_peer_gr_flags_update(peer);
BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA(peer->bgp, peer->bgp->peer);
```

## 6. 函数特点

### 优点
1. **线程安全**: 使用互斥锁保护关键操作
2. **功能完整**: 支持普通通知和硬重置
3. **调试友好**: 详细的调试信息记录
4. **错误处理**: 完善的错误状态管理
5. **协议兼容**: 严格遵循 BGP 协议规范

### 关键设计模式
1. **资源管理**: 自动解锁互斥量
2. **策略模式**: 根据条件选择不同的消息格式
3. **模板方法**: 固定的消息构建流程
4. **观察者模式**: 调试信息的钩子机制

## 7. 性能考虑
1. **内存管理**: 及时释放动态分配的内存
2. **I/O 优化**: 直接写入套接字，避免缓冲延迟
3. **锁粒度**: 最小化锁的持有时间
4. **数据复制**: 只在必要时复制数据用于调试
