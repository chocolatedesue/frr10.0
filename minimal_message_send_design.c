/*
 * 自定义消息发送函数 - 从最小到完整的渐进式设计
 * 基于 bgp_notify_send_internal 的设计思路
 */

#include <zebra.h>
#include "bgpd/bgpd.h"
#include "bgpd/bgp_packet.h"

/* ================ 版本1: 最小化版本 (5行核心代码) ================ */

/**
 * 最简版本：只包含绝对必需的功能
 * 适用场景：快速原型、测试环境
 */
static int send_message_v1_minimal(
    struct peer_connection *connection,
    uint8_t msg_type,
    uint8_t *data,
    size_t data_len)
{
    struct stream *s = stream_new(BGP_STANDARD_MESSAGE_MAX_PACKET_SIZE);
    bgp_packet_set_marker(s, msg_type);
    if (data) stream_write(s, data, data_len);
    bgp_packet_set_size(s);
    stream_fifo_push(connection->obuf, s);
    bgp_writes_on(connection);
    return 0;
}

/* ================ 版本2: 基础安全版本 ================ */

/**
 * 基础安全版本：添加基本的错误检查和线程安全
 * 适用场景：开发环境、基本生产应用
 */
static int send_message_v2_safe(
    struct peer_connection *connection,
    uint8_t msg_type,
    uint8_t *data,
    size_t data_len)
{
    struct stream *s;
    struct peer *peer;
    
    // 基本验证
    if (!connection || !(peer = connection->peer))
        return -1;
    
    // 线程安全
    frr_mutex_lock_autounlock(&connection->io_mtx);
    
    // 内存分配检查
    s = stream_new(peer->max_packet_size);
    if (!s)
        return -1;
    
    // 构建消息
    bgp_packet_set_marker(s, msg_type);
    if (data && data_len > 0)
        stream_write(s, data, data_len);
    bgp_packet_set_size(s);
    
    // 发送
    stream_fifo_push(connection->obuf, s);
    bgp_writes_on(connection);
    
    return 0;
}

/* ================ 版本3: 生产级版本 ================ */

/**
 * 生产级版本：完整的错误处理、调试支持
 * 适用场景：生产环境
 */
static int send_message_v3_production(
    struct peer_connection *connection,
    uint8_t msg_type,
    uint8_t *data,
    size_t data_len)
{
    struct stream *s;
    struct peer *peer;
    size_t total_len;
    
    // 参数验证
    if (!connection || !(peer = connection->peer)) {
        flog_err(EC_BGP_PKT_PROCESS, "Invalid connection or peer");
        return -EINVAL;
    }
    
    if (data_len > BGP_MAX_PACKET_SIZE) {
        flog_err(EC_BGP_PKT_PROCESS, "Message too large: %zu bytes", data_len);
        return -EMSGSIZE;
    }
    
    // 状态检查
    if (!peer_established(connection)) {
        zlog_debug("%s: Cannot send message, peer not established", peer->host);
        return -ENOTCONN;
    }
    
    // 线程安全
    frr_mutex_lock_autounlock(&connection->io_mtx);
    
    // 内存分配
    s = stream_new(peer->max_packet_size);
    if (!s) {
        flog_err(EC_BGP_PKT_PROCESS, "Failed to allocate stream");
        return -ENOMEM;
    }
    
    // 构建消息
    bgp_packet_set_marker(s, msg_type);
    if (data && data_len > 0) {
        stream_write(s, data, data_len);
    }
    bgp_packet_set_size(s);
    total_len = stream_get_endp(s);
    
    // 调试信息
    if (bgp_debug_neighbor_events(peer)) {
        zlog_debug("%s: Sending custom message type %u (%zu bytes)",
                   peer->host, msg_type, total_len);
    }
    
    // 统计更新
    atomic_fetch_add_explicit(&peer->open_out, 1, memory_order_relaxed);
    
    // 发送
    stream_fifo_push(connection->obuf, s);
    bgp_writes_on(connection);
    
    // 钩子调用
    hook_call(bgp_packet_send, peer, msg_type, total_len, s);
    
    return 0;
}

/* ================ 版本4: 完全可配置版本 ================ */

/**
 * 消息发送选项结构
 */
struct custom_message_options {
    bool force_send;        // 强制发送（清空缓冲区）
    bool enable_debug;      // 启用调试输出
    bool update_stats;      // 更新统计信息
    bool call_hooks;        // 调用钩子函数
    uint32_t timeout_ms;    // 发送超时（毫秒）
};

/**
 * 完全可配置版本：支持各种选项和扩展
 * 适用场景：需要精细控制的高级应用
 */
static int send_message_v4_configurable(
    struct peer_connection *connection,
    uint8_t msg_type,
    uint8_t *data,
    size_t data_len,
    struct custom_message_options *opts)
{
    struct stream *s;
    struct peer *peer;
    size_t total_len;
    struct custom_message_options default_opts = {
        .force_send = false,
        .enable_debug = true,
        .update_stats = true,
        .call_hooks = true,
        .timeout_ms = 5000
    };
    
    // 使用默认选项（如果未提供）
    if (!opts)
        opts = &default_opts;
    
    // 参数验证
    if (!connection || !(peer = connection->peer)) {
        if (opts->enable_debug)
            flog_err(EC_BGP_PKT_PROCESS, "Invalid connection or peer");
        return -EINVAL;
    }
    
