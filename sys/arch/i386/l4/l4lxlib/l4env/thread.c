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
#include <l4/re/c/rm.h>
#include <l4/log/log.h>
#include <l4/re/c/util/cap.h>
#include <l4/re/c/util/kumem_alloc.h>
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

struct free_list_t {
	unsigned long *head;
	unsigned blk_sz;
};

static struct free_list_t ku_free_small = {
	.blk_sz = L4_UTCB_OFFSET,
};

static void add_to_free_list(l4_addr_t start, l4_addr_t end,
                             unsigned sz, unsigned long **head)
{
	l4_addr_t n = start;
	unsigned long **last_free = head;
	while (n + sz <= end) {
		unsigned long *u = (unsigned long *)n;
		*u = 0;
		*last_free = u;
		last_free = (unsigned long **)u;
		n += sz;
	}
}

void l4lx_thread_utcb_alloc_init(void)
{
	l4_fpage_t utcb_area = l4re_env()->utcb_area;
	l4_addr_t free_utcb = l4re_env()->first_free_utcb;
	l4_addr_t end       = ((l4_addr_t)l4_fpage_page(utcb_area) << 12UL)
	                      + (1UL << (l4_addr_t)l4_fpage_size(utcb_area));
	add_to_free_list(free_utcb, end, L4_UTCB_OFFSET,
	                 &ku_free_small.head);

	/* Used up all initial slots... */
	l4re_env()->first_free_utcb = ~0UL;
}

static int get_more_kumem(unsigned order_pages, unsigned blk_sz,
                          unsigned long **head)
{
	l4_addr_t kumem;
	if (l4re_util_kumem_alloc(&kumem, order_pages,
	                          L4_BASE_TASK_CAP, l4re_env()->rm))
	{
		enter_kdebug("eins");
		return 1;
	}
	LOG_printf("kumem = %lx\n", kumem);
	add_to_free_list(kumem, kumem + (1 << order_pages) * L4_PAGESIZE,
	                 blk_sz, head);
	return 0;
}

static void *l4lx_thread_ku_alloc_alloc(struct free_list_t *head)
{
	unsigned long *n = head->head;
	if (!n) {
		if (get_more_kumem(0, head->blk_sz, &head->head))
			return 0;

		n = head->head;
	}

	head->head = (unsigned long *)(*n);
	return (void *)n;
}

static void *l4lx_thread_ku_alloc_alloc_u(void)
{
	return l4lx_thread_ku_alloc_alloc(&ku_free_small);
}

static void *l4lx_thread_ku_alloc_alloc_v(void)
{
	return l4lx_thread_ku_alloc_alloc(&ku_free_small);
}

static void l4lx_thread_ku_alloc_free(void *k, unsigned long **head)
{
	*(unsigned long *)k = (unsigned long)(*head);
	*head = (unsigned long *)k;
}

static void l4lx_thread_ku_alloc_free_u(void *k)
{
	return l4lx_thread_ku_alloc_free(k, &ku_free_small.head);
}

static void l4lx_thread_ku_alloc_free_v(void *k)
{
	return l4lx_thread_ku_alloc_free(k, &ku_free_small.head);
}

/*
#ifdef ARCH_arm
void __thread_launch(void);
asm(
"__thread_launch:\n"
"	ldmia sp!, {r1}\n" // func
"	ldmia sp!, {lr}\n" // ret
"	ldmia sp!, {r0}\n" // arg1
"	bic sp, sp, #7\n"
"	mov pc, r1\n"
);
#endif
*/

l4lx_thread_t l4lx_thread_create(L4_CV void (*thread_func)(void *data),
                                 unsigned vcpu,
                                 void *stack_pointer,
                                 void *stack_data, unsigned stack_data_size,
                                 int prio,
                                 l4_vcpu_state_t **vcpu_state,
                                 const char *name)
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
		return 0;

	res = l4_factory_create_thread(l4re_env()->factory, l4cap);
	if (l4_error(res))
		goto out_free_cap;

	if (!stack_pointer) {
		stack_pointer = l4lx_thread_stack_alloc(l4cap);
		if (!stack_pointer) {
			LOG_printf("no more stacks, bye");
			goto out_rel_cap;
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

	utcb = l4lx_thread_ku_alloc_alloc_u();
	if (!utcb)
		goto out_rel_cap;

	if (vcpu_state) {
		*vcpu_state = (l4_vcpu_state_t *)l4lx_thread_ku_alloc_alloc_v();
		if (!*vcpu_state)
			goto out_free_utcb;
	}

	l4_utcb_tcr_u(utcb)->user[L4X_UTCB_TCR_ID]   = l4cap;
	l4_utcb_tcr_u(utcb)->user[L4X_UTCB_TCR_PRIO] = prio;

	l4_thread_control_start();
	l4_thread_control_pager(l4re_env()->rm);
	l4_thread_control_exc_handler(l4re_env()->rm);
	l4_thread_control_bind(utcb, L4_BASE_TASK_CAP);
	res = l4_thread_control_commit(l4cap);
	if (l4_error(res))
		goto out_free_vcpu;

	if (vcpu_state) {
		res = l4_thread_vcpu_control(l4cap, (l4_addr_t)(*vcpu_state));
		if (l4_error(res))
			goto out_free_vcpu;
	}

	schedp = l4_sched_param(prio, 0);
	schedp.affinity = l4_sched_cpu_set(l4x_cpu_physmap_get_id(vcpu), 0, 1);

	res = l4_scheduler_run_thread (l4re_env()->scheduler, l4cap, &schedp);
	if (l4_error(res)) {
		LOG_printf("%s: Failed to set cpu%d of thread '%s': %ld.\n",
		           __func__, vcpu, name, l4_error(res));
		goto out_free_vcpu;
	}

	LOG_printf("%s: Created thread " PRINTF_L4TASK_FORM " (%s) (u:%08lx, v:%08lx, sp:%08lx)\n",
	           __func__, PRINTF_L4TASK_ARG(l4cap), name, (l4_addr_t)utcb,
		   vcpu_state ? (l4_addr_t)(*vcpu_state) : 0, (l4_umword_t)sp);


#ifdef ARCH_arm
	res = l4_thread_ex_regs(l4cap, (l4_umword_t)__thread_launch,
	                        (l4_umword_t)sp, 0);
#else
	res = l4_thread_ex_regs(l4cap, (l4_umword_t)thread_func,
	                        (l4_umword_t)sp, 0);
#endif
	if (l4_error(res))
		goto out_free_vcpu;

	l4lx_thread_name_set(l4cap, name);

	return utcb;

out_free_vcpu:
	if (vcpu_state)
		l4lx_thread_ku_alloc_free_v(*vcpu_state);

out_free_utcb:
	l4lx_thread_ku_alloc_free_u(utcb);
out_rel_cap:
	l4re_util_cap_release(l4cap);
out_free_cap:
	l4x_cap_free(l4cap);

	return 0;
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


*//*
 * l4lx_thread_shutdown
 *//*
void l4lx_thread_shutdown(l4lx_thread_t u, void *v)
{
	l4_cap_idx_t thread = l4lx_thread_get_cap(u);

	*//* free "stack memory" used for data if there's some *//*
	l4lx_thread_stack_return(thread);
	l4lx_thread_name_delete(thread);

	l4lx_thread_ku_alloc_free_u(u);
	if (v)
		l4lx_thread_ku_alloc_free_v((l4_utcb_t *)v);

	l4re_util_cap_release(thread);
	l4x_cap_free(thread);
}
EXPORT_SYMBOL(l4lx_thread_shutdown);
*/
