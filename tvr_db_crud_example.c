/*
 * TVR Database CRUD Operations Example
 * 
 * This file demonstrates how to directly perform CRUD operations
 * on the tvr_db structure.
 */

#include "tvr_db_crud_example.h"
#include "tvr_db.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Helper function to get current timestamp
static uint64_t get_current_timestamp(void) {
    return (uint64_t)time(NULL);
}

// Helper function to create IPv6 address from string
static void create_ipv6_addr(struct in6_addr *addr, const char *str) {
    inet_pton(AF_INET6, str, addr);
}

/* ========== CREATE OPERATIONS ========== */

/**
 * Example 1: Create and insert a Node NLRI
 */
bool tvr_db_add_node(struct tvr_db *db, uint64_t node_id, 
                     uint8_t spf_status, uint64_t seq_num) {
    struct tvr_nlri nlri;
    
    // 构建节点NLRI
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = node_id;
    nlri.u.node_nlri.time_stamp = get_current_timestamp();
    nlri.u.node_nlri.attr.spf_status = spf_status;
    nlri.u.node_nlri.attr.seq_num = seq_num;
    
    // 插入数据库（false表示插入，不是删除）
    return tvr_db_process(db, &nlri, false);
}

/**
 * Example 2: Create and insert a Link NLRI
 */
bool tvr_db_add_link(struct tvr_db *db, uint64_t local_node, uint64_t remote_node,
                     const char *link_addr_str, uint32_t igp_metric, 
                     uint8_t spf_status, uint64_t seq_num) {
    struct tvr_nlri nlri;
    
    // 构建链路NLRI
    nlri.type = LINK;
    nlri.u.link_nlri.local_node = local_node;
    nlri.u.link_nlri.remote_node = remote_node;
    create_ipv6_addr(&nlri.u.link_nlri.link_addr, link_addr_str);
    nlri.u.link_nlri.time_stamp = get_current_timestamp();
    nlri.u.link_nlri.attr.igp_metric = igp_metric;
    nlri.u.link_nlri.attr.spf_status = spf_status;
    nlri.u.link_nlri.attr.seq_num = seq_num;
    
    return tvr_db_process(db, &nlri, false);
}

/**
 * Example 3: Create and insert a Prefix NLRI
 */
bool tvr_db_add_prefix(struct tvr_db *db, uint64_t local_node, 
                       const char *prefix_str, uint8_t prefixlen,
                       uint8_t spf_status, uint64_t seq_num) {
    struct tvr_nlri nlri;
    
    // 构建前缀NLRI
    nlri.type = PREFIX;
    nlri.u.prefix_nlri.local_node = local_node;
    nlri.u.prefix_nlri.prefixlen = prefixlen;
    create_ipv6_addr(&nlri.u.prefix_nlri.prefix, prefix_str);
    nlri.u.prefix_nlri.time_stamp = get_current_timestamp();
    nlri.u.prefix_nlri.attr.spf_status = spf_status;
    nlri.u.prefix_nlri.attr.seq_num = seq_num;
    
    return tvr_db_process(db, &nlri, false);
}

/* ========== READ OPERATIONS ========== */

/**
 * Example 4: Find a specific Node NLRI
 */
struct tvr_node_nlri *tvr_db_find_node(struct tvr_db *db, uint64_t node_id, 
                                        uint64_t time_stamp) {
    struct tvr_node_nlri key;
    
    // 构建查找键
    key.local_node = node_id;
    key.time_stamp = time_stamp;
    
    // 在红黑树中查找
    return nnlri_rb_find(&db->nnlri_rb_root, &key);
}

/**
 * Example 5: Find the latest Node NLRI for a specific node
 */
struct tvr_node_nlri *tvr_db_find_latest_node(struct tvr_db *db, uint64_t node_id) {
    struct tvr_node_nlri key;
    
    // 构建查找键：使用最大时间戳
    key.local_node = node_id;
    key.time_stamp = UINT64_MAX;
    
    // 查找小于该键的最大元素（即最新记录）
    return nnlri_rb_find_lt(&db->nnlri_rb_root, &key);
}

/**
 * Example 6: Find all Link NLRIs for a specific local node
 */
