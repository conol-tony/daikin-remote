#ifndef TEST_RTTHREAD_H
#define TEST_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t rt_tick_t;
typedef uintptr_t rt_ubase_t;
typedef int rt_err_t;
typedef void *rt_thread_t;
struct rt_mailbox { int unused; };

#define RT_EOK 0
#define RT_NULL NULL
#define RT_IPC_FLAG_FIFO 0

rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(int ms);
rt_err_t rt_mb_init(struct rt_mailbox *, const char *, void *, size_t, int);
rt_err_t rt_mb_send(struct rt_mailbox *, rt_ubase_t);
rt_err_t rt_mb_recv(struct rt_mailbox *, rt_ubase_t *, int);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, size_t, int, int);
rt_err_t rt_thread_startup(rt_thread_t);
void rt_thread_mdelay(int ms);

#endif
