/*
 * TVR Database Simple CRUD Examples
 * 
 * This file shows practical examples of how to use tvr_db
 * in your BGP or routing code.
 */

#include "tvr_db.h"
#include <stdio.h>
#include <time.h>

/* Function prototypes */
static void example_basic_crud(void);
static void example_time_based_query(void);
static void example_batch_operations(void);
static void example_error_handling(void);

/* ========== 基础CRUD操作示例 ========== */

/**
 * 示例1: 创建数据库并添加基本记录
 */
static void example_basic_crud(void) {
    struct tvr_db *db;
    struct tvr_nlri nlri;
    bool result;
    
    printf("=== Basic CRUD Example ===\n");
    
    // 1. 创建数据库
    db = tvr_db_create();
    if (!db) {
        printf("Failed to create database\n");
        return;
    }
    
    // 2. 添加节点记录
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = 1001;
    nlri.u.node_nlri.time_stamp = (uint64_t)time(NULL);
    nlri.u.node_nlri.attr.spf_status = 0;  // TVR_DEFAULT_STATUS
    nlri.u.node_nlri.attr.seq_num = 1;
    
    result = tvr_db_process(db, &nlri, false);  // false = 添加/更新
    printf("Add node 1001: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 3. 添加链路记录  
    nlri.type = LINK;
    nlri.u.link_nlri.local_node = 1001;
    nlri.u.link_nlri.remote_node = 1002;
    inet_pton(AF_INET6, "2001:db8::1", &nlri.u.link_nlri.link_addr);
    nlri.u.link_nlri.time_stamp = (uint64_t)time(NULL);
    nlri.u.link_nlri.attr.igp_metric = 10;
    nlri.u.link_nlri.attr.spf_status = 0;
    nlri.u.link_nlri.attr.seq_num = 1;
    
    result = tvr_db_process(db, &nlri, false);
    printf("Add link 1001->1002: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 4. 查找记录
    struct tvr_node_nlri *found_node = nnlri_rb_find(&db->nnlri_rb_root, &nlri.u.node_nlri);
    printf("Find node 1001: %s\n", found_node ? "FOUND" : "NOT FOUND");
    
    // 5. 更新记录 (更高序列号)
    nlri.u.node_nlri.attr.seq_num = 2;
    nlri.u.node_nlri.attr.spf_status = 1;  // 改变状态
    result = tvr_db_process(db, &nlri, false);
    printf("Update node 1001: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 6. 删除记录
    result = tvr_db_process(db, &nlri, true);  // true = 删除
    printf("Delete node 1001: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 7. 清理
    tvr_db_destroy(&db);
    printf("Database destroyed\n\n");
}

/**
 * 示例2: 时间范围查询
 */
static void example_time_based_query(void) {
    struct tvr_db *db;
    struct tvr_nlri nlri;
    uint64_t base_time = (uint64_t)time(NULL);
    
    printf("=== Time-based Query Example ===\n");
    
    db = tvr_db_create();
    
    // 添加同一链路在不同时间的记录
    nlri.type = LINK;
    nlri.u.link_nlri.local_node = 2001;
    nlri.u.link_nlri.remote_node = 2002;
    inet_pton(AF_INET6, "2001:db8::100", &nlri.u.link_nlri.link_addr);
    nlri.u.link_nlri.attr.spf_status = 0;
    
    // 时间点1: metric = 10
    nlri.u.link_nlri.time_stamp = base_time;
    nlri.u.link_nlri.attr.igp_metric = 10;
    nlri.u.link_nlri.attr.seq_num = 1;
    tvr_db_process(db, &nlri, false);
    
    // 时间点2: metric = 20 (链路质量下降)
    nlri.u.link_nlri.time_stamp = base_time + 10;
    nlri.u.link_nlri.attr.igp_metric = 20;
    nlri.u.link_nlri.attr.seq_num = 2;
    tvr_db_process(db, &nlri, false);
    
    // 时间点3: metric = 15 (链路质量恢复)
    nlri.u.link_nlri.time_stamp = base_time + 20;
    nlri.u.link_nlri.attr.igp_metric = 15;
    nlri.u.link_nlri.attr.seq_num = 3;
    tvr_db_process(db, &nlri, false);
    
    // 查询特定时间点的状态
    struct tvr_link_nlri key;
    key.local_node = 2001;
    key.remote_node = 2002;
    key.link_addr = nlri.u.link_nlri.link_addr;
    key.time_stamp = base_time + 15;  // 查询时间点2和3之间
    
    // 找到小于等于查询时间的最新记录
    struct tvr_link_nlri *result = lnlri_rb_find_lt(&db->lnlri_rb_root, &key);
    if (result) {
        printf("Link state at time %llu: metric=%u\n", 
               (unsigned long long)key.time_stamp, result->attr.igp_metric);
    }
    
    // 遍历所有历史记录
    printf("Link history:\n");
    struct tvr_link_nlri *cur;
    frr_each(lnlri_rb, &db->lnlri_rb_root, cur) {
        if (cur->local_node == 2001 && cur->remote_node == 2002) {
            printf("  Time %llu: metric=%u\n", 
                   (unsigned long long)cur->time_stamp, cur->attr.igp_metric);
        }
    }
    
    tvr_db_destroy(&db);
    printf("\n");
}

/**
 * 示例3: 批量操作和统计
 */
static void example_batch_operations(void) {
    struct tvr_db *db;
    struct tvr_nlri nlri;
    uint64_t timestamp = (uint64_t)time(NULL);
    
    printf("=== Batch Operations Example ===\n");
    
    db = tvr_db_create();
    
    // 批量添加节点
    nlri.type = NODE;
    nlri.u.node_nlri.time_stamp = timestamp;
    nlri.u.node_nlri.attr.spf_status = 0;
    nlri.u.node_nlri.attr.seq_num = 1;
    
    for (uint64_t i = 3001; i <= 3010; i++) {
        nlri.u.node_nlri.local_node = i;
        tvr_db_process(db, &nlri, false);
    }
    
    // 批量添加链路 (创建链状拓扑)
    nlri.type = LINK;
    nlri.u.link_nlri.time_stamp = timestamp;
    nlri.u.link_nlri.attr.igp_metric = 10;
    nlri.u.link_nlri.attr.spf_status = 0;
    nlri.u.link_nlri.attr.seq_num = 1;
    
    for (uint64_t i = 3001; i < 3010; i++) {
        nlri.u.link_nlri.local_node = i;
        nlri.u.link_nlri.remote_node = i + 1;
        
        // 生成不同的链路地址
        char addr_str[INET6_ADDRSTRLEN];
        snprintf(addr_str, sizeof(addr_str), "2001:db8::%llx", (unsigned long long)i);
        inet_pton(AF_INET6, addr_str, &nlri.u.link_nlri.link_addr);
        
        tvr_db_process(db, &nlri, false);
    }
    
    // 统计信息
    printf("Database statistics:\n");
    printf("  Nodes: %zu\n", nnlri_rb_count(&db->nnlri_rb_root));
    printf("  Links: %zu\n", lnlri_rb_count(&db->lnlri_rb_root));
    printf("  Prefixes: %zu\n", pnlri_rb_count(&db->pnlri_rb_root));
    
    // 查找特定节点的所有出链路
    printf("Links from node 3005:\n");
    struct tvr_link_nlri *link;
    frr_each(lnlri_rb, &db->lnlri_rb_root, link) {
        if (link->local_node == 3005) {
            char addr_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &link->link_addr, addr_str, sizeof(addr_str));
            printf("  -> %llu via %s (metric=%u)\n", 
                   (unsigned long long)link->remote_node, addr_str, link->attr.igp_metric);
        }
    }
    
    // 老化清理 (删除30秒前的记录)
    size_t aged_count = tvr_db_aging(db, timestamp - 30);
    printf("Aged %zu old records\n", aged_count);
    
    tvr_db_destroy(&db);
    printf("\n");
}

/**
 * 示例4: 错误处理和边界情况
 */
static void example_error_handling(void) {
    struct tvr_db *db;
    struct tvr_nlri nlri;
    bool result;
    
    printf("=== Error Handling Example ===\n");
    
    db = tvr_db_create();
    
    // 1. 添加记录
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = 4001;
    nlri.u.node_nlri.time_stamp = (uint64_t)time(NULL);
    nlri.u.node_nlri.attr.spf_status = 0;
    nlri.u.node_nlri.attr.seq_num = 10;
    
    result = tvr_db_process(db, &nlri, false);
    printf("Add node with seq=10: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 2. 尝试用更小的序列号更新 (应该失败)
    nlri.u.node_nlri.attr.seq_num = 5;  // 更小的序列号
    result = tvr_db_process(db, &nlri, false);
    printf("Update with smaller seq=5: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 3. 用更大的序列号更新 (应该成功)
    nlri.u.node_nlri.attr.seq_num = 15;
    result = tvr_db_process(db, &nlri, false);
    printf("Update with larger seq=15: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 4. 删除不存在的记录 (应该失败)
    nlri.u.node_nlri.local_node = 9999;  // 不存在的节点
    result = tvr_db_process(db, &nlri, true);
    printf("Delete non-existent node: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 5. 删除存在的记录 (应该成功)
    nlri.u.node_nlri.local_node = 4001;
    result = tvr_db_process(db, &nlri, true);
    printf("Delete existing node: %s\n", result ? "SUCCESS" : "FAILED");
    
    // 6. 再次删除同一记录 (应该失败)
    result = tvr_db_process(db, &nlri, true);
    printf("Delete already deleted node: %s\n", result ? "SUCCESS" : "FAILED");
    
    tvr_db_destroy(&db);
    printf("\n");
}

/**
 * 主函数 - 运行所有示例
 */
int main(void) {
    printf("TVR Database CRUD Examples\n");
    printf("==========================\n\n");
    
    example_basic_crud();
    example_time_based_query();
    example_batch_operations();
    example_error_handling();
    
    printf("All examples completed successfully!\n");
    return 0;
}
