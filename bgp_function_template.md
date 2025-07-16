# 仿写 BGP 通知函数的设计思路和模板

## 1. 通用消息发送函数设计模式

基于 `bgp_notify_send_internal` 的分析，我们可以抽象出一个通用的网络消息发送函数模式：

### 1.1 函数框架模板

```c
/**
 * 通用协议消息发送函数模板
 * @param connection 连接对象
 * @param msg_type 消息类型
 * @param msg_data 消息数据
 * @param data_len 数据长度
 * @param options 选项参数
 * @return 发送结果
 */
static int protocol_message_send_internal(
    struct connection *connection,
    uint8_t msg_type,
    uint8_t sub_type,
    uint8_t *msg_data,
    size_t data_len,
    struct send_options *options)
{
    struct stream *s = NULL;
    struct peer *peer = connection->peer;
    int result = 0;
    
    // 1. 参数验证和初始化
    if (!connection || !peer) {
        return -EINVAL;
    }
    
    // 2. 线程安全保护
    frr_mutex_lock_autounlock(&connection->io_mtx);
    
    // 3. 预处理和策略选择
    bool special_handling = should_use_special_handling(peer, msg_type, sub_type);
    
    // 4. 消息构建
    s = stream_new(peer->max_packet_size);
    if (!s) {
        return -ENOMEM;
    }
    
    // 5. 填充消息内容
    result = build_message_content(s, msg_type, sub_type, msg_data, data_len, special_handling);
    if (result < 0) {
        goto cleanup;
    }
    
    // 6. 输出准备
    prepare_output_buffer(connection);
    
    // 7. 调试信息处理
    if (options && options->enable_debug) {
        record_debug_info(peer, s, msg_type, options);
    }
    
    // 8. 状态更新
    update_peer_state(peer, msg_type, sub_type);
    
    // 9. 发送处理
    result = send_message(connection, peer, s);
    
cleanup:
    if (s && result < 0) {
        stream_free(s);
    }
    
    return result;
}
```

### 1.2 具体实现示例：BGP UPDATE 消息发送函数

```c
/**
 * BGP UPDATE 消息发送函数
 * 仿照 bgp_notify_send_internal 的设计模式
 */
static int bgp_update_send_internal(
    struct peer_connection *connection,
    struct bgp_update_info *update_info,
    bool force_send)
{
    struct stream *s;
    struct peer *peer = connection->peer;
    size_t total_len = 0;
    int result = 0;
    
    // 1. 参数验证
    if (!connection || !peer || !update_info) {
        flog_err(EC_BGP_UPDATE_SND, "Invalid parameters for BGP UPDATE");
        return -EINVAL;
    }
    
    // 2. 线程安全保护
    frr_mutex_lock_autounlock(&connection->io_mtx);
    /* ============================================== */
    
    // 3. 检查连接状态
    if (!peer_established(connection)) {
        zlog_debug("%s: Peer not established, skipping UPDATE", peer->host);
        return -ENOTCONN;
    }
    
    // 4. 检查是否需要特殊处理（如路由刷新）
    bool is_eor = (update_info->nlri_count == 0 && update_info->withdraw_count == 0);
    
    // 5. 分配数据流
    s = stream_new(peer->max_packet_size);
    if (!s) {
        flog_err(EC_BGP_UPDATE_SND, "Failed to allocate stream for UPDATE");
        return -ENOMEM;
    }
    
    // 6. 构建 UPDATE 消息
    bgp_packet_set_marker(s, BGP_MSG_UPDATE);
    
    // 6.1 写入撤销路由
    if (update_info->withdraw_count > 0) {
        stream_putw(s, update_info->withdraw_len);
        stream_write(s, update_info->withdraw_data, update_info->withdraw_len);
    } else {
        stream_putw(s, 0);
    }
    
    // 6.2 写入路径属性
    if (update_info->attr_len > 0) {
        stream_putw(s, update_info->attr_len);
        stream_write(s, update_info->attr_data, update_info->attr_len);
    } else {
        stream_putw(s, 0);
    }
    
    // 6.3 写入 NLRI
    if (update_info->nlri_count > 0) {
        stream_write(s, update_info->nlri_data, update_info->nlri_len);
    }
    
    // 7. 设置包大小
    bgp_packet_set_size(s);
    total_len = stream_get_endp(s);
    
    // 8. 清理输出缓冲区（如果需要）
    if (force_send) {
        stream_fifo_clean(connection->obuf);
    }
    
    // 9. 调试信息记录
    if (bgp_debug_update(peer)) {
        record_update_debug_info(peer, update_info, total_len, is_eor);
    }
    
    // 10. 更新统计信息
    atomic_fetch_add_explicit(&peer->update_out, 1, memory_order_relaxed);
    peer->last_update_sent = monotime(NULL);
    
    // 11. 添加到输出队列
    stream_fifo_push(connection->obuf, s);
    
    // 12. 触发写入
    bgp_writes_on(connection);
    
    // 13. 调用钩子
    hook_call(bgp_packet_send, peer, BGP_MSG_UPDATE, total_len, s);
    
    if (bgp_debug_update(peer)) {
        zlog_debug("%s: Sent UPDATE message (%zu bytes)", peer->host, total_len);
    }
    
    return 0;
}
```

## 2. 函数设计原则

### 2.1 结构化设计
1. **参数验证**: 始终验证输入参数
2. **资源管理**: 确保资源正确分配和释放
3. **错误处理**: 提供清晰的错误返回路径
4. **线程安全**: 在多线程环境中保护共享资源

