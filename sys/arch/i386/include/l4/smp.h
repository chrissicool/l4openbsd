#ifndef __ASM_L4__GENERIC__SMP_H__
#define __ASM_L4__GENERIC__SMP_H__

#include <machine/cpu.h>

#include <l4/sys/types.h>

l4_cap_idx_t linux_server_thread_id = L4_INVALID_CAP;

struct l4x_cpu_physmap_struct
{
	unsigned int phys_id;
};
struct l4x_cpu_physmap_struct l4x_cpu_physmap[MAXCPUS];

#ifdef MULTIPROCESSOR
#error not implemented!

#include <linux/sched.h>
#include <linux/bitops.h>

#include <l4/sys/types.h>

#define L4X_TIMER_VECTOR	9

extern unsigned int l4x_nr_cpus;

void do_l4x_smp_process_IPI(int vector, struct pt_regs *regs);

void l4x_cpu_spawn(int cpu, struct task_struct *idle);
void l4x_cpu_release(int cpu);
l4_cap_idx_t l4x_cpu_thread_get(int cpu);
struct task_struct *l4x_cpu_idle_get(int cpu);
void l4x_smp_broadcast_timer(void);
void l4x_send_IPI_mask_bitmask(unsigned long, int);
void l4x_cpu_ipi_trigger(unsigned cpu);

void l4x_cpu_ipi_thread_start(unsigned cpu);

void l4x_migrate_thread(l4_cap_idx_t thread, unsigned from_cpu, unsigned to_cpu);

#ifdef CONFIG_HOTPLUG_CPU
void l4x_cpu_ipi_thread_stop(unsigned cpu);
void l4x_cpu_dead(void);
void l4x_destroy_ugate(unsigned cpu);
void l4x_shutdown_cpu(unsigned cpu);
#ifdef ARCH_x86
unsigned l4x_utcb_get_orig_segment(void);
#endif
#endif

#ifdef ARCH_x86
void l4x_load_percpu_gdt_descriptor(struct desc_struct *gdt);
#endif

#else
/* UP Systems */

static inline l4_cap_idx_t l4x_cpu_thread_get(int _cpu)
{
	return linux_server_thread_id;
}

static inline int l4x_IPI_pending_tac(int cpu)
{
	return 0;
}

static inline int l4x_IPI_is_ipi_message(l4_umword_t d0)
{
	return 0;
}

static inline void l4x_smp_process_IPI(void)
{
}

static inline void l4x_smp_broadcast_timer(void)
{
}

static inline unsigned l4x_cpu_physmap_get_id(unsigned lcpu)
{
	return l4x_cpu_physmap[lcpu].phys_id;
}

#endif

#endif /* ! __ASM_L4__GENERIC__SMP_H__ */
