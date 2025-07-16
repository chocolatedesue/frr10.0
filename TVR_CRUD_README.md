# TVR Database CRUD操作指南

本目录包含了TVR数据库CRUD操作的完整示例代码，展示如何在实际项目中使用`tvr_db`结构。

## 文件说明

- `tvr_db_crud_example.h/.c` - 完整的CRUD操作封装函数
- `tvr_db_simple_examples.c` - 简单易懂的使用示例
- `README.md` - 本文档

## 核心CRUD操作

### 1. Create (创建/插入)

```c
// 创建数据库
struct tvr_db *db = tvr_db_create();

// 插入节点记录
struct tvr_nlri nlri;
nlri.type = NODE;
nlri.u.node_nlri.local_node = 1001;
nlri.u.node_nlri.time_stamp = (uint64_t)time(NULL);
nlri.u.node_nlri.attr.spf_status = 0;
nlri.u.node_nlri.attr.seq_num = 1;

bool success = tvr_db_process(db, &nlri, false);  // false表示插入
```

### 2. Read (查询)

```c
// 精确查找
struct tvr_node_nlri key = {.local_node = 1001, .time_stamp = timestamp};
struct tvr_node_nlri *result = nnlri_rb_find(&db->nnlri_rb_root, &key);

// 范围查找 - 找到小于指定时间的最新记录
key.time_stamp = target_time + 1;
result = nnlri_rb_find_lt(&db->nnlri_rb_root, &key);

// 遍历所有记录
struct tvr_link_nlri *link;
frr_each(lnlri_rb, &db->lnlri_rb_root, link) {
    // 处理每个链路记录
    printf("Link: %llu -> %llu\n", link->local_node, link->remote_node);
}
```

### 3. Update (更新)

```c
// 更新记录 (需要更高的序列号)
nlri.u.node_nlri.attr.seq_num = 2;  // 序列号必须更大
nlri.u.node_nlri.attr.spf_status = 1;  // 新状态

bool success = tvr_db_process(db, &nlri, false);  // 内部会检查序列号
```

### 4. Delete (删除)

```c
// 删除特定记录
bool success = tvr_db_process(db, &nlri, true);  // true表示删除

// 批量删除过期记录
size_t aged_count = tvr_db_aging(db, cutoff_timestamp);

// 完全清空数据库
tvr_db_destroy(&db);
```

## 红黑树操作详解

TVR数据库使用红黑树存储所有数据，提供以下操作：

### 基本操作
- `xxx_rb_add()` - 插入节点 O(log n)
- `xxx_rb_find()` - 精确查找 O(log n)
- `xxx_rb_del()` - 删除节点 O(log n)
- `xxx_rb_count()` - 获取节点数量 O(1)

### 范围查询
- `xxx_rb_find_lt()` - 查找小于指定键的最大节点
- `xxx_rb_find_gteq()` - 查找大于等于指定键的最小节点
- `xxx_rb_first()` - 获取最小节点
- `xxx_rb_next()` - 获取下一个节点

### 遍历操作
```c
// 正向遍历
frr_each(lnlri_rb, &db->lnlri_rb_root, item) {
    // 处理item
}

// 安全遍历（可在循环中删除元素）
frr_each_safe(lnlri_rb, &db->lnlri_rb_root, item) {
    if (condition) {
        lnlri_rb_del(&db->lnlri_rb_root, item);
        // 释放内存
    }
}
```

## 时间维度处理

TVR最重要的特性是时间感知，支持：

### 时间窗口查询
```c
// 查找时间范围[t1, t2]内的状态变化
uint64_t t1 = start_time;
uint64_t t2 = end_time;

// 设置查找键
struct tvr_link_nlri key;
key.local_node = target_node;
key.remote_node = target_remote;
key.link_addr = target_addr;
key.time_stamp = t1 + 1;

// 找到t1时刻的有效状态
struct tvr_link_nlri *cur = lnlri_rb_find_lt(&db->lnlri_rb_root, &key);

// 遍历到t2时刻
while (cur && cur->time_stamp <= t2) {
    // 处理状态变化
    cur = lnlri_rb_next(&db->lnlri_rb_root, cur);
}
```

### 历史状态追踪
```c
// 找到某个链路的所有历史记录
key.time_stamp = UINT64_MAX;
struct tvr_link_nlri *last = lnlri_rb_find_lt(&db->lnlri_rb_root, &key);

// 从第一条记录开始遍历
cur = lnlri_rb_first(&db->lnlri_rb_root);
while (cur && cur != last) {
    if (same_link(cur, &target)) {
        printf("Time %llu: metric=%u\n", cur->time_stamp, cur->attr.igp_metric);
    }
    cur = lnlri_rb_next(&db->lnlri_rb_root, cur);
}
```

## 序列号控制

TVR使用序列号实现乐观并发控制：

```c
// 只有新序列号大于现有序列号才能更新
if (existing->attr.seq_num < new_nlri->attr.seq_num) {
    // 删除旧记录，插入新记录
    lnlri_rb_del(root, existing);
    lnlri_rb_add(root, new_record);
} else {
    // 拒绝更新
    return false;
}
```

## 内存管理

- 使用`XCALLOC(MTYPE_TVR_DB, size)`分配内存
- 使用`XFREE(MTYPE_TVR_DB, ptr)`释放内存
- 红黑树节点删除后需要手动释放内存
- 数据库销毁时会自动清理所有记录

## 错误处理

- 所有操作都返回布尔值表示成功/失败
- 序列号过时会导致更新失败
- 删除不存在的记录会返回false
- 空指针和无效参数会被安全处理

## 性能特点

- **时间复杂度**: 所有操作都是O(log n)
- **空间效率**: 红黑树自动平衡，避免退化
- **并发安全**: 通过序列号实现乐观锁
- **内存友好**: 支持老化清理过期数据

## 编译和运行

```bash
# 编译示例
gcc -I./lib tvr_db_simple_examples.c lib/tvr_db.c -o tvr_examples

# 运行示例
./tvr_examples
```

## 实际应用场景

1. **BGP路由表**: 存储路径信息的时间变化
2. **网络拓扑**: 跟踪链路状态的历史演化  
3. **流量工程**: 记录带宽和延迟的时间序列
4. **故障检测**: 分析网络故障的时间模式
5. **性能监控**: 存储网络性能指标的历史数据

这些示例展示了TVR数据库在时间敏感网络应用中的强大功能。