void tvr_db_find_node_links(struct tvr_db *db, uint64_t local_node,
                            void (*callback)(struct tvr_link_nlri *link, void *arg),
                            void *callback_arg) {
    struct tvr_link_nlri *cur;
    
    // 遍历所有链路NLRI
    frr_each(lnlri_rb, &db->lnlri_rb_root, cur) {
        if (cur->local_node == local_node) {
            callback(cur, callback_arg);
        }
    }
}

/**
 * Example 7: Find Link NLRI within time range
 */
void tvr_db_find_links_in_time_range(struct tvr_db *db, uint64_t local_node,
                                     uint64_t remote_node, const char *link_addr_str,
                                     uint64_t start_time, uint64_t end_time,
                                     void (*callback)(struct tvr_link_nlri *link, void *arg),
                                     void *callback_arg) {
    struct tvr_link_nlri key_start, key_end;
    struct tvr_link_nlri *cur, *start_link;
    
    // 构建查找范围
    key_start.local_node = local_node;
    key_start.remote_node = remote_node;
    create_ipv6_addr(&key_start.link_addr, link_addr_str);
    key_start.time_stamp = start_time;
    
    key_end = key_start;
    key_end.time_stamp = end_time;
    
    // 找到起始位置
    start_link = lnlri_rb_find_gteq(&db->lnlri_rb_root, &key_start);
    if (!start_link) return;
    
    // 从起始位置遍历到结束位置
    for (cur = start_link; cur; cur = lnlri_rb_next(&db->lnlri_rb_root, cur)) {
        // 检查是否超出范围
        if (cur->local_node != local_node || 
            cur->remote_node != remote_node ||
            memcmp(&cur->link_addr, &key_start.link_addr, sizeof(struct in6_addr)) != 0 ||
            cur->time_stamp > end_time) {
            break;
        }
        
        callback(cur, callback_arg);
    }
}

/* ========== UPDATE OPERATIONS ========== */

/**
 * Example 8: Update Node NLRI (higher sequence number)
 */
bool tvr_db_update_node(struct tvr_db *db, uint64_t node_id, 
                        uint8_t new_spf_status, uint64_t new_seq_num) {
    struct tvr_nlri nlri;
    
    // 构建更新的节点NLRI（序列号必须更大）
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = node_id;
    nlri.u.node_nlri.time_stamp = get_current_timestamp();
    nlri.u.node_nlri.attr.spf_status = new_spf_status;
    nlri.u.node_nlri.attr.seq_num = new_seq_num;
    
    // 处理更新（内部会检查序列号）
    return tvr_db_process(db, &nlri, false);
}

/**
 * Example 9: Batch update multiple Link NLRIs
 */
bool tvr_db_batch_update_links(struct tvr_db *db, 
                               struct tvr_link_nlri *links, 
                               size_t count) {
    struct tvr_nlri nlri;
    bool all_success = true;
    
    for (size_t i = 0; i < count; i++) {
        nlri.type = LINK;
        nlri.u.link_nlri = links[i];
        nlri.u.link_nlri.time_stamp = get_current_timestamp(); // 更新时间戳
        
        if (!tvr_db_process(db, &nlri, false)) {
            all_success = false;
            // 可以选择继续处理其他记录或者中止
        }
    }
    
    return all_success;
}

/* ========== DELETE OPERATIONS ========== */

/**
 * Example 10: Delete a specific NLRI
 */
bool tvr_db_delete_node(struct tvr_db *db, uint64_t node_id, uint64_t time_stamp) {
    struct tvr_nlri nlri;
    
    // 构建要删除的节点NLRI
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = node_id;
    nlri.u.node_nlri.time_stamp = time_stamp;
    // attr 字段在删除时不重要
    
    // 删除操作（true表示删除）
    return tvr_db_process(db, &nlri, true);
}

/**
 * Example 11: Delete all NLRIs for a specific node
 */
size_t tvr_db_delete_all_node_nlris(struct tvr_db *db, uint64_t node_id) {
    struct tvr_node_nlri *cur;
    size_t deleted_count = 0;
    
    // 安全遍历并删除
    frr_each_safe(nnlri_rb, &db->nnlri_rb_root, cur) {
        if (cur->local_node == node_id) {
            nnlri_rb_del(&db->nnlri_rb_root, cur);
            // 注意：这里需要手动释放内存
            XFREE(MTYPE_TVR_DB, cur);
            deleted_count++;
        }
    }
    
    return deleted_count;
}

/**
 * Example 12: Delete old records (aging)
 */
