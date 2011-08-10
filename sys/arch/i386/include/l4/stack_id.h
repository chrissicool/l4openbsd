/*
 * License:
 * This file is largely based on code from the L4Linux project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */

#ifndef __ASM_L4__GENERIC__STACK_ID_H__
#define __ASM_L4__GENERIC__STACK_ID_H__

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <machine/cpu.h>
#include <machine/pcb.h>

#include <machine/l4/vcpu.h>
#include <machine/l4/kthreads.h>
#include <machine/l4/l4lxapi/thread.h>

#include <l4/sys/utcb.h>

struct l4x_stack_struct {
	l4_utcb_t *l4utcb;
	l4_vcpu_state_t *vcpu;
};

enum {
	L4X_UTCB_TCR_ID = 0,
	L4X_UTCB_TCR_PRIO = 1,
};

static inline
struct l4x_stack_struct *l4x_stack_struct_get(struct user *ti)
{
	/* struct is just after the user struct on the stack */
	return (struct l4x_stack_struct *)(ti + 1);
}

static inline l4_utcb_t *l4x_stack_utcb_get(void)
{
	KASSERT(curproc != NULL);
	return l4x_stack_struct_get(curproc->p_addr)->l4utcb;
}

static inline l4_vcpu_state_t *l4x_stack_vcpu_state_get(void)
{
	KASSERT(curproc != NULL);
	return l4x_stack_struct_get(curproc->p_addr)->vcpu;
}

static inline void l4x_stack_setup(struct user *ti,
		                   l4_utcb_t *u, unsigned cpu)
{
	KASSERT(ti != NULL);
	struct l4x_stack_struct *s = l4x_stack_struct_get(ti);
	s->l4utcb = u;
	s->vcpu = l4x_vcpu_state(cpu);
}

static inline l4_cap_idx_t l4x_stack_id_get(void)
{
	return l4_utcb_tcr_u(l4x_stack_utcb_get())->user[L4X_UTCB_TCR_ID];
}

static inline unsigned int l4x_stack_prio_get(void)
{
	return l4_utcb_tcr_u(l4x_stack_utcb_get())->user[L4X_UTCB_TCR_PRIO];
}

L4_INLINE int l4lx_thread_equal(l4_cap_idx_t t1, l4_cap_idx_t t2)
{
	return l4_capability_equal(t1, t2);
}

L4_INLINE int l4lx_thread_is_valid(l4lx_thread_t t)
{
	return (int)(unsigned long)t;
}

L4_INLINE l4_cap_idx_t l4lx_thread_get_cap(l4lx_thread_t t)
{
	return l4_utcb_tcr_u(t)->user[L4X_UTCB_TCR_ID];
}

#endif /* ! __ASM_L4__GENERIC__STACK_ID_H__ */
