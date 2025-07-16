/*
 * FRR autounlock 机制演示代码
 * 展示 frr_mutex_lock_autounlock 的工作原理和使用方法
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// 模拟 FRR 的 autounlock 实现
#define NAMECTR(x) x##__LINE__

// 自动解锁宏定义（简化版）
#define auto_mutex_lock(mutex)                                                 \
    pthread_mutex_t *NAMECTR(_auto_mtx_)                                      \
        __attribute__((cleanup(auto_unlock_cleanup))) = auto_lock_helper(mutex)

// 辅助函数：加锁并返回互斥锁指针
static inline pthread_mutex_t *auto_lock_helper(pthread_mutex_t *mutex)
{
    printf("🔒 Auto-locking mutex at %p\n", (void*)mutex);
    pthread_mutex_lock(mutex);
    return mutex;
}

// 清理函数：自动解锁
static inline void auto_unlock_cleanup(pthread_mutex_t **mutex)
{
    if (*mutex) {
        printf("🔓 Auto-unlocking mutex at %p\n", (void*)*mutex);
        pthread_mutex_unlock(*mutex);
        *mutex = NULL;
    }
}

// 全局互斥锁
pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
int shared_counter = 0;

// 演示1：基本的自动解锁
void demo_basic_autounlock()
{
    printf("\n=== Demo 1: 基本自动解锁 ===\n");
    
    {
        printf("进入临界区前\n");
        auto_mutex_lock(&global_mutex);
        printf("进入临界区，修改共享数据\n");
        
        shared_counter++;
        printf("shared_counter = %d\n", shared_counter);
        
        printf("离开作用域，即将自动解锁\n");
    } // 这里自动解锁
    
    printf("已离开临界区\n");
}

// 演示2：提前返回时的自动解锁
void demo_early_return(int should_return_early)
{
    printf("\n=== Demo 2: 提前返回的自动解锁 ===\n");
    
    auto_mutex_lock(&global_mutex);
    printf("获得锁，开始处理\n");
    
    if (should_return_early) {
        printf("检测到提前返回条件\n");
        return; // 这里会自动解锁！
    }
    
    printf("正常处理完成\n");
    // 函数结束时也会自动解锁
}

// 演示3：异常情况的处理
void demo_exception_safety()
{
    printf("\n=== Demo 3: 异常安全性 ===\n");
    
    auto_mutex_lock(&global_mutex);
    printf("获得锁，开始可能失败的操作\n");
    
    // 模拟可能失败的操作
    if (rand() % 2) {
        printf("操作失败，直接返回\n");
        return; // 自动解锁
    }
    
    printf("操作成功完成\n");
    // 正常结束也会自动解锁
}

// 演示4：嵌套作用域
void demo_nested_scopes()
{
    printf("\n=== Demo 4: 嵌套作用域 ===\n");
    
    printf("外层开始\n");
    
    if (1) {
        auto_mutex_lock(&global_mutex);
        printf("内层：获得锁\n");
        
        shared_counter += 10;
        printf("内层：shared_counter = %d\n", shared_counter);
        
        if (shared_counter > 5) {
            printf("内层：条件满足，处理特殊逻辑\n");
            // 可以有更复杂的控制流
        }
        
        printf("内层：即将离开作用域\n");
    } // 内层作用域结束，自动解锁
    
    printf("外层：继续执行\n");
}

// 演示5：与传统锁的对比
void demo_traditional_locking()
{
    printf("\n=== Demo 5: 传统锁方式（对比） ===\n");
    
    printf("🔒 手动加锁\n");
    pthread_mutex_lock(&global_mutex);
    
    shared_counter++;
    printf("shared_counter = %d\n", shared_counter);
    
    // 在传统方式中，如果这里有复杂的控制流
    // 你需要在每个可能的退出点都手动解锁
    if (shared_counter > 15) {
        printf("🔓 手动解锁（提前返回）\n");
        pthread_mutex_unlock(&global_mutex);
        return;
    }
    
    printf("🔓 手动解锁（正常结束）\n");
    pthread_mutex_unlock(&global_mutex);
}

// 模拟 BGP 包发送函数（使用自动解锁）
typedef struct {
    pthread_mutex_t io_mtx;
    int packet_count;
} connection_t;

void bgp_send_packet_demo(connection_t *conn, const char *packet_data)
{
    printf("\n=== BGP 包发送演示 ===\n");
    printf("发送数据: %s\n", packet_data);
    
    // 保护 I/O 操作
    auto_mutex_lock(&conn->io_mtx);
    printf("🔒 保护 I/O 操作\n");
    
    // 模拟包计数检查
    if (conn->packet_count > 100) {
        printf("❌ 包队列已满，丢弃包\n");
        return; // 自动解锁
    }
    
    // 模拟网络错误
    if (rand() % 3 == 0) {
        printf("❌ 网络错误，发送失败\n");
        return; // 自动解锁
    }
    
    // 模拟实际发送
    conn->packet_count++;
    printf("✅ 包发送成功，当前队列: %d\n", conn->packet_count);
    
    // 函数结束时自动解锁，无需手动管理
}

int main()
{
    printf("FRR Auto-unlock 机制演示\n");
    printf("========================\n");
    
    // 初始化随机数
    srand(time(NULL));
    
    // 运行各种演示
    demo_basic_autounlock();
    demo_early_return(1);  // 提前返回
    demo_early_return(0);  // 正常执行
    demo_exception_safety();
    demo_nested_scopes();
    demo_traditional_locking();
    
    // BGP 包发送演示
    connection_t conn = { .io_mtx = PTHREAD_MUTEX_INITIALIZER, .packet_count = 0 };
    
    bgp_send_packet_demo(&conn, "UPDATE message");
    bgp_send_packet_demo(&conn, "KEEPALIVE message");
    bgp_send_packet_demo(&conn, "NOTIFICATION message");
    
    printf("\n最终 shared_counter 值: %d\n", shared_counter);
    printf("最终包计数: %d\n", conn.packet_count);
    
    return 0;
}

/*
 * 编译和运行：
 * gcc -o autounlock_demo autounlock_demo.c -lpthread
 * ./autounlock_demo
 * 
 * 输出示例：
 * ===========================================
 * FRR Auto-unlock 机制演示
 * ========================
 * 
 * === Demo 1: 基本自动解锁 ===
 * 进入临界区前
 * 🔒 Auto-locking mutex at 0x...
 * 进入临界区，修改共享数据
 * shared_counter = 1
 * 离开作用域，即将自动解锁
 * 🔓 Auto-unlocking mutex at 0x...
 * 已离开临界区
 * 
 * === Demo 2: 提前返回的自动解锁 ===
 * 🔒 Auto-locking mutex at 0x...
 * 获得锁，开始处理
 * 检测到提前返回条件
 * 🔓 Auto-unlocking mutex at 0x...
 * 
 * ... 更多输出 ...
 */
