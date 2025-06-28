#include "simple_id.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

// 获取当前时间戳（毫秒）
uint64_t get_current_timestamp_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday failed");
        return 0;
    }
    return (uint64_t)(tv.tv_sec * 1000UL + tv.tv_usec / 1000);
}

// 初始化简单ID生成器
int init_simple_id_generator(simple_id_generator_t* gen) {
    if (!gen) {
        fprintf(stderr, "Error: ID generator pointer is NULL\n");
        return -1;
    }
    
    gen->last_timestamp = 0;
    gen->sequence = 0;
    
    if (pthread_mutex_init(&gen->mutex, NULL) != 0) {
        fprintf(stderr, "Error: Failed to initialize mutex: %s\n", strerror(errno));
        return -1;
    }
    
    printf("Simple ID Generator initialized successfully\n");
    printf("  Timestamp bits: %d (max ~%lu years from epoch)\n", 
           TIMESTAMP_BITS, (1UL << TIMESTAMP_BITS) / (1000UL * 60 * 60 * 24 * 365));
    printf("  Sequence bits: %d (max %lu IDs per millisecond)\n", 
           SEQUENCE_BITS, MAX_SEQUENCE + 1);
    
    return 0;
}

// 销毁ID生成器
void destroy_simple_id_generator(simple_id_generator_t* gen) {
    if (gen) {
        pthread_mutex_destroy(&gen->mutex);
        printf("Simple ID Generator destroyed\n");
    }
}

// 等待下一毫秒
static uint64_t wait_next_millis(uint64_t last_timestamp) {
    uint64_t timestamp = get_current_timestamp_ms();
    while (timestamp <= last_timestamp) {
        usleep(50); // 休眠50微秒
        timestamp = get_current_timestamp_ms();
    }
    return timestamp;
}

// 检查序列号是否已满
int is_id_sequence_full(simple_id_generator_t* gen) {
    if (!gen) return 0;
    
    pthread_mutex_lock(&gen->mutex);
    int is_full = (gen->sequence >= MAX_SEQUENCE);
    pthread_mutex_unlock(&gen->mutex);
    
    return is_full;
}

// 生成简单的全局唯一ID（仅时间戳+序列号）
uint64_t generate_simple_id(simple_id_generator_t* gen) {
    if (!gen) {
        fprintf(stderr, "Error: ID generator pointer is NULL\n");
        return 0;
    }
    
    pthread_mutex_lock(&gen->mutex);
    
    uint64_t timestamp = get_current_timestamp_ms();
    if (timestamp == 0) {
        pthread_mutex_unlock(&gen->mutex);
        return 0;
    }
    
    // 时间回退检查
    if (timestamp < gen->last_timestamp) {
        fprintf(stderr, "Warning: Clock moved backwards by %lu ms. Waiting...\n",
                gen->last_timestamp - timestamp);
        pthread_mutex_unlock(&gen->mutex);
        
        // 等待时钟追上
        while (timestamp < gen->last_timestamp) {
            usleep(1000); // 1毫秒
            timestamp = get_current_timestamp_ms();
        }
        
        pthread_mutex_lock(&gen->mutex);
    }
    
    // 同一毫秒内
    if (timestamp == gen->last_timestamp) {
        gen->sequence++;
        
        // 序列号溢出检查
        if (gen->sequence > MAX_SEQUENCE) {
            // 等待下一毫秒并重置序列号
            timestamp = wait_next_millis(gen->last_timestamp);
            gen->sequence = 0;
        }
    } else {
        // 新的毫秒，重置序列号
        gen->sequence = 0;
    }
    
    gen->last_timestamp = timestamp;
    
    // 计算相对时间戳（从基准时间开始）
    uint64_t relative_timestamp = timestamp - EPOCH_TIMESTAMP;
    
    // 检查时间戳是否超出范围
    uint64_t max_timestamp = (1UL << TIMESTAMP_BITS) - 1;
    if (relative_timestamp > max_timestamp) {
        fprintf(stderr, "Error: Timestamp overflow! Need to update epoch or reduce timestamp bits\n");
        pthread_mutex_unlock(&gen->mutex);
        return 0;
    }
    
    // 组装ID：高位时间戳 + 低位序列号
    uint64_t id = (relative_timestamp << TIMESTAMP_SHIFT) | gen->sequence;
    
    pthread_mutex_unlock(&gen->mutex);
    return id;
}

// 解析简单ID
void parse_simple_id(uint64_t id, uint64_t* timestamp, uint64_t* sequence) {
    if (timestamp) {
        uint64_t relative_timestamp = id >> TIMESTAMP_SHIFT;
        *timestamp = relative_timestamp + EPOCH_TIMESTAMP;
    }
    if (sequence) {
        *sequence = id & MAX_SEQUENCE;
    }
}

// 打印ID详细信息
void print_simple_id_info(uint64_t id) {
    uint64_t timestamp, sequence;
    parse_simple_id(id, &timestamp, &sequence);
    
    time_t seconds = timestamp / 1000;
    uint64_t milliseconds = timestamp % 1000;
    
    struct tm* tm_info = gmtime(&seconds);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("Simple ID: %lu (0x%lx)\n", id, id);
    printf("  Timestamp: %lu ms (%s.%03lu UTC)\n", timestamp, time_str, milliseconds);
    printf("  Sequence: %lu\n", sequence);
    printf("  Binary: ");
    
    // 打印二进制表示（高位时间戳部分）
    for (int i = 63; i >= SEQUENCE_BITS; i--) {
        printf("%d", (int)((id >> i) & 1));
        if (i == SEQUENCE_BITS) printf("|");
    }
    // 打印二进制表示（低位序列号部分）
    for (int i = SEQUENCE_BITS - 1; i >= 0; i--) {
        printf("%d", (int)((id >> i) & 1));
    }
    printf("\n");
    printf("  (Timestamp bits | Sequence bits)\n");
}