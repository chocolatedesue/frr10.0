# BGP NOTIFY消息发送流程分析

## 1. 概述

`bgp_notify_send`是FRR BGP中用于发送BGP NOTIFICATION消息的核心函数。NOTIFICATION消息是BGP协议中用于报告错误条件和关闭连接的重要机制。

## 2. 函数层次结构

```
bgp_notify_send() [简单版本，无数据]
│
├── bgp_notify_send_with_data() [带数据版本]
│
└── bgp_notify_send_internal() [内部核心实现]
    │
    ├── bgp_notify_send_hard_reset() [判断是否需要Hard Reset]
    ├── bgp_notify_encapsulate_hard_reset() [封装Hard Reset]
    └── bgp_write_notify() [立即写入socket]
```

## 3. 主要函数分析

### 3.1 bgp_notify_send() - 基本版本

```c
void bgp_notify_send(struct peer_connection *connection, uint8_t code,
                     uint8_t sub_code)
{
    bgp_notify_send_internal(connection, code, sub_code, NULL, 0, true);
}
```

**作用**：发送不带附加数据的BGP NOTIFICATION消息
**参数**：
- `connection`: peer连接对象
- `code`: BGP错误代码
- `sub_code`: BGP错误子代码

### 3.2 bgp_notify_send_with_data() - 带数据版本

```c
void bgp_notify_send_with_data(struct peer_connection *connection, uint8_t code,
                               uint8_t sub_code, uint8_t *data, size_t datalen)
{
    bgp_notify_send_internal(connection, code, sub_code, data, datalen, true);
}
```

**作用**：发送带附加诊断数据的BGP NOTIFICATION消息
**参数**：
- 前三个参数同上
- `data`: 附加的诊断数据
- `datalen`: 数据长度

### 3.3 bgp_notify_send_internal() - 核心实现

这是NOTIFY消息发送的核心函数，完整流程如下：

#### 第一阶段：初始化和线程安全
```c
static void bgp_notify_send_internal(struct peer_connection *connection,
                                     uint8_t code, uint8_t sub_code,
                                     uint8_t *data, size_t datalen,
                                     bool use_curr)
{
    struct stream *s;
    struct peer *peer = connection->peer;
    bool hard_reset = bgp_notify_send_hard_reset(peer, code, sub_code);

    /* 锁定I/O互斥锁，防止其他线程推送数据包 */
    frr_mutex_lock_autounlock(&connection->io_mtx);
    // ...
}
```

**关键点**：
- 使用互斥锁确保线程安全
- 检查是否需要发送Hard Reset消息

#### 第二阶段：创建NOTIFY数据包
```c
/* 分配新的stream */
s = stream_new(peer->max_packet_size);

/* 制作notify数据包 */
bgp_packet_set_marker(s, BGP_MSG_NOTIFY);

/* 检查是否应该发送Hard Reset Notification */
if (hard_reset) {
    uint8_t *hard_reset_message = bgp_notify_encapsulate_hard_reset(
        code, sub_code, data, datalen);

    /* Hard Reset将另一个NOTIFICATION消息封装在其数据部分中 */
    stream_putc(s, BGP_NOTIFY_CEASE);
    stream_putc(s, BGP_NOTIFY_CEASE_HARD_RESET);
    stream_write(s, hard_reset_message, datalen + 2);

    XFREE(MTYPE_BGP_NOTIFICATION, hard_reset_message);
} else {
    stream_putc(s, code);
    stream_putc(s, sub_code);
    if (data)
        stream_write(s, data, datalen);
}

/* 设置BGP数据包长度 */
bgp_packet_set_size(s);
```

**数据包格式**：
- **标准NOTIFY**: BGP头部 + 错误代码 + 错误子代码 + 数据
- **Hard Reset**: BGP头部 + CEASE代码 + HARD_RESET子代码 + 封装的原始NOTIFY

#### 第三阶段：清理输出缓冲区
```c
/* 清空输出缓冲区 */
stream_fifo_clean(connection->obuf);
```

**目的**：确保NOTIFY消息是连接关闭前发送的最后一个消息

#### 第四阶段：调试和记录
```c
/* 保存最后一个数据包用于调试 */
if (use_curr && peer->curr) {
    size_t packetsize = stream_get_endp(peer->curr);
    if (peer->last_reset_cause)
        stream_free(peer->last_reset_cause);
    peer->last_reset_cause = stream_dup(peer->curr);
}

/* 调试输出 */
bgp_notify_print(peer, &bgp_notify, "sending", hard_reset);
```

#### 第五阶段：设置重置原因
```c
/* peer重置原因 */
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

#### 第六阶段：发送数据包
```c
/* 将数据包添加到peer的输出队列 */
stream_fifo_push(connection->obuf, s);

bgp_peer_gr_flags_update(peer);
BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA(peer->bgp, peer->bgp->peer);

/* 立即写入NOTIFY消息 */
bgp_write_notify(connection, peer);
```

## 4. bgp_write_notify() - 立即发送机制

```c
static void bgp_write_notify(struct peer_connection *connection,
                             struct peer *peer)
{
    int ret, val;
    struct stream *s;

    /* 从输出队列弹出数据包 */
    s = stream_fifo_pop(connection->obuf);

