/*
 *
 * Implementation of the API defined in l4lxapi/task.h
 * for l4env.
 *
 */

#include <sys/types.h>

#include <machine/cpufunc.h>

#include <machine/l4/l4lxapi/task.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/user.h>

#include <l4/sys/types.h>
#include <l4/sys/factory.h>
#include <l4/re/env.h>

/*
#include <machine/l4/l4lxapi/generic/task_gen.h>
#include <machine/l4/l4lxapi/thread.h>

#include <machine/l4/api/config.h>
#include <machine/l4/api/macros.h>

#include <l4/sys/kdebug.h>
#include <l4/sys/task.h>
#include <l4/sys/thread.h>
#include <l4/sys/scheduler.h>
#include <l4/sys/factory.h>
#include <l4/re/env.h>

#include <asm/generic/cap_alloc.h>
#include <asm/generic/kthreads.h>
#include <asm/generic/user.h>
#include <asm/generic/smp.h>
*/
void l4lx_task_init(void)
{
}
/*
int l4lx_task_number_free(l4_cap_idx_t id)
{
	if (l4_is_invalid_cap(id))
		enter_kdebug("inv-cap");
	l4x_cap_free(id);
	return 0;
}
*/
int l4lx_task_get_new_task(l4_cap_idx_t parent_id,
                           l4_cap_idx_t *id)
{
	*id = l4x_cap_alloc();
	return 0;
}
/*
#ifndef CONFIG_L4_VCPU
int l4lx_task_create_thread_in_task(l4_cap_idx_t thread, l4_cap_idx_t task,
                                    l4_cap_idx_t pager, unsigned vcpu)
{
	l4_msgtag_t t;
	l4_utcb_t *u = l4_utcb();
	l4_sched_param_t l4sp = l4_sched_param(CONFIG_L4_PRIO_SERVER_PROC, 0);
	l4sp.affinity = l4_sched_cpu_set(l4x_cpu_physmap_get_id(vcpu), 0, 1);

	t = l4_factory_create_thread_u(l4re_env()->factory, thread, u);
	if (unlikely(l4_msgtag_has_error(t)))
		printk("l4_factory_create_thread failed\n");

	l4_thread_control_start_u(u);
	l4_thread_control_bind_u(l4x_user_utcb_addr(vcpu), task, u);
	l4_thread_control_pager_u(pager, u);
	l4_thread_control_exc_handler_u(pager, u);
	l4_thread_control_alien_u(u, 1);
	t = l4_thread_control_commit_u(thread, u);
	if (unlikely(l4_error_u(t, u)))
		printk("l4_thread_control_error\n");

	t = l4_thread_ex_regs_u(thread, ~0UL, ~0UL,
	                        L4_THREAD_EX_REGS_TRIGGER_EXCEPTION, u);
	if (unlikely(l4_error_u(t, u)))
		printk("l4_thread_ex_regs error\n");

	t = l4_scheduler_run_thread(l4re_env()->scheduler,
	                            thread, &l4sp);
	if (unlikely(l4_error_u(t, u)))
		printk("l4_scheduler_run_thread error\n");

	return 1;
}
#endif
*/

int l4lx_task_create(l4_cap_idx_t task)
{
	l4_msgtag_t t;
	l4_utcb_t *u = l4_utcb();
	L4XV_V(f);
	L4XV_L(f);

	t = l4_factory_create_task_u(l4re_env()->factory, task,
	                             l4_fpage(L4X_USER_UTCB_ADDR,
	                             L4X_USER_UTCB_SIZE, 0), u);
	L4XV_U(f);
	return l4_error_u(t, u);
}

/*
static int l4lx_task_delete_obj(l4_cap_idx_t obj)
{
	l4_msgtag_t t;
	l4_utcb_t *u = l4_utcb();
	L4XV_V(f);
	L4XV_L(f);
	t = l4_task_unmap_u(L4RE_THIS_TASK_CAP,
	                    l4_obj_fpage(obj, 0, L4_FPAGE_RWX),
	                    L4_FP_ALL_SPACES, u);
	L4XV_U(f);
	return l4_error_u(t, u);
}

int l4lx_task_delete_thread(l4_cap_idx_t thread)
{
	unsigned int r;
	if (unlikely(r = l4lx_task_delete_obj(thread))) {
		printk("Failed to kill thread %lx %d\n", thread, r);
		return 0;
	}
	return 1;
}


int l4lx_task_delete_task(l4_cap_idx_t task, unsigned foo)
{
	unsigned int r;
	if (unlikely(r = l4lx_task_delete_obj(task))) {
		printk("Failed to kill task %lx: %d\n", task, r);
		return 0;
	}
	return 1;
}
*/
