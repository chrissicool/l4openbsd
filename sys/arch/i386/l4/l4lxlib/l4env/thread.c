/*
 * Functions implementing the API defined in asm/l4lxapi/thread.h
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <l4/sys/kdebug.h>
#include <l4/sys/err.h>

#include <l4/sys/consts.h>
#include <l4/sys/thread.h>
#include <l4/sys/scheduler.h>
#include <l4/sys/factory.h>
#include <l4/re/env.h>
#include <l4/log/log.h>
#include <l4/re/c/util/cap.h>
#include <l4/sys/debugger.h>

#include <machine/l4/l4lxapi/thread.h>
#include <machine/l4/l4lxapi/generic/thread_gen.h>
//#include <machine/l4/l4lxapi/misc.h>
#include <machine/l4/api/macros.h>
#include <machine/l4/stack_id.h>
#include <machine/l4/smp.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/linux_compat.h>

#include <lib/libkern/libkern.h>

/* UTCB allocator */
enum {
	UTCB_BITMAP_ALLOC_MWORD_SIZE = 1,
	UTCB_BITMAP_ALLOC_BITS
	  = UTCB_BITMAP_ALLOC_MWORD_SIZE * sizeof(unsigned long) * 8,
};
static unsigned long utcb_alloc_bitmap[UTCB_BITMAP_ALLOC_MWORD_SIZE];

static inline unsigned utcb_alloc_bit(l4_addr_t utcb_addr)
{
	l4_addr_t a = l4_fpage_page(l4re_env()->utcb_area) << L4_PAGESHIFT;
	return (utcb_addr - a) / L4_UTCB_OFFSET;
}

void l4lx_thread_utcb_alloc_init(void)
{
	unsigned next_free_bit = utcb_alloc_bit(l4re_env()->first_free_utcb);

	if (next_free_bit > UTCB_BITMAP_ALLOC_BITS)
		/* First thread alloc will fail... */
		next_free_bit = UTCB_BITMAP_ALLOC_BITS;

	bitmap_zero(utcb_alloc_bitmap, UTCB_BITMAP_ALLOC_BITS);
	bitmap_fill(utcb_alloc_bitmap, next_free_bit);

	/* Use up all slots... */
	l4re_env()->first_free_utcb = ~0UL;
}


static l4_addr_t l4lx_thread_utcb_alloc_alloc(unsigned order)
{
	/* find_free_region nicely searches with our requirements, i.e.
	 * log2size aligned */
	int r = bitmap_find_free_region(utcb_alloc_bitmap,
	                                UTCB_BITMAP_ALLOC_BITS, order);
	if (r < 0)
		return 0;

	return (l4_fpage_page(l4re_env()->utcb_area) << L4_PAGESHIFT)
	       + L4_UTCB_OFFSET * r;
}
/*
static void l4lx_thread_utcb_alloc_free(l4_addr_t utcb_addr, int order)
{
	unsigned bit = utcb_alloc_bit(utcb_addr);
	if (bit + (1 << order) > UTCB_BITMAP_ALLOC_BITS)
		return;
	bitmap_release_region(utcb_alloc_bitmap, bit, order);
}
*/

static inline l4_utcb_t *next_utcb(l4_utcb_t *u)
{
	return (l4_utcb_t *)((char *)u + L4_UTCB_OFFSET);
}

/*
#ifdef ARCH_arm
void __thread_launch(void);
asm(
"__thread_launch:\n"
"	ldmia sp!, {r1}\n" // func
"	ldmia sp!, {lr}\n" // ret
"	ldmia sp!, {r0}\n" // arg1
"	mov pc, r1\n"
);
#endif
*/

