# BGP UPDATE 报文发送和构造流程分析

## 概述

BGP UPDATE报文是BGP协议中用于发送路由信息的核心消息类型。FRR中的BGP UPDATE报文的构造和发送涉及多个模块的协调工作，包括路由广告、Update Group管理、包构造和I/O处理等。

## 整体架构

BGP UPDATE报文的发送流程可以分为以下几个主要阶段：

1. **路由广告阶段**：路由信息被添加到广告队列
2. **Update Group处理**：将路由信息组织到Update Group中
3. **报文构造阶段**：构造具体的BGP UPDATE报文
4. **报文发送阶段**：将报文写入网络

## 核心数据结构

### 1. BGP Advertise结构 (`bgp_advertise.h`)

```c
struct bgp_advertise {
    /* FIFO for advertisement */
    struct bgp_adv_fifo_item fifo;
    
    /* FIFO for this item in the bgp_advertise_attr fifo */
    struct bgp_advertise_attr_fifo_item item;
    
    /* Prefix information */
    struct bgp_dest *dest;
    
    /* Reference pointer */
    struct bgp_adj_out *adj;
    
    /* Advertisement attribute */
    struct bgp_advertise_attr *baa;
    
    /* BGP info */
    struct bgp_path_info *pathi;
};
```

### 2. Update Subgroup结构

Update Subgroup是具有相同出站策略的peer的集合，用于优化报文生成。

### 3. Packet Buffer结构 (`bpacket`)

```c
struct bpacket {
    struct stream *buffer;    // 实际的报文数据
    // 其他元数据
};
```

## UPDATE 报文构造流程

### 1. 主要入口函数

**`subgroup_update_packet()`** (`bgpd/bgp_updgrp_packet.c`)

这是构造BGP UPDATE报文的核心函数，负责：
- 从subgroup的同步队列中获取要广告的路由
- 构造BGP UPDATE报文头部
- 添加路径属性
- 添加NLRI信息

### 2. 报文构造步骤

#### 2.1 报文头部构造
```c
/* 1: Write the BGP message header - 16 bytes marker, 2 bytes length,
 * one byte message type. */
bgp_packet_set_marker(s, BGP_MSG_UPDATE);

/* 2: withdrawn routes length */
stream_putw(s, 0);

/* 3: total attributes length - attrlen_pos stores the position */
attrlen_pos = stream_get_endp(s);
stream_putw(s, 0);
```

#### 2.2 路径属性构造
```c
/* 5: Encode all the attributes, except MP_REACH_NLRI attr. */
total_attr_len = bgp_packet_attribute(
    NULL, peer, s, adv->baa->attr, &vecarr, NULL,
    afi, safi, from, NULL, NULL, 0, 0, 0, path, source_peer);
```

#### 2.3 NLRI信息添加

对于IPv4单播：
```c
if ((afi == AFI_IP && safi == SAFI_UNICAST)
    && !peer_cap_enhe(peer, afi, safi))
    stream_put_prefix_addpath(s, dest_p, addpath_capable, addpath_tx_id);
```

对于其他AFI/SAFI：
```c
/* Encode the prefix in MP_REACH_NLRI attribute */
if (stream_empty(snlri))
    mpattrlen_pos = bgp_packet_mpattr_start(snlri, peer, afi, safi, &vecarr, adv->baa->attr);

bgp_packet_mpattr_prefix(snlri, afi, safi, dest_p, prd,
                        label_pnt, num_labels,
                        addpath_capable, addpath_tx_id,
                        adv->baa->attr);
```

#### 2.4 报文大小设置
```c
/* set the total attribute length correctly */
stream_putw_at(s, attrlen_pos, total_attr_len);

if (!stream_empty(snlri)) {
    packet = stream_dupcat(s, snlri, mpattr_pos);
    bpacket_attr_vec_arr_update(&vecarr, mpattr_pos);
} else
    packet = stream_dup(s);
    
bgp_packet_set_size(packet);
```

### 3. WITHDRAW 报文构造

**`subgroup_withdraw_packet()`** 函数负责构造路由撤销报文：

- 对于IPv4单播：直接在Unfeasible Routes字段中添加撤销的前缀
- 对于其他AFI/SAFI：使用MP_UNREACH_NLRI属性

## UPDATE 报文发送流程

### 1. 触发写入

**`subgroup_trigger_write()`** (`bgpd/bgp_updgrp.c`)

```c
void subgroup_trigger_write(struct update_subgroup *subgrp)
{
    struct peer_af *paf;
    
    /* For each peer in the subgroup, schedule a job to pull packets from
     * the subgroup output queue into their own output queue. This action
     * will trigger a write job on the I/O thread. */
    SUBGRP_FOREACH_PEER(subgrp, paf) {
        struct peer_connection *connection = paf->peer->connection;
        
        if (peer_established(connection))
            event_add_timer_msec(bm->master,
                                bgp_generate_updgrp_packets,
                                connection, 0,
                                &connection->t_generate_updgrp_packets);
    }
}
```

