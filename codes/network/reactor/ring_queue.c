#include "ring_queue.h"
#include <stdio.h>
#include <string.h>

// 静态断言，确保大小为2的幂
_Static_assert((RING_QUEUE_SIZE & (RING_QUEUE_SIZE - 1)) == 0, 
               "RING_QUEUE_SIZE must be power of 2");

// 初始化队列
void ring_queue_init(ring_queue_t *q) {
    if (!q) return;
    
    atomic_store(&q->head, 0);
    atomic_store(&q->tail, 0);
    atomic_store(&q->push_count, 0);
    atomic_store(&q->pop_count, 0);
    atomic_store(&q->push_fail_count, 0);
    
    memset(q->buffer, 0, sizeof(q->buffer));
}

// 检查队列是否为空
bool ring_queue_empty(ring_queue_t *q) {
    if (!q) return true;
    
    uint32_t head = atomic_load(&q->head);
    uint32_t tail = atomic_load(&q->tail);
    
    return head == tail;
}

// 检查队列是否已满
bool ring_queue_full(ring_queue_t *q) {
    if (!q) return true;
    
    uint32_t head = atomic_load(&q->head);
    uint32_t tail = atomic_load(&q->tail);
    
    // 使用无符号计算避免负数问题
    return (tail - head) >= RING_QUEUE_SIZE;
}

// 获取队列当前大小
uint32_t ring_queue_size(ring_queue_t *q) {
    if (!q) return 0;
    
    uint32_t head = atomic_load(&q->head);
    uint32_t tail = atomic_load(&q->tail);
    
    return tail - head;
}

// 单元素入队（无锁）
bool ring_queue_push(ring_queue_t *q, int item) {
    if (!q) return false;
    
    uint32_t head, tail;
    
    while (1) {
        tail = atomic_load(&q->tail);
        head = atomic_load(&q->head);
        
        // 检查队列是否已满
        if ((tail - head) >= RING_QUEUE_SIZE) {
            atomic_fetch_add(&q->push_fail_count, 1);
            return false;
        }
        
        // 尝试原子地增加tail
        if (atomic_compare_exchange_weak(&q->tail, &tail, tail + 1)) {
            // 成功获取槽位，写入数据
            q->buffer[tail & RING_QUEUE_MASK] = item;
            
            // 确保数据在增加计数前对其他线程可见
            atomic_thread_fence(memory_order_release);
            
            atomic_fetch_add(&q->push_count, 1);
            return true;
        }
        // 如果CAS失败，重试
    }
}

// 单元素出队（无锁）
bool ring_queue_pop(ring_queue_t *q, int *item) {
    if (!q || !item) return false;
    
    uint32_t head, tail;
    
    while (1) {
        head = atomic_load(&q->head);
        tail = atomic_load(&q->tail);
        
        // 检查队列是否为空
        if (head == tail) {
            return false;
        }
        
        // 读取数据
        *item = q->buffer[head & RING_QUEUE_MASK];
        
        // 尝试原子地增加head
        if (atomic_compare_exchange_weak(&q->head, &head, head + 1)) {
            atomic_fetch_add(&q->pop_count, 1);
            return true;
        }
        // 如果CAS失败，重试
    }
}

// 批量入队（更高性能）
uint32_t ring_queue_push_batch(ring_queue_t *q, int *items, uint32_t count) {
    if (!q || !items || count == 0) return 0;
    
    uint32_t head, tail, free_space, actual_count;
    
    // 一次性获取当前状态
    tail = atomic_load(&q->tail);
    head = atomic_load(&q->head);
    
    // 计算可用空间（使用无符号减法避免负数）
    free_space = RING_QUEUE_SIZE - (tail - head);
    
    // 确定实际可插入的数量
    actual_count = (count < free_space) ? count : free_space;
    if (actual_count == 0) {
        atomic_fetch_add(&q->push_fail_count, 1);
        return 0;
    }
    
    // 尝试原子地增加tail
    uint32_t new_tail = tail + actual_count;
    if (!atomic_compare_exchange_weak(&q->tail, &tail, new_tail)) {
        // 如果CAS失败，说明其他线程修改了tail，返回0让调用者重试
        return 0;
    }
    
    // 批量拷贝数据
    uint32_t index = tail & RING_QUEUE_MASK;
    uint32_t first_chunk = RING_QUEUE_SIZE - index;
    
    if (actual_count <= first_chunk) {
        // 不需要回绕
        memcpy(&q->buffer[index], items, actual_count * sizeof(int));
    } else {
        // 需要回绕：分两次拷贝
        memcpy(&q->buffer[index], items, first_chunk * sizeof(int));
        memcpy(q->buffer, items + first_chunk, (actual_count - first_chunk) * sizeof(int));
    }
    
    // 内存屏障确保数据可见性
    atomic_thread_fence(memory_order_release);
    
    atomic_fetch_add(&q->push_count, actual_count);
    return actual_count;
}

// 批量出队（更高性能）
uint32_t ring_queue_pop_batch(ring_queue_t *q, int *items, uint32_t count) {
    if (!q || !items || count == 0) return 0;
    
    uint32_t head, tail, queue_size, actual_count;
    
    // 获取当前状态
    head = atomic_load(&q->head);
    tail = atomic_load(&q->tail);
    
    // 计算队列当前大小
    queue_size = tail - head;
    if (queue_size == 0) return 0;
    
    // 确定实际可取出的数量
    actual_count = (count < queue_size) ? count : queue_size;
    
    // 尝试原子地增加head
    uint32_t new_head = head + actual_count;
    if (!atomic_compare_exchange_weak(&q->head, &head, new_head)) {
        // 如果CAS失败，说明其他线程修改了head，返回0让调用者重试
        return 0;
    }
    
    // 批量拷贝数据
    uint32_t index = head & RING_QUEUE_MASK;
    uint32_t first_chunk = RING_QUEUE_SIZE - index;
    
    if (actual_count <= first_chunk) {
        // 不需要回绕
        memcpy(items, &q->buffer[index], actual_count * sizeof(int));
    } else {
        // 需要回绕：分两次拷贝
        memcpy(items, &q->buffer[index], first_chunk * sizeof(int));
        memcpy(items + first_chunk, q->buffer, (actual_count - first_chunk) * sizeof(int));
    }
    
    // 内存屏障确保数据读取完成
    atomic_thread_fence(memory_order_release);
    
    atomic_fetch_add(&q->pop_count, actual_count);
    return actual_count;
}