l4_cap_idx_t l4lx_thread_create(L4_CV void (*thread_func)(void *data),
                                unsigned vcpu,
                                void *stack_pointer,
                                void *stack_data, unsigned stack_data_size,
                                int prio,
                                unsigned thread_control_flags, const char *name)
{
	l4_cap_idx_t l4cap;
	l4_sched_param_t schedp;
	l4_msgtag_t res;
	char l4lx_name[20] = "l4bsd.";
	l4_utcb_t *utcb;
	l4_umword_t *sp = stack_pointer;

	/* Prefix name with 'l4lx.' */
	strncpy(l4lx_name + strlen(l4lx_name), name,
	        sizeof(l4lx_name) - strlen(l4lx_name));
	l4lx_name[sizeof(l4lx_name) - 1] = 0;

	l4cap = l4x_cap_alloc();
	if (l4_is_invalid_cap(l4cap))
		return L4_INVALID_CAP;

	res = l4_factory_create_thread(l4re_env()->factory, l4cap);
	if (l4_error(res)) {
		l4x_cap_free(l4cap);
		return L4_INVALID_CAP;
	}

	if (!stack_pointer) {
		stack_pointer = l4lx_thread_stack_alloc(l4cap);
		if (!stack_pointer) {
			LOG_printf("no more stacks, bye");
			l4re_util_cap_release(l4cap);
			l4x_cap_free(l4cap);
			return L4_INVALID_CAP;
		}
	}


	sp = (l4_umword_t *)((char *)stack_pointer - 8 - stack_data_size);
	memcpy(sp, stack_data, stack_data_size);


	--sp;
	*sp = (l4_umword_t)(sp + 1);
	*(--sp) = 0;
#ifdef ARCH_arm
	*(--sp) = (l4_umword_t)thread_func;
#endif

	l4_debugger_set_object_name(l4cap, l4lx_name);

	utcb = (l4_utcb_t *)l4lx_thread_utcb_alloc_alloc(thread_control_flags & L4_THREAD_CONTROL_VCPU_ENABLED ? 1 : 0);

	l4_utcb_tcr_u(utcb)->user[L4X_UTCB_TCR_ID]   = l4cap;
	l4_utcb_tcr_u(utcb)->user[L4X_UTCB_TCR_PRIO] = prio;

	l4_thread_control_start();
	l4_thread_control_pager(l4re_env()->rm);
	l4_thread_control_exc_handler(l4re_env()->rm);
	l4_thread_control_bind(utcb, L4_BASE_TASK_CAP);
	if (thread_control_flags & L4_THREAD_CONTROL_VCPU_ENABLED) {
		l4_thread_control_vcpu_enable(1);
		// remember corresponding thread for freeing again
		l4_utcb_tcr_u(next_utcb(utcb))->user[L4X_UTCB_TCR_ID] = l4cap;
	}
	res = l4_thread_control_commit(l4cap);
	if (l4_error(res)) {
		l4re_util_cap_release(l4cap);
		l4x_cap_free(l4cap);
		return L4_INVALID_CAP;
	}

	schedp = l4_sched_param(prio, 0);
	schedp.affinity = l4_sched_cpu_set(l4x_cpu_physmap_get_id(vcpu), 0, 1);

	res = l4_scheduler_run_thread (l4re_env()->scheduler, l4cap, &schedp);
	if (l4_error(res)) {
		LOG_printf("%s: Failed to set cpu%d of thread '%s': %ld.\n",
		           __func__, vcpu, name, l4_error(res));
		l4re_util_cap_release(l4cap);
		l4x_cap_free(l4cap);
		return L4_INVALID_CAP;
	}

	LOG_printf("%s: Created thread " PRINTF_L4TASK_FORM " (%s) (u:%08lx)\n",
	           __func__, PRINTF_L4TASK_ARG(l4cap), name, (l4_addr_t)utcb);


#ifdef ARCH_arm
	res = l4_thread_ex_regs(l4cap, (l4_umword_t)__thread_launch,
	                        (l4_umword_t)sp, 0);
#else
	res = l4_thread_ex_regs(l4cap, (l4_umword_t)thread_func,
	                        (l4_umword_t)sp, 0);
#endif
	if (l4_error(res)) {
		l4re_util_cap_release(l4cap);
		l4x_cap_free(l4cap);
		return L4_INVALID_CAP;
	}

	l4lx_thread_name_set(l4cap, name);

	return l4cap;
}

/*
 * l4lx_thread_pager_change
 */
void l4lx_thread_pager_change(l4_cap_idx_t thread, l4_cap_idx_t pager)
{
	l4_utcb_t *u = l4_utcb();

	l4_thread_control_start_u(u);
	l4_thread_control_pager_u(pager, u);
	l4_thread_control_exc_handler_u(pager, u);
	l4_thread_control_commit_u(thread, u);
}

/*
 * l4lx_thread_set_kernel_pager
 *//*
void l4lx_thread_set_kernel_pager(l4_cap_idx_t thread)
{
	l4lx_thread_pager_change(thread, l4x_start_thread_id);
}


static l4_utcb_t *search_thread_utcb(l4_cap_idx_t id, unsigned *order)
{
	l4_addr_t start = l4_fpage_page(l4re_env()->utcb_area) << L4_PAGESHIFT;
	l4_addr_t end = start + (1UL << l4_fpage_size(l4re_env()->utcb_area));
	l4_utcb_t *u = (l4_utcb_t *)start;
	for (; u < (l4_utcb_t *)end; u = next_utcb(u)) {
		l4_cap_idx_t t = l4_utcb_tcr_u(u)->user[L4X_UTCB_TCR_ID];
		if (l4_capability_equal(t, id)) {
			t = l4_utcb_tcr_u(next_utcb(u))->user[L4X_UTCB_TCR_ID];
			*order = l4_capability_equal(t, id) ? 1 : 0;
			return u;
		}
	}
	return NULL;
}

*//*
 * l4lx_thread_shutdown
 *//*
void l4lx_thread_shutdown(l4_cap_idx_t thread)
{
	unsigned order;
	l4_utcb_t *u = search_thread_utcb(thread, &order);

	*//* free "stack memory" used for data if there's some *//*
	l4lx_thread_stack_return(thread);
	l4lx_thread_name_delete(thread);

	if (u)
		l4lx_thread_utcb_alloc_free((l4_addr_t)u, order);

	l4re_util_cap_release(thread);
	l4x_cap_free(thread);
}
EXPORT_SYMBOL(l4lx_thread_shutdown);
*/