### 2. 报文生成

**`bgp_generate_updgrp_packets()`** (`bgpd/bgp_packet.c`)

这个函数负责：
- 从subgroup的包队列中获取报文
- 为特定peer重新格式化报文（处理peer特定的属性）
- 将报文添加到peer的输出队列

```c
void bgp_generate_updgrp_packets(struct event *thread)
{
    // ...
    do {
        // 遍历所有AFI/SAFI
        for (index = BGP_AF_START; index < BGP_AF_MAX; index++) {
            paf = peer->peer_af_array[index];
            if (!paf || !PAF_SUBGRP(paf))
                continue;
                
            next_pkt = paf->next_pkt_to_send;
            
            /* Try to generate a packet for the peer */
            if (!next_pkt || !next_pkt->buffer) {
                next_pkt = subgroup_withdraw_packet(PAF_SUBGRP(paf));
                if (!next_pkt || !next_pkt->buffer)
                    subgroup_update_packet(PAF_SUBGRP(paf), peer);
                next_pkt = paf->next_pkt_to_send;
            }
            
            if (next_pkt && next_pkt->buffer) {
                /* Found a packet template to send, overwrite
                 * packet with appropriate attributes from peer
                 * and advance peer */
                s = bpacket_reformat_for_peer(next_pkt, paf);
                bgp_packet_add(connection, peer, s);
                bpacket_queue_advance_peer(paf);
            }
        }
    } while (s && (++generated < wpq) && (connection->obuf->count <= bm->outq_limit));
    
    if (generated)
        bgp_writes_on(connection);
}
```

### 3. 报文重新格式化

**`bpacket_reformat_for_peer()`** (`bgpd/bgp_updgrp_packet.c`)

为特定peer重新格式化报文，处理：
- Next-hop属性的peer特定修改
- 其他需要peer特定处理的属性

### 4. I/O处理

**`bgp_writes_on()`** 和 **`bgp_write()`** (`bgpd/bgp_io.c`)

- `bgp_writes_on()`: 启用写入事件
- `bgp_write()`: 实际的写入操作，使用`writev()`系统调用发送数据

```c
static uint16_t bgp_write(struct peer_connection *connection)
{
    // 从peer的输出缓冲区中获取数据包
    // 使用writev()批量发送
    num = writev(connection->fd, iov, iovsz);
    // 错误处理和状态更新
}
```

## 特殊处理

### 1. AddPath支持

如果启用了AddPath功能，会在NLRI前添加Path ID：

```c
if (addpath_capable) {
    stream_putl(s, addpath_tx_id);
}
```

### 2. MP-BGP支持

对于非IPv4单播地址族，使用MP_REACH_NLRI和MP_UNREACH_NLRI属性：

```c
if (afi != AFI_IP || safi != SAFI_UNICAST || peer_cap_enhe(peer, afi, safi)) {
    // 使用MP_REACH_NLRI属性
    bgp_packet_mpattr_prefix(snlri, afi, safi, dest_p, prd,
                            label_pnt, num_labels,
                            addpath_capable, addpath_tx_id,
                            adv->baa->attr);
}
```

### 3. EOR (End-of-RIB) 处理

当没有更多UPDATE需要发送时，发送EOR标记：

```c
if (CHECK_FLAG(peer->cap, PEER_CAP_RESTART_RCV)) {
    if (BGP_SEND_EOR(peer->bgp, afi, safi)) {
        BGP_UPDATE_EOR_PKT(peer, afi, safi, s);
    }
}
```

## 优化机制

### 1. Update Group

将具有相同出站策略的peer分组，避免重复构造相同的报文。

### 2. Coalescing

在一定时间窗口内收集多个路由更新，批量发送以提高效率。

### 3. Write Quantization

限制每次I/O操作发送的报文数量，防止长时间阻塞。

## 总结

FRR中BGP UPDATE报文的发送和构造是一个复杂的多阶段过程，涉及路由广告管理、Update Group优化、报文构造和I/O处理等多个层次。通过Update Group机制和各种优化策略，实现了高效的BGP报文处理。核心流程可以概括为：

1. **路由加入广告队列** → 
2. **Update Group处理** → 
3. **报文构造** (`subgroup_update_packet()`) → 
4. **触发发送** (`subgroup_trigger_write()`) → 
5. **报文生成** (`bgp_generate_updgrp_packets()`) → 
6. **I/O写入** (`bgp_write()`)

这种分层设计既保证了BGP协议的正确实现，又通过各种优化机制提高了性能和可扩展性。
