# TVR 数据库实例差异修复指南

## 问题总结

发现在调试过程中，`sg.db` (sharpd 进程) 和 `bgp->db` (BGP 进程) 指向了不同的 TVR 数据库实例。经过代码分析，确认问题根源是：

**BGP 和 Sharpd 是不同的进程，每个进程都有独立的内存空间，因此 `g_tvr_db_instance` 静态变量在不同进程中是独立的。**

## 解决方案实现

### 方案 1：临时验证修复 (快速测试)

如果你想快速验证修复效果，可以在调试代码中添加进程 ID 和实例地址：

```c
// 在 bgp_fsm.c 的调试输出中添加：
sprintf(debug_buf,
    "[%s] BGP add self to tvr_db, pkt_id: %llu, db_address: %p, static_db_address: %p, PID: %d\n",
    bgp_router_id_str, pkt_id, (void*) peer->bgp->db, tvr_db_get_instance(), getpid());

// 在 sharpd/sharp_vty.c 的 tvrdb_show 命令中添加：
snprintf(debug_buf, sizeof(debug_buf), 
    "tvrdb_show: db=%p, static db=%p, PID: %d\n", 
    sg.db, tvr_db_get_instance(), getpid());
```

这样你会看到：
- 不同的 PID 说明是不同进程
- 相同 PID 内的地址应该相同
- 不同 PID 间的地址可能不同（这是正常的）

### 方案 2：共享内存实现 (推荐生产环境)

我已经创建了基于共享内存的 TVR 数据库实现，文件包括：
- `lib/tvr_db_shared.h` - 共享内存 API 声明
- `lib/tvr_db_shared.c` - 共享内存实现

#### 启用共享内存版本

1. **编译时启用**：
   在 `lib/tvr_db.h` 中已添加：
   ```c
   #ifndef USE_SHARED_TVR_DB
   #define USE_SHARED_TVR_DB 1
   #endif
   ```

2. **更新 BGP 进程初始化**：
   在 `bgpd/bgpd.c` 中的 `bgp_create` 函数中添加进程注册：
   ```c
   bgp->db = tvr_db_get_instance();
   
   // 注册当前进程
   tvr_db_register_process("bgpd");
   ```

3. **更新 Sharpd 进程初始化**：
   在 `sharpd/sharp_main.c` 中的 `sharp_global_init` 函数中：
   ```c
   static void sharp_global_init(void)
   {
       memset(&sg, 0, sizeof(sg));
       sg.nhs = list_new();
       sg.nhs->del = (void (*)(void *))sharp_nh_tracker_free;
       sg.ted = NULL;
       sg.srv6_locators = list_new();
       
       // 使用共享实例
       sg.db = tvr_db_get_instance();
       
       // 注册当前进程
       tvr_db_register_process("sharpd");
   }
   ```

4. **更新进程退出清理**：
   在各进程退出时调用：
   ```c
   tvr_db_unregister_process();
   tvr_db_release_shared_instance();
   ```

#### 验证共享内存工作正常

添加调试命令来验证：

```c
// 在 sharpd/sharp_vty.c 中添加新的 VTY 命令
DEFPY(sharp_tvrdb_debug,
      sharp_tvrdb_debug_cmd,
      "tvrdb debug",
      TVR_DB_STR 
      "Debug information\n")
{
    tvr_db_show_shared_debug(vty);
    return CMD_SUCCESS;
}
```

这个命令会显示：
- 共享内存地址
- 引用计数
- 创建者进程信息
- 当前访问进程信息

### 方案 3：进程间通信 (IPC) - 高级方案

如果共享内存方案不适合，可以实现基于消息队列的 IPC：

```c
// lib/tvr_db_ipc.h
struct tvr_ipc_message {
    enum tvr_op_type operation;
    union {
        struct tvr_node_nlri node;
        struct tvr_link_nlri link; 
        struct tvr_prefix_nlri prefix;
    } data;
    bool is_delete;
    pid_t sender_pid;
};

// 主数据库服务器（可以在 zebra 进程中运行）
void tvr_db_ipc_server_start(void);

// 客户端接口
bool tvr_db_ipc_process(struct tvr_nlri *nlri, bool is_delete);
```

## 构建系统更新

需要在相关的 Makefile.am 中添加新文件：

```makefile
# lib/Makefile.am
lib_LTLIBRARIES += lib/libfrr.la
lib_libfrr_la_SOURCES += \
    lib/tvr_db.c \
    lib/tvr_db_shared.c \
    # ... 其他文件

# 添加必要的链接库
lib_libfrr_la_LIBADD += -lrt -lpthread
```

## 测试验证

### 1. 单元测试

```c
// test_shared_tvr_db.c
void test_cross_process_singleton(void) {
    // 创建子进程
    pid_t pid = fork();
    
    if (pid == 0) {
        // 子进程
        struct tvr_db *child_db = tvr_db_get_instance();
        printf("Child process (PID %d): DB address = %p\n", 
               getpid(), (void*)child_db);
        
        // 添加一些数据
        struct tvr_nlri nlri;
        nlri.type = NODE;
        nlri.u.node_nlri.local_node = 0x01010101;
        tvr_db_process(child_db, &nlri, false);
        
        exit(0);
    } else {
        // 父进程
        sleep(1); // 等待子进程完成
        
        struct tvr_db *parent_db = tvr_db_get_instance();
        printf("Parent process (PID %d): DB address = %p\n", 
               getpid(), (void*)parent_db);
        
        // 检查子进程添加的数据是否可见
        struct tvr_db_shared_stats stats;
        tvr_db_get_shared_stats(&stats);
        printf("Reference count: %d\n", stats.ref_count);
        
        wait(NULL);
    }
}
```

### 2. 运行时验证

```bash
# 启动 BGP 进程
sudo bgpd -f /etc/frr/bgpd.conf

# 启动 Sharpd 进程  
sudo sharpd -f /etc/frr/sharpd.conf

# 在 vtysh 中验证
vtysh -c "show tvr-db"
vtysh -c "sharp tvrdb debug"
```

### 3. 预期结果

使用共享内存方案后，你应该看到：
- 不同进程中的 `tvr_db_get_instance()` 返回相同的逻辑地址
- 一个进程中添加的数据在另一个进程中可见
- 引用计数正确管理进程生命周期

## 注意事项

1. **线程安全**：共享内存实现使用 pthread 互斥锁保护并发访问
2. **错误处理**：包含完整的错误处理和日志记录
3. **资源清理**：进程退出时自动清理资源
4. **向后兼容**：可以通过宏开关在传统模式和共享模式间切换

## 下一步

1. 编译并测试共享内存实现
2. 在实际环境中验证跨进程数据一致性
3. 监控性能影响和内存使用
4. 根据需要调整共享内存大小和锁粒度

这样就能确保 `sg.db` 和 `bgp->db` 在逻辑上指向同一个共享的 TVR 数据库实例。