size_t tvr_db_cleanup_old_records(struct tvr_db *db, uint64_t cutoff_time) {
    // 使用内置的老化功能
    return tvr_db_aging(db, cutoff_time);
}

/* ========== UTILITY FUNCTIONS ========== */

/**
 * Example 13: Count records by type
 */
void tvr_db_get_statistics(struct tvr_db *db, 
                          size_t *node_count, 
                          size_t *link_count, 
                          size_t *prefix_count) {
    if (node_count) *node_count = nnlri_rb_count(&db->nnlri_rb_root);
    if (link_count) *link_count = lnlri_rb_count(&db->lnlri_rb_root);
    if (prefix_count) *prefix_count = pnlri_rb_count(&db->pnlri_rb_root);
}

/**
 * Example 14: Check if database is empty
 */
bool tvr_db_is_empty(struct tvr_db *db) {
    return (nnlri_rb_count(&db->nnlri_rb_root) == 0 &&
            lnlri_rb_count(&db->lnlri_rb_root) == 0 &&
            pnlri_rb_count(&db->pnlri_rb_root) == 0);
}

/**
 * Example 15: Print callback function for debugging
 */
static void print_link_nlri(struct tvr_link_nlri *link, void *arg) {
    char addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &link->link_addr, addr_str, sizeof(addr_str));
    
    printf("Link: %llu -> %llu via %s, metric=%u, status=%u, seq=%llu, time=%llu\n",
           (unsigned long long)link->local_node, (unsigned long long)link->remote_node, addr_str,
           link->attr.igp_metric, link->attr.spf_status, 
           (unsigned long long)link->attr.seq_num, (unsigned long long)link->time_stamp);
}

/* ========== USAGE EXAMPLE ========== */

/**
 * Complete example showing CRUD operations
 */
void tvr_db_crud_example(void) {
    struct tvr_db *db;
    size_t node_count, link_count, prefix_count;
    
    // 1. CREATE DATABASE
    db = tvr_db_create();
    if (!db) {
        printf("Failed to create TVR database\n");
        return;
    }
    
    printf("=== TVR Database CRUD Example ===\n\n");
    
    // 2. CREATE OPERATIONS
    printf("1. Adding records...\n");
    
    // 添加节点
    tvr_db_add_node(db, 1001, 0, 1);
    tvr_db_add_node(db, 1002, 0, 1);
    tvr_db_add_node(db, 1003, 0, 1);
    
    // 添加链路
    tvr_db_add_link(db, 1001, 1002, "2001:db8::1", 10, 0, 1);
    tvr_db_add_link(db, 1002, 1003, "2001:db8::2", 15, 0, 1);
    tvr_db_add_link(db, 1001, 1003, "2001:db8::3", 20, 0, 1);
    
    // 添加前缀
    tvr_db_add_prefix(db, 1001, "2001:db8:1000::", 48, 0, 1);
    tvr_db_add_prefix(db, 1002, "2001:db8:2000::", 48, 0, 1);
    
    // 3. READ OPERATIONS
    printf("2. Reading records...\n");
    
    // 获取统计信息
    tvr_db_get_statistics(db, &node_count, &link_count, &prefix_count);
    printf("Database contains: %zu nodes, %zu links, %zu prefixes\n",
           node_count, link_count, prefix_count);
    
    // 查找特定节点的链路
    printf("Links from node 1001:\n");
    tvr_db_find_node_links(db, 1001, print_link_nlri, NULL);
    
    // 4. UPDATE OPERATIONS  
    printf("3. Updating records...\n");
    
    // 更新节点状态
    tvr_db_update_node(db, 1001, 1, 2); // 更高的序列号
    
    // 5. DELETE OPERATIONS
    printf("4. Deleting records...\n");
    
    // 删除过期记录（假设30秒前的记录过期）
    uint64_t cutoff = get_current_timestamp() - 30;
    size_t aged_count = tvr_db_cleanup_old_records(db, cutoff);
    printf("Aged out %zu old records\n", aged_count);
    
    // 6. CLEANUP
    printf("5. Final statistics...\n");
    tvr_db_get_statistics(db, &node_count, &link_count, &prefix_count);
    printf("Final: %zu nodes, %zu links, %zu prefixes\n",
           node_count, link_count, prefix_count);
    
    // 清理数据库
    tvr_db_destroy(&db);
    printf("Database cleaned up.\n");
}
