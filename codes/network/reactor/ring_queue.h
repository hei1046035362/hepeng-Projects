#ifndef RING_QUEUE_H
#define RING_QUEUE_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

// 确保大小为2的幂，便于位运算
#define RING_QUEUE_SIZE 65536  // 2^16
#define RING_QUEUE_MASK (RING_QUEUE_SIZE - 1)

// 高性能无锁环形队列
typedef struct ring_queue_s {
    // 缓存行对齐，避免伪共享
    _Alignas(64) atomic_uint head;  // 读位置
    _Alignas(64) atomic_uint tail;  // 写位置
    
    // 数据存储（缓存行对齐）
    _Alignas(64) int buffer[RING_QUEUE_SIZE];
    
    // 统计信息
    _Alignas(64) atomic_ullong push_count;
    _Alignas(64) atomic_ullong pop_count;
    _Alignas(64) atomic_ullong push_fail_count;
} ring_queue_t;

// API
void ring_queue_init(ring_queue_t *q);
bool ring_queue_push(ring_queue_t *q, int item);
bool ring_queue_pop(ring_queue_t *q, int *item);
bool ring_queue_empty(ring_queue_t *q);
bool ring_queue_full(ring_queue_t *q);
uint32_t ring_queue_size(ring_queue_t *q);

// 批量操作（更高性能）
uint32_t ring_queue_push_batch(ring_queue_t *q, int *items, uint32_t count);
uint32_t ring_queue_pop_batch(ring_queue_t *q, int *items, uint32_t count);

#endif