    /* 直接写入socket（非阻塞模式） */
    ret = write(connection->fd, STREAM_DATA(s), stream_get_endp(s));

    if (ret <= 0) {
        stream_free(s);
        BGP_EVENT_ADD(connection, TCP_fatal_error);
        return;
    }

    /* 禁用Nagle算法，确保NOTIFY消息立即发出 */
    val = 1;
    setsockopt(connection->fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

    /* 更新统计 */
    atomic_fetch_add_explicit(&peer->notify_out, 1, memory_order_relaxed);

    /* 双倍启动定时器（backoff机制） */
    peer->v_start *= 2;
    if (peer->v_start >= (60 * 2))
        peer->v_start = (60 * 2);

    /* 触发BGP停止事件 */
    BGP_EVENT_ADD(connection, BGP_Stop);

    stream_free(s);
}
```

**关键特性**：
1. **立即发送**：绕过正常的数据包队列机制，直接写入socket
2. **TCP_NODELAY**：禁用Nagle算法确保消息立即发送
3. **连接关闭**：发送后立即触发BGP_Stop事件
4. **重连策略**：实现指数退避机制

## 5. Hard Reset机制

### 5.1 判断条件
```c
bool bgp_notify_send_hard_reset(struct peer *peer, uint8_t code, uint8_t subcode)
{
    /* 当交换了"N"位时，使用Hard Reset消息来指示对端会话完全终止 */
    if (!bgp_has_graceful_restart_notification(peer))
        return false;

    if (code == BGP_NOTIFY_CEASE) {
        switch (subcode) {
        case BGP_NOTIFY_CEASE_MAX_PREFIX:
        case BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN:
        case BGP_NOTIFY_CEASE_PEER_UNCONFIG:
        case BGP_NOTIFY_CEASE_HARD_RESET:
        case BGP_NOTIFY_CEASE_BFD_DOWN:
            return true;
        case BGP_NOTIFY_CEASE_ADMIN_RESET:
            /* 用户控制：`bgp hard-administrative-reset` */
            return CHECK_FLAG(peer->bgp->flags, BGP_FLAG_HARD_ADMIN_RESET);
        default:
            break;
        }
    }
    return false;
}
```

### 5.2 封装机制
```c
static uint8_t *bgp_notify_encapsulate_hard_reset(uint8_t code, uint8_t subcode,
                                                  uint8_t *data, size_t datalen)
{
    uint8_t *message = XCALLOC(MTYPE_BGP_NOTIFICATION, datalen + 2);

    /* 错误代码 */
    message[0] = code;
    /* 子代码 */
    message[1] = subcode;
    /* 数据 */
    if (datalen)
        memcpy(message + 2, data, datalen);

    return message;
}
```

## 6. NOTIFY消息的作用

### 6.1 错误报告
- 报告BGP协议错误（如消息格式错误、属性错误等）
- 提供详细的错误代码和子代码
- 携带诊断数据帮助调试

### 6.2 连接管理
- 优雅地关闭BGP会话
- 通知对端连接即将终止
- 触发连接状态机转换

### 6.3 故障恢复
- 实现指数退避重连机制
- 支持Graceful Restart场景
- 提供Hard Reset选项用于完全重置

## 7. 与普通数据包发送的区别

| 特性 | 普通数据包 | NOTIFY消息 |
|------|------------|------------|
| 发送方式 | 队列缓冲 | 立即发送 |
| 线程安全 | bgp_packet_add + bgp_writes_on | 直接write() |
| 输出队列 | 排队等待 | 清空队列后发送 |
| TCP选项 | 默认Nagle | 强制TCP_NODELAY |
| 连接状态 | 保持连接 | 立即关闭 |
| 重试机制 | 正常重试 | 指数退避 |

## 8. 使用场景

### 8.1 协议错误
```c
// 消息格式错误
bgp_notify_send(connection, BGP_NOTIFY_HEADER_ERR, BGP_NOTIFY_HEADER_BAD_MESLEN);

// 属性错误
bgp_notify_send_with_data(connection, BGP_NOTIFY_UPDATE_ERR, 
                         BGP_NOTIFY_UPDATE_MAL_ATTR, attr_data, attr_len);
```

### 8.2 管理操作
```c
// 管理员关闭
bgp_notify_send(connection, BGP_NOTIFY_CEASE, BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);

// 配置变更
bgp_notify_send(connection, BGP_NOTIFY_CEASE, BGP_NOTIFY_CEASE_PEER_UNCONFIG);
```

### 8.3 资源限制
```c
// 前缀数量超限
bgp_notify_send(connection, BGP_NOTIFY_CEASE, BGP_NOTIFY_CEASE_MAX_PREFIX);
```

## 9. 总结

`bgp_notify_send`是BGP协议栈中的关键函数，负责：

1. **构造标准的BGP NOTIFICATION消息**
2. **实现立即发送机制**，绕过正常排队
3. **支持Hard Reset扩展**，用于Graceful Restart场景
4. **提供完整的错误报告和调试信息**
5. **触发连接关闭和状态转换**
6. **实现重连的指数退避策略**

这个机制确保了BGP连接在出现错误时能够快速、准确地通知对端，并优雅地关闭连接，是BGP协议可靠性的重要保障。
