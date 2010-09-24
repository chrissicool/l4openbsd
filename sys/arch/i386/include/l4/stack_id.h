#ifndef __ASM_L4__GENERIC__STACK_ID_H__
#define __ASM_L4__GENERIC__STACK_ID_H__

#include <machine/cpu.h>
#include <machine/pcb.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <machine/l4/l4lxapi/thread.h>
#include <l4/sys/utcb.h>

struct l4x_stack_struct {
	l4_utcb_t *l4utcb;
};

enum {
	L4X_UTCB_TCR_ID = 0,
	L4X_UTCB_TCR_PRIO = 1,
};


static inline
struct l4x_stack_struct *l4x_stack_struct_get(struct user *ti)
{
	/* struct is just after the thread_info struct on the stack */
	return (struct l4x_stack_struct *)(ti + 1);
}

static inline l4_utcb_t *l4x_stack_utcb_get(void)
{
	return l4x_stack_struct_get(curproc->p_addr)->l4utcb;
}

static inline void l4x_stack_setup(struct user *ti)
{
	struct l4x_stack_struct *s = l4x_stack_struct_get(ti);
	s->l4utcb = l4_utcb();
}

static inline l4_cap_idx_t l4x_stack_id_get(void)
{
	return l4_utcb_tcr_u(l4x_stack_utcb_get())->user[L4X_UTCB_TCR_ID];
}

static inline void l4x_stack_id_set(struct user *ti,
                                    l4_cap_idx_t id)
{
	l4_utcb_t *u = l4x_stack_struct_get(ti)->l4utcb;
	l4_utcb_tcr_u(u)->user[L4X_UTCB_TCR_ID] = id;
}

static inline unsigned int l4x_stack_prio_get(void)
{
	return l4_utcb_tcr_u(l4x_stack_utcb_get())->user[L4X_UTCB_TCR_PRIO];
}

#endif /* ! __ASM_L4__GENERIC__STACK_ID_H__ */
