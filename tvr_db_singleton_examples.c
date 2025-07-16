/*
 * TVR Database Singleton Pattern Usage Examples
 * 
 * This file demonstrates how to use the singleton pattern
 * version of the TVR database.
 */

#include "tvr_db.h"
#include <stdio.h>
#include <assert.h>
#include <time.h>

/* Function prototypes */
static void example_singleton_basic_usage(void);
static void example_singleton_thread_safety_consideration(void);
static void example_singleton_vs_regular_pattern(void);
static void example_singleton_in_routing_context(void);
static void print_singleton_best_practices(void);

/* Helper functions for examples */
static void process_with_regular_db(struct tvr_db *db);
static void process_with_singleton(void);
static void bgp_add_node_info(uint64_t node_id);
static void ospf_add_link_info(uint64_t local, uint64_t remote, uint32_t metric);
static void spf_show_database_stats(void);

/**
 * Example 1: Basic singleton usage
 */
static void example_singleton_basic_usage(void) {
    printf("=== Singleton Basic Usage Example ===\n");
    
    // 检查实例是否存在
    printf("Instance exists initially: %s\n", 
           tvr_db_instance_exists() ? "YES" : "NO");
    
    // 获取单例实例（第一次调用会创建）
    struct tvr_db *db1 = tvr_db_get_instance();
    printf("First call to get_instance: %p\n", (void*)db1);
    printf("Instance exists after first call: %s\n", 
           tvr_db_instance_exists() ? "YES" : "NO");
    
    // 再次获取实例（应该返回相同的指针）
    struct tvr_db *db2 = tvr_db_get_instance();
    printf("Second call to get_instance: %p\n", (void*)db2);
    
    // 验证是同一个实例
    printf("db1 == db2: %s\n", (db1 == db2) ? "YES" : "NO");
    assert(db1 == db2);
    
    // 添加一些测试数据
    struct tvr_nlri nlri;
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = 1001;
    nlri.u.node_nlri.time_stamp = (uint64_t)time(NULL);
    nlri.u.node_nlri.attr.spf_status = 0;
    nlri.u.node_nlri.attr.seq_num = 1;
    
    bool success = tvr_db_process(db1, &nlri, false);
    printf("Added node via db1: %s\n", success ? "SUCCESS" : "FAILED");
    
    // 通过db2验证数据存在（因为是同一个实例）
    printf("Node count via db2: %zu\n", nnlri_rb_count(&db2->nnlri_rb_root));
    
    // 销毁单例实例
    tvr_db_destroy_instance();
    printf("Instance exists after destroy: %s\n", 
           tvr_db_instance_exists() ? "YES" : "NO");
    
    printf("\n");
}

/**
 * Example 2: Thread safety considerations (conceptual)
 */
static void example_singleton_thread_safety_consideration(void) {
    printf("=== Thread Safety Consideration Example ===\n");
    
    printf("IMPORTANT: Current singleton implementation is NOT thread-safe!\n");
    printf("In multi-threaded environment, you should:\n");
    printf("1. Add mutex protection around tvr_db_get_instance()\n");
    printf("2. Or use once-initialization patterns\n");
    printf("3. Or initialize singleton at program startup\n\n");
    
    // 在程序启动时初始化（推荐做法）
    struct tvr_db *db = tvr_db_get_instance();
    printf("Initialized singleton at startup: %p\n", (void*)db);
    
    // 模拟多个模块使用同一实例
    printf("Module A accesses DB: %p\n", (void*)tvr_db_get_instance());
    printf("Module B accesses DB: %p\n", (void*)tvr_db_get_instance());
    printf("Module C accesses DB: %p\n", (void*)tvr_db_get_instance());
    
    printf("All modules use the same instance: %s\n", 
           (tvr_db_get_instance() == db) ? "YES" : "NO");
    
    printf("\n");
}

/**
 * Example 3: Singleton vs Regular pattern comparison
 */
static void example_singleton_vs_regular_pattern(void) {
    printf("=== Singleton vs Regular Pattern Comparison ===\n");
    
    // 常规模式：需要显式传递数据库指针
    printf("--- Regular Pattern ---\n");
    struct tvr_db *regular_db = tvr_db_create();
    printf("Created regular DB: %p\n", (void*)regular_db);
    
    // 模拟函数调用需要传递指针
    process_with_regular_db(regular_db);
    tvr_db_destroy(&regular_db);
    printf("Destroyed regular DB\n");
    
    // 单例模式：全局访问，无需传递指针
    printf("--- Singleton Pattern ---\n");
    
    process_with_singleton();
    printf("Singleton DB still exists: %s\n", 
           tvr_db_instance_exists() ? "YES" : "NO");
    
    tvr_db_destroy_instance();
    printf("Destroyed singleton DB\n");
    
    printf("\n");
}

/**
 * Example 4: Singleton in routing context
 */
