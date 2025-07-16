# BGP FSM Change Status - Simplified Logic

## 核心逻辑伪代码

```c
void bgp_fsm_change_status(struct peer_connection *connection, enum bgp_fsm_status status)
{
    // =================== 1. 初始化阶段 ===================
    struct peer *peer = connection->peer;
    struct bgp *bgp = peer->bgp;
    uint32_t old_peer_count = bgp->established_peers;
    
    // =================== 2. 计数器维护 ===================
    // 维护BGP实例中established peer的数量
    if (status == Established) {
        bgp->established_peers++;
    } else if (peer_is_currently_established(connection) && status != Established) {
        bgp->established_peers--;
    }
    
    // =================== 3. 调试日志 ===================
    log_status_change(peer, status, bgp->established_peers);
    
    // =================== 4. Router ID管理 ===================
    // 当所有BGP连接都断开时，重新设置Router ID
    if (peer_count_changed(old_peer_count, bgp->established_peers) && 
        no_established_peers(bgp)) {
        reset_router_id_from_zebra(bgp);
    }
    
    // =================== 5. 路由清理 ===================
    // 对于Clearing及以上的状态，需要清理路由
    if (status >= Clearing) {
        clear_all_routes(peer);
        
        // 如果没有路由需要清理，直接完成清理过程
        if (no_pending_route_cleanup(peer) && status != Deleted) {
            trigger_clearing_completed_event(connection);
        }
    }
    
    // =================== 6. 状态更新 ===================
    connection->ostatus = connection->status;  // 保存旧状态
    connection->status = status;               // 设置新状态
    peer->rtt_keepalive_rcv = 0;              // 重置keepalive计数器
    
    // =================== 7. 向后转换处理 ===================
    // 特殊处理：从Established向其他状态的转换
    if (transition_from_established_to_other(connection)) {
        // 自定义逻辑：通知其他established peer
        for_each_peer_in_bgp_instance(peer->bgp) {
            if (other_peer_is_established(other_peer)) {
                send_custom_notification_to_peer(other_peer, peer);
                write_debug_info_to_file(other_peer, peer);
            }
        }
        
        // 调用向后转换钩子
        call_backward_transition_hook(peer);
    }
    
    // =================== 8. 事件记录 ===================
    peer->last_major_event = peer->cur_event;
    call_status_changed_hook(peer);
    
    // =================== 9. Established状态特殊处理 ===================
    if (status == Established) {
        clear_accept_peer_flag(peer);
    }
    
    // =================== 10. 特性处理 ===================
    if (status == Established) {
        // Max-Med处理：启动时的MED控制
        if (maxmed_onstartup_is_configured_and_applicable(bgp)) {
            process_maxmed_onstartup_status_change(peer);
        } else {
            mark_maxmed_onstartup_over(bgp);
        }
        
        // Update-Delay处理：延迟路由更新
        if (update_delay_is_configured_and_applicable(bgp)) {
            process_update_delay_status_change(peer);
        }
    }
    
    // =================== 11. 最终调试日志 ===================
    log_final_status_transition(peer, connection->ostatus, connection->status);
}
```

## 关键函数说明

### 核心状态处理函数
```c
// 检查peer是否当前处于established状态
bool peer_is_currently_established(struct peer_connection *connection) {
    return connection->status == Established;
}

// 检查是否没有established peer
bool no_established_peers(struct bgp *bgp) {
    return bgp->established_peers == 0;
}

// 检查是否从established状态向其他状态转换
bool transition_from_established_to_other(struct peer_connection *connection) {
    return (connection->ostatus == Established && 
            connection->status != Established);
}
```

### 资源管理函数
```c
// 清理所有路由
void clear_all_routes(struct peer *peer) {
    bgp_clear_route_all(peer);
}

// 检查是否还有路由清理任务待处理
bool no_pending_route_cleanup(struct peer *peer) {
    return !work_queue_is_scheduled(peer->clear_node_queue);
}

// 触发清理完成事件
void trigger_clearing_completed_event(struct peer_connection *connection) {
    BGP_EVENT_ADD(connection, Clearing_Completed);
}
```

### 通知和调试函数
```c
// 向其他peer发送自定义通知
void send_custom_notification_to_peer(struct peer *target_peer, struct peer *source_peer) {
    // 构造并发送BGP通知消息
    // 包含source_peer的状态变更信息
}

// 写入调试信息到文件
void write_debug_info_to_file(struct peer *target_peer, struct peer *source_peer) {
    // 将状态变更信息写入调试文件
    // 格式化输出相关信息
}
```

## 状态转换的关键点

### 1. Established状态的重要性
- 只有Established状态才被计入established_peers
- 从Established状态的转换需要特殊处理
- Established状态会触发Max-Med和Update-Delay处理

### 2. Clearing状态的处理
- 任何大于等于Clearing的状态都需要清理路由
- 清理完成后自动触发Clearing_Completed事件
- 避免peer卡在Clearing状态

### 3. 计数器的一致性
- established_peers计数器必须准确维护
- 用于Router ID管理和特性处理的判断

### 4. 钩子机制的使用
- peer_backward_transition: 向后转换钩子
- peer_status_changed: 状态变更钩子
- 允许其他模块响应状态变更

## 错误处理和边界情况

1. **空指针检查**: 函数假设传入的指针有效
2. **状态一致性**: 通过ostatus保存旧状态，确保转换的原子性
3. **资源清理**: 确保路由清理操作的完整性
4. **计数器溢出**: established_peers的递增递减操作需要保证正确性

## 性能考虑

1. **遍历优化**: 向后转换时遍历所有peer，在大型网络中需要考虑性能
2. **日志开销**: 调试日志的条件判断，避免不必要的字符串操作
3. **钩子调用**: 钩子函数的调用开销，特别是在频繁状态变更时
4. **队列操作**: 工作队列的检查和操作需要考虑并发性
