#ifndef __ASM_L4__GENERIC__VCPU_H__
#define __ASM_L4__GENERIC__VCPU_H__

/* #ifdef CONFIG_L4_VCPU */

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/reg.h>

#include <l4/sys/types.h>
#include <l4/sys/vcpu.h>
#include <l4/sys/utcb.h>

static inline
l4_vcpu_state_t *l4x_vcpu_state_u(l4_utcb_t *u)
{
	char *a = (char *)l4_utcb() + L4_UTCB_OFFSET;
	return (l4_vcpu_state_t*)a;
}

extern l4_vcpu_state_t *l4x_vcpu_states[MAXCPUS];

static inline
l4_vcpu_state_t *l4x_vcpu_state(int cpu)
{
	return l4x_vcpu_states[cpu];
}

void l4x_vcpu_handle_irq(l4_vcpu_state_t *t, struct reg *regs);
//void l4x_vcpu_handle_ipi(struct pt_regs *regs);

static inline void l4x_vcpu_entry(void) {}; /* TODO implement in dispatch.c */

/* #else */
#if 0

#define  L4XV_V(n)
#define  L4XV_L(n) do {} while (0)
#define  L4XV_U(n) do {} while (0)

#endif

#endif /* ! __ASM_L4__GENERIC__VCPU_H__ */
