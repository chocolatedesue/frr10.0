#ifndef SIMPLE_ID_H
#define SIMPLE_ID_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

// ID结构配置 (64位总长度)
#define TIMESTAMP_BITS 42  // 时间戳位数 (可用约139年)
#define SEQUENCE_BITS  22  // 序列号位数 (每毫秒可生成4M个ID)

// 位移量
#define SEQUENCE_SHIFT  0
#define TIMESTAMP_SHIFT SEQUENCE_BITS

// 最大值
#define MAX_SEQUENCE    ((1UL << SEQUENCE_BITS) - 1)

// 时间基准 (2025-01-01 00:00:00 UTC)
#define EPOCH_TIMESTAMP 1735689600000UL

// ID生成器结构
typedef struct {
    uint64_t last_timestamp;
    uint64_t sequence;
    pthread_mutex_t mutex;
} simple_id_generator_t;

// 函数声明
int init_simple_id_generator(simple_id_generator_t* gen);
void destroy_simple_id_generator(simple_id_generator_t* gen);
uint64_t generate_simple_id(simple_id_generator_t* gen);
uint64_t get_current_timestamp_ms(void);
void parse_simple_id(uint64_t id, uint64_t* timestamp, uint64_t* sequence);
void print_simple_id_info(uint64_t id);
int is_id_sequence_full(simple_id_generator_t* gen);

#endif