    if (data_len > BGP_MAX_PACKET_SIZE) {
        if (opts->enable_debug)
            flog_err(EC_BGP_PKT_PROCESS, "Message too large: %zu bytes", data_len);
        return -EMSGSIZE;
    }
    
    // 状态检查
    if (!peer_established(connection)) {
        if (opts->enable_debug)
            zlog_debug("%s: Cannot send message, peer not established", peer->host);
        return -ENOTCONN;
    }
    
    // 线程安全
    frr_mutex_lock_autounlock(&connection->io_mtx);
    
    // 内存分配
    s = stream_new(peer->max_packet_size);
    if (!s) {
        if (opts->enable_debug)
            flog_err(EC_BGP_PKT_PROCESS, "Failed to allocate stream");
        return -ENOMEM;
    }
    
    // 构建消息
    bgp_packet_set_marker(s, msg_type);
    if (data && data_len > 0) {
        stream_write(s, data, data_len);
    }
    bgp_packet_set_size(s);
    total_len = stream_get_endp(s);
    
    // 强制发送处理
    if (opts->force_send) {
        stream_fifo_clean(connection->obuf);
        if (opts->enable_debug)
            zlog_debug("%s: Clearing output buffer for priority message", peer->host);
    }
    
    // 调试信息
    if (opts->enable_debug && bgp_debug_neighbor_events(peer)) {
        zlog_debug("%s: Sending custom message type %u (%zu bytes)%s",
                   peer->host, msg_type, total_len,
                   opts->force_send ? " [PRIORITY]" : "");
    }
    
    // 统计更新
    if (opts->update_stats) {
        atomic_fetch_add_explicit(&peer->open_out, 1, memory_order_relaxed);
    }
    
    // 发送
    stream_fifo_push(connection->obuf, s);
    bgp_writes_on(connection);
    
    // 钩子调用
    if (opts->call_hooks) {
        hook_call(bgp_packet_send, peer, msg_type, total_len, s);
    }
    
    return 0;
}

/* ================ 公共接口函数 ================ */

/**
 * 标准接口：发送简单自定义消息
 */
int bgp_send_custom_message(struct peer *peer, uint8_t msg_type, 
                           uint8_t *data, size_t data_len)
{
    if (!peer || !peer->connection)
        return -EINVAL;
    
    return send_message_v2_safe(peer->connection, msg_type, data, data_len);
}

/**
 * 高级接口：发送可配置自定义消息
 */
int bgp_send_custom_message_advanced(struct peer *peer, uint8_t msg_type,
                                    uint8_t *data, size_t data_len,
                                    struct custom_message_options *opts)
{
    if (!peer || !peer->connection)
        return -EINVAL;
    
    return send_message_v4_configurable(peer->connection, msg_type, 
                                       data, data_len, opts);
}

/**
 * 紧急接口：发送高优先级消息
 */
int bgp_send_custom_message_urgent(struct peer *peer, uint8_t msg_type,
                                  uint8_t *data, size_t data_len)
{
    if (!peer || !peer->connection)
        return -EINVAL;
    
    struct custom_message_options opts = {
        .force_send = true,
        .enable_debug = true,
        .update_stats = true,
        .call_hooks = true,
        .timeout_ms = 1000
    };
    
    return send_message_v4_configurable(peer->connection, msg_type, 
                                       data, data_len, &opts);
}

/* ================ 使用示例 ================ */

/**
 * 示例：发送自定义心跳消息
 */
void example_send_custom_heartbeat(struct peer *peer)
{
    uint8_t heartbeat_data[] = {0x01, 0x02, 0x03, 0x04}; // 自定义数据
    uint8_t custom_msg_type = 200; // 自定义消息类型
    
    // 方法1：简单发送
    bgp_send_custom_message(peer, custom_msg_type, 
                           heartbeat_data, sizeof(heartbeat_data));
    
    // 方法2：高优先级发送
    bgp_send_custom_message_urgent(peer, custom_msg_type,
                                  heartbeat_data, sizeof(heartbeat_data));
    
    // 方法3：完全自定义
    struct custom_message_options opts = {
        .force_send = false,
        .enable_debug = true,
        .update_stats = false,  // 心跳不计入统计
        .call_hooks = false,    // 心跳不触发钩子
        .timeout_ms = 3000
    };
    
    bgp_send_custom_message_advanced(peer, custom_msg_type,
                                    heartbeat_data, sizeof(heartbeat_data), 
                                    &opts);
}

/* ================ 设计总结 ================ */

/*
 * 最小化自定义消息发送函数的核心思路：
 * 
 * 1. 最小核心（5个步骤）：
 *    - 分配内存
 *    - 设置消息头
 *    - 写入数据
 *    - 设置大小
 *    - 添加到队列
 * 
 * 2. 渐进增强：
 *    - V1: 最小功能（5行代码）
 *    - V2: 基础安全（错误检查+线程安全）
 *    - V3: 生产级别（完整错误处理+调试）
 *    - V4: 完全可配置（选项+扩展性）
 * 
 * 3. 设计原则：
 *    - 从简单开始，逐步增强
 *    - 保持核心逻辑不变
 *    - 提供多个接口级别
 *    - 向后兼容
 * 
 * 4. 适用场景：
 *    - V1: 快速原型、概念验证
 *    - V2: 开发测试、基本应用
 *    - V3: 生产环境、稳定版本
 *    - V4: 高级应用、精细控制
 */