static void example_singleton_in_routing_context(void) {
    printf("=== Singleton in Routing Context Example ===\n");
    
    // 模拟各模块协同工作
    printf("Simulating multi-module routing environment:\n");
    
    bgp_add_node_info(2001);
    bgp_add_node_info(2002);
    bgp_add_node_info(2003);
    
    ospf_add_link_info(2001, 2002, 10);
    ospf_add_link_info(2002, 2003, 15);
    
    spf_show_database_stats();
    
    // 重置数据库
    printf("Resetting database...\n");
    struct tvr_db *new_db = tvr_db_reset_instance();
    printf("New instance after reset: %p\n", (void*)new_db);
    
    spf_show_database_stats();
    
    printf("\n");
}

/**
 * Singleton pattern best practices guide
 */
void print_singleton_best_practices(void) {
    printf("=== TVR Database Singleton Best Practices ===\n\n");
    
    printf("1. INITIALIZATION:\n");
    printf("   - Call tvr_db_get_instance() at program startup\n");
    printf("   - Check tvr_db_instance_exists() before use if needed\n\n");
    
    printf("2. USAGE:\n");
    printf("   - Use tvr_db_get_instance() to access the database\n");
    printf("   - No need to pass database pointers between functions\n");
    printf("   - Multiple calls return the same instance\n\n");
    
    printf("3. CLEANUP:\n");
    printf("   - Call tvr_db_destroy_instance() at program shutdown\n");
    printf("   - Use tvr_db_reset_instance() to clear and restart\n\n");
    
    printf("4. THREAD SAFETY:\n");
    printf("   - Current implementation is NOT thread-safe\n");
    printf("   - Add mutex protection in multi-threaded environments\n");
    printf("   - Consider initialization at startup to avoid race conditions\n\n");
    
    printf("5. BENEFITS:\n");
    printf("   - Global access without global variables\n");
    printf("   - Lazy initialization\n");
    printf("   - Guaranteed single instance\n");
    printf("   - Simplified function interfaces\n\n");
    
    printf("6. DRAWBACKS:\n");
    printf("   - Harder to unit test (global state)\n");
    printf("   - Potential memory leaks if not properly cleaned up\n");
    printf("   - Hidden dependencies in code\n\n");
}

/**
 * Main function - run all examples
 */
int main(void) {
    printf("TVR Database Singleton Pattern Examples\n");
    printf("========================================\n\n");
    
    example_singleton_basic_usage();
    example_singleton_thread_safety_consideration();
    example_singleton_vs_regular_pattern();
    example_singleton_in_routing_context();
    
    print_singleton_best_practices();
    
    // 确保清理
    tvr_db_destroy_instance();
    
    printf("All examples completed successfully!\n");
    return 0;
}

/* Helper function implementations */

static void process_with_regular_db(struct tvr_db *db) {
    printf("Processing with regular DB: %p\n", (void*)db);
    // 实际处理...
}

static void process_with_singleton(void) {
    struct tvr_db *db = tvr_db_get_instance();
    printf("Processing with singleton DB: %p\n", (void*)db);
    // 实际处理...
}

static void bgp_add_node_info(uint64_t node_id) {
    struct tvr_db *db = tvr_db_get_instance();
    struct tvr_nlri nlri;
    
    nlri.type = NODE;
    nlri.u.node_nlri.local_node = node_id;
    nlri.u.node_nlri.time_stamp = (uint64_t)time(NULL);
    nlri.u.node_nlri.attr.spf_status = 0;
    nlri.u.node_nlri.attr.seq_num = 1;
    
    bool success = tvr_db_process(db, &nlri, false);
    printf("BGP: Added node %llu: %s\n", 
           (unsigned long long)node_id, success ? "OK" : "FAIL");
}

static void ospf_add_link_info(uint64_t local, uint64_t remote, uint32_t metric) {
    struct tvr_db *db = tvr_db_get_instance();
    struct tvr_nlri nlri;
    
    nlri.type = LINK;
    nlri.u.link_nlri.local_node = local;
    nlri.u.link_nlri.remote_node = remote;
    inet_pton(AF_INET6, "2001:db8::1", &nlri.u.link_nlri.link_addr);
    nlri.u.link_nlri.time_stamp = (uint64_t)time(NULL);
    nlri.u.link_nlri.attr.igp_metric = metric;
    nlri.u.link_nlri.attr.spf_status = 0;
    nlri.u.link_nlri.attr.seq_num = 1;
    
    bool success = tvr_db_process(db, &nlri, false);
    printf("OSPF: Added link %llu->%llu (metric=%u): %s\n",
           (unsigned long long)local, (unsigned long long)remote, 
           metric, success ? "OK" : "FAIL");
}

static void spf_show_database_stats(void) {
    struct tvr_db *db = tvr_db_get_instance();
    printf("SPF: Database stats - Nodes: %zu, Links: %zu, Prefixes: %zu\n",
           nnlri_rb_count(&db->nnlri_rb_root),
           lnlri_rb_count(&db->lnlri_rb_root),
           pnlri_rb_count(&db->pnlri_rb_root));
}