### 2.2 可扩展性
```c
// 消息处理策略接口
struct message_handler {
    int (*validate)(void *data);
    int (*build)(struct stream *s, void *data);
    void (*debug)(struct peer *peer, void *data);
    void (*cleanup)(void *data);
};

// 注册不同类型的消息处理器
static struct message_handler *handlers[MSG_TYPE_MAX];

int register_message_handler(uint8_t msg_type, struct message_handler *handler)
{
    if (msg_type >= MSG_TYPE_MAX || !handler)
        return -EINVAL;
    
    handlers[msg_type] = handler;
    return 0;
}
```

### 2.3 调试支持
```c
// 统一的调试信息结构
struct debug_info {
    uint64_t timestamp;
    uint8_t msg_type;
    uint8_t sub_type;
    size_t data_len;
    char peer_addr[INET6_ADDRSTRLEN];
    char details[256];
};

// 调试信息记录函数
static void record_debug_info(struct peer *peer, uint8_t msg_type, 
                             uint8_t sub_type, size_t len)
{
    if (!bgp_debug_enabled(msg_type))
        return;
    
    struct debug_info info = {0};
    info.timestamp = monotime(NULL);
    info.msg_type = msg_type;
    info.sub_type = sub_type;
    info.data_len = len;
    
    inet_ntop(peer->su.sa.sa_family, &peer->su.sin.sin_addr,
              info.peer_addr, sizeof(info.peer_addr));
    
    snprintf(info.details, sizeof(info.details),
             "Sent %s message to %s (%zu bytes)",
             bgp_msg_type_str(msg_type), peer->host, len);
    
    // 记录到日志或调试缓冲区
    bgp_debug_log(&info);
}
```

## 3. 完整的仿写示例：BGP KEEPALIVE 发送函数

```c
/**
 * BGP KEEPALIVE 消息发送函数
 * 基于 bgp_notify_send_internal 的设计模式
 */
static int bgp_keepalive_send_internal(
    struct peer_connection *connection,
    bool high_priority)
{
    struct stream *s;
    struct peer *peer = connection->peer;
    int result = 0;
    
    // 1. 参数验证和状态检查
    if (!connection || !peer) {
        return -EINVAL;
    }
    
    if (!peer_established(connection)) {
        zlog_debug("%s: Cannot send KEEPALIVE, peer not established", 
                   peer->host);
        return -ENOTCONN;
    }
    
    // 2. 线程安全保护
    frr_mutex_lock_autounlock(&connection->io_mtx);
    /* ============================================== */
    
    // 3. 分配数据流
    s = stream_new(BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE);
    if (!s) {
        flog_err(EC_BGP_KEEPALIVE_SND, 
                 "%s: Failed to allocate stream for KEEPALIVE", peer->host);
        return -ENOMEM;
    }
    
    // 4. 构建 KEEPALIVE 消息
    bgp_packet_set_marker(s, BGP_MSG_KEEPALIVE);
    
    // KEEPALIVE 消息没有额外数据，只需要设置包大小
    bgp_packet_set_size(s);
    
    // 5. 高优先级处理
    if (high_priority) {
        // 清空输出缓冲区，优先发送 KEEPALIVE
        stream_fifo_clean(connection->obuf);
    }
    
    // 6. 调试信息
    if (bgp_debug_keepalive(peer)) {
        zlog_debug("%s: Sending KEEPALIVE message%s", 
                   peer->host, high_priority ? " (high priority)" : "");
    }
    
    // 7. 更新统计信息
    atomic_fetch_add_explicit(&peer->keepalive_out, 1, memory_order_relaxed);
    peer->last_keepalive_sent = monotime(NULL);
    
    // 8. 重置 keepalive 定时器
    if (peer->connection->t_keepalive) {
        EVENT_OFF(peer->connection->t_keepalive);
    }
    
    uint32_t keepalive_interval = peer->v_keepalive;
    if (keepalive_interval > 0) {
        event_add_timer(bm->master, bgp_keepalive_timer, connection, 
                       keepalive_interval, &peer->connection->t_keepalive);
    }
    
    // 9. 添加到输出队列
    stream_fifo_push(connection->obuf, s);
    
    // 10. 触发写入
    bgp_writes_on(connection);
    
    // 11. 调用钩子函数
    hook_call(bgp_packet_send, peer, BGP_MSG_KEEPALIVE, 
              stream_get_endp(s), s);
    
    return 0;
}

// 公共接口函数
void bgp_keepalive_send(struct peer *peer)
{
    if (!peer || !peer->connection) {
        return;
    }
    
    int result = bgp_keepalive_send_internal(peer->connection, false);
    if (result < 0) {
        flog_err(EC_BGP_KEEPALIVE_SND, 
                 "%s: Failed to send KEEPALIVE: %s", 
                 peer->host, strerror(-result));
    }
}

void bgp_keepalive_send_urgent(struct peer *peer)
{
    if (!peer || !peer->connection) {
        return;
    }
    
    int result = bgp_keepalive_send_internal(peer->connection, true);
    if (result < 0) {
        flog_err(EC_BGP_KEEPALIVE_SND, 
                 "%s: Failed to send urgent KEEPALIVE: %s", 
                 peer->host, strerror(-result));
    }
}
```

## 4. 设计要点总结

### 4.1 必需组件
1. **参数验证**
2. **线程安全保护**
3. **资源管理**
4. **错误处理**
5. **调试支持**
6. **状态管理**

### 4.2 可选组件
1. **性能优化**
2. **钩子机制**
3. **统计信息**
4. **特殊处理逻辑**

### 4.3 关键考虑
1. **内存泄漏预防**
2. **竞态条件避免**
3. **协议规范遵循**
4. **可维护性**
5. **可测试性**

这个设计模式可以适用于任何需要可靠消息传输的网络协议实现。
