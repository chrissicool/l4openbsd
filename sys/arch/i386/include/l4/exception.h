#ifndef __ASM_L4__L4X_I386__EXCEPTION_H__
#define __ASM_L4__L4X_I386__EXCEPTION_H__

#include <sys/ptrace.h>
#include <machine/reg.h>
#include <machine/psl.h>
#include <machine/cpufunc.h>

#include <l4/sys/utcb.h>
#include <l4/sys/vcpu.h>

enum l4x_cpu_modes {
	L4X_MODE_KERNEL = 0,
	L4X_MODE_USER   = 3,
};

static inline void l4x_set_cpu_mode(struct reg *r, enum l4x_cpu_modes mode)
{
	r->r_cs = mode;
}

static inline void l4x_set_user_mode(struct reg *r)
{
	l4x_set_cpu_mode(r, L4X_MODE_USER);
}

static inline void l4x_set_kernel_mode(struct reg *r)
{
	l4x_set_cpu_mode(r, L4X_MODE_KERNEL);
}

static inline unsigned long l4x_get_cpu_mode(struct reg *r)
{
	return r->r_cs & 3;
}

static inline void l4x_make_up_kernel_regs(struct reg *r)
{
	l4x_set_cpu_mode(r, L4X_MODE_KERNEL);
	r->r_eip = (unsigned long)__builtin_return_address(0);
	__asm __volatile("movl %%esp,%0" : "=r" (r->r_esp));
	r->r_eflags = read_eflags();
}

//#ifdef CONFIG_L4_VCPU

#define V2P(p, pr, v, vr) do { (p)->pr = (v)->vr; } while (0)
static inline void vcpu_to_ptregs(l4_vcpu_state_t *v,
                                  struct reg *regs)
{
	V2P(regs, r_eax,    &v->r, ax);
	V2P(regs, r_ebx,    &v->r, bx);
	V2P(regs, r_ecx,    &v->r, cx);
	V2P(regs, r_edx,    &v->r, dx);
	V2P(regs, r_edi,    &v->r, di);
	V2P(regs, r_esi,    &v->r, si);
	V2P(regs, r_ebp,    &v->r, bp);
	V2P(regs, r_eip,    &v->r, ip);
	V2P(regs, r_eflags, &v->r, flags);
	V2P(regs, r_esp,    &v->r, sp);
	V2P(regs, r_fs,    &v->r, fs);
	if (v->saved_state & L4_VCPU_F_IRQ)
	        regs->r_eflags |= PSL_I;
	else
	        regs->r_eflags &= ~PSL_I;
	regs->r_cs = (regs->r_cs & ~3) | ((v->saved_state & L4_VCPU_F_USER_MODE) ? 3 : 0);
}
#undef V2P

#define P2V(p, pr, v, vr)   do { (v)->vr = (p)->pr; } while (0)
static inline void ptregs_to_vcpu(l4_vcpu_state_t *v,
                                  struct reg *regs)
{
	P2V(regs, r_eax,    &v->r, ax);
	P2V(regs, r_ebx,    &v->r, bx);
	P2V(regs, r_ecx,    &v->r, cx);
	P2V(regs, r_edx,    &v->r, dx);
	P2V(regs, r_edi,    &v->r, di);
	P2V(regs, r_esi,    &v->r, si);
	P2V(regs, r_ebp,    &v->r, bp);
	P2V(regs, r_eip,    &v->r, ip);
	P2V(regs, r_eflags, &v->r, flags);
	P2V(regs, r_esp,    &v->r, sp);
	P2V(regs, r_fs,    &v->r, fs);
	v->saved_state &= ~(L4_VCPU_F_IRQ | L4_VCPU_F_USER_MODE);
	if (regs->r_eflags & PSL_I)
	        v->saved_state |= L4_VCPU_F_IRQ;
	if (regs->r_cs & 3)
	        v->saved_state |= L4_VCPU_F_USER_MODE;
}
#undef P2V

//#endif

#define UE2P(p, pr, e, er)   do { p->pr = e->er; } while (0)
static inline void utcb_exc_to_ptregs(l4_exc_regs_t *exc, struct reg *ptregs)
{
	UE2P(ptregs, r_eax,    exc, eax);
	UE2P(ptregs, r_ebx,    exc, ebx);
	UE2P(ptregs, r_ecx,    exc, ecx);
	UE2P(ptregs, r_edx,    exc, edx);
	UE2P(ptregs, r_edi,    exc, edi);
	UE2P(ptregs, r_esi,    exc, esi);
	UE2P(ptregs, r_ebp,    exc, ebp);
	UE2P(ptregs, r_eip,    exc, ip);
	UE2P(ptregs, r_eflags, exc, flags);
	UE2P(ptregs, r_esp,    exc, sp);
	ptregs->r_fs = exc->fs;
}
#undef UE2P

#define P2UE(e, er, p, pr) do { e->er = p->pr; } while (0)
static inline void ptregs_to_utcb_exc(struct reg *ptregs, l4_exc_regs_t *exc)
{
	P2UE(exc, eax,    ptregs, r_eax);
	P2UE(exc, ebx,    ptregs, r_ebx);
	P2UE(exc, ecx,    ptregs, r_ecx);
	P2UE(exc, edx,    ptregs, r_edx);
	P2UE(exc, edi,    ptregs, r_edi);
	P2UE(exc, esi,    ptregs, r_esi);
	P2UE(exc, ebp,    ptregs, r_ebp);
	P2UE(exc, ip,     ptregs, r_eip);
	P2UE(exc, flags,  ptregs, r_eflags);
	P2UE(exc, sp,     ptregs, r_esp);
	exc->fs = ptregs->r_fs;
}
#undef P2UE

#endif /* ! __ASM_L4__L4X_I386__EXCEPTION_H__ */
