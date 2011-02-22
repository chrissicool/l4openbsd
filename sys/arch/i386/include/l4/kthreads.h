/*
 * Kernel thread related things.
 */
#ifndef __ASM_L4__GENERIC__KTHREADS_H__
#define __ASM_L4__GENERIC__KTHREADS_H__

#include <l4/sys/utcb.h>

/* thread id type */
typedef l4_utcb_t *l4lx_thread_t;

/* thread id of the starter thread, also pager for all other threads */
extern l4_cap_idx_t l4x_start_thread_id;

/* pager of the starter thread */
extern l4_cap_idx_t l4x_start_thread_pager_id;

l4lx_thread_t l4x_cpu_thread_get(int cpu);
l4_cap_idx_t  l4x_cpu_thread_get_cap(int cpu);

#endif /* ! __ASM_L4__GENERIC__KTHREADS_H__ */
