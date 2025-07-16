# BGP FSM Change Status Function Flow Chart

## 函数执行流程图

```
bgp_fsm_change_status(connection, status)
    |
    ├─ 1. 初始化变量
    │   ├─ peer = connection->peer
    │   ├─ bgp = peer->bgp
    │   └─ peer_count = bgp->established_peers
    │
    ├─ 2. 更新established_peers计数器
    │   ├─ if (status == Established)
    │   │   └─ bgp->established_peers++
    │   └─ else if (peer_established(connection) && status != Established)
    │       └─ bgp->established_peers--
    │
    ├─ 3. 输出调试日志
    │   └─ 记录状态变更信息
    │
    ├─ 4. Router ID管理
    │   └─ if (peer_count != bgp->established_peers && bgp->established_peers == 0)
    │       └─ bgp_router_id_zebra_bump(bgp->vrf_id, NULL)
    │
    ├─ 5. 路由清理处理
    │   └─ if (status >= Clearing)
    │       ├─ bgp_clear_route_all(peer)
    │       └─ if (!work_queue_is_scheduled(peer->clear_node_queue) && status != Deleted)
    │           └─ BGP_EVENT_ADD(connection, Clearing_Completed)
    │
    ├─ 6. 更新状态
    │   ├─ connection->ostatus = connection->status  // 保存旧状态
    │   ├─ connection->status = status               // 设置新状态
    │   └─ peer->rtt_keepalive_rcv = 0              // 重置计数器
    │
    ├─ 7. 向后状态转换处理
    │   └─ if (connection->ostatus == Established && connection->status != Established)
    │       ├─ 遍历BGP实例中的所有peer
    │       ├─ 对每个established peer发送通知
    │       ├─ 写入调试信息到文件
    │       └─ hook_call(peer_backward_transition, peer)
    │
    ├─ 8. 事件记录
    │   ├─ peer->last_major_event = peer->cur_event
    │   └─ hook_call(peer_status_changed, peer)
    │
    ├─ 9. Established状态特殊处理
    │   └─ if (status == Established)
    │       └─ UNSET_FLAG(peer->sflags, PEER_STATUS_ACCEPT_PEER)
    │
    ├─ 10. Max-Med处理
    │    └─ if (status == Established)
    │        ├─ if (bgp_maxmed_onstartup_configured && applicable)
    │        │   └─ bgp_maxmed_onstartup_process_status_change(peer)
    │        └─ else
    │            └─ peer->bgp->maxmed_onstartup_over = 1
    │
    ├─ 11. Update-Delay处理
    │    └─ if (bgp_update_delay_configured && applicable)
    │        └─ bgp_update_delay_process_status_change(peer)
    │
    └─ 12. 最终调试日志
        └─ 记录状态转换信息 (从ostatus到status)
```

## 关键决策点分析

### 决策点1: established_peers计数器更新
```
条件: status == Established
动作: bgp->established_peers++

条件: peer_established(connection) && status != Established  
动作: bgp->established_peers--
```

### 决策点2: Router ID重新设置
```
条件: (peer_count != bgp->established_peers) && (bgp->established_peers == 0)
动作: bgp_router_id_zebra_bump()
说明: 当所有BGP连接都断开时，重新从Zebra获取Router ID
```

### 决策点3: 路由清理
```
条件: status >= Clearing
动作: 
  1. bgp_clear_route_all(peer)  // 清理所有路由
  2. 如果没有队列任务且状态不是Deleted，生成Clearing_Completed事件
```

### 决策点4: 向后状态转换检测
```
条件: ostatus == Established && status != Established
动作:
  1. 遍历所有其他established peer
  2. 向它们发送BGP通知消息
  3. 调用向后转换钩子
```

### 决策点5: Max-Med启动处理
```
条件: status == Established && bgp_maxmed_onstartup_configured && applicable
动作: bgp_maxmed_onstartup_process_status_change(peer)
否则: maxmed_onstartup_over = 1
```

### 决策点6: Update-Delay处理
```
条件: status == Established && bgp_update_delay_configured && applicable
动作: bgp_update_delay_process_status_change(peer)
```

## 状态转换矩阵

| 旧状态 \ 新状态 | Idle | Connect | Active | OpenSent | OpenConfirm | Established | Clearing | Deleted |
|----------------|------|---------|--------|----------|-------------|-------------|----------|---------|
| Idle           | -    | ✓       | ✓      | ✗        | ✗           | ✗           | ✓        | ✓       |
| Connect        | ✓    | -       | ✓      | ✓        | ✗           | ✗           | ✓        | ✓       |
| Active         | ✓    | ✓       | -      | ✓        | ✗           | ✗           | ✓        | ✓       |
| OpenSent       | ✓    | ✓       | ✓      | -        | ✓           | ✗           | ✓        | ✓       |
| OpenConfirm    | ✓    | ✓       | ✓      | ✓        | -           | ✓           | ✓        | ✓       |
| Established    | ✓    | ✓       | ✓      | ✓        | ✓           | -           | ✓        | ✓       |
| Clearing       | ✓    | ✗       | ✗      | ✗        | ✗           | ✗           | -        | ✓       |
| Deleted        | ✗    | ✗       | ✗      | ✗        | ✗           | ✗           | ✗        | -       |

## 副作用处理总览

1. **计数器维护**: established_peers的正确更新
2. **资源清理**: 路由表、队列的清理
3. **通知机制**: 钩子函数调用、事件生成
4. **调试支持**: 详细的日志记录
5. **特性集成**: Max-Med、Update-Delay等特性的处理
6. **自定义逻辑**: 向其他peer发送通知的特殊处理

## 函数的设计原则

1. **原子性**: 状态变更操作保证原子性
2. **一致性**: 相关数据结构的一致性维护
3. **可观测性**: 完善的调试和日志支持
4. **扩展性**: 通过钩子机制支持扩展
5. **健壮性**: 异常情况的处理和恢复
