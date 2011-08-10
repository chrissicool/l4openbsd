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

#ifndef __ASM_L4__L4X_I386__EXCEPTION_H__
#define __ASM_L4__L4X_I386__EXCEPTION_H__

#include <sys/ptrace.h>
#include <machine/frame.h>
#include <machine/psl.h>
#include <machine/cpufunc.h>

#include <l4/sys/utcb.h>
#include <l4/sys/vcpu.h>

enum l4x_cpu_modes {
	L4X_MODE_KERNEL = 0,
	L4X_MODE_USER   = 3,
};

static inline void l4x_set_cpu_mode(struct trapframe *r, enum l4x_cpu_modes mode)
{
	r->tf_cs = mode;
}

static inline void l4x_set_user_mode(struct trapframe *r)
{
	l4x_set_cpu_mode(r, L4X_MODE_USER);
}

static inline void l4x_set_kernel_mode(struct trapframe *r)
{
	l4x_set_cpu_mode(r, L4X_MODE_KERNEL);
}

static inline unsigned long l4x_get_cpu_mode(struct trapframe *r)
{
	return r->tf_cs & 3;
}

static inline void l4x_make_up_kernel_regs(struct trapframe *r)
{
	l4x_set_cpu_mode(r, L4X_MODE_KERNEL);
	r->tf_eip = (unsigned long)__builtin_return_address(0);
	__asm __volatile("movl %%esp,%0" : "=r" (r->tf_esp));
	r->tf_eflags = read_eflags();
}

//#ifdef CONFIG_L4_VCPU

#define V2P(p, pr, v, vr) do { (p)->pr = (v)->vr; } while (0)
static inline void vcpu_to_ptregs(l4_vcpu_state_t *v,
                                  struct trapframe *regs)
{
	V2P(regs, tf_eax,    &v->r, ax);
	V2P(regs, tf_ebx,    &v->r, bx);
	V2P(regs, tf_ecx,    &v->r, cx);
	V2P(regs, tf_edx,    &v->r, dx);
	V2P(regs, tf_edi,    &v->r, di);
	V2P(regs, tf_esi,    &v->r, si);
	V2P(regs, tf_ebp,    &v->r, bp);
	V2P(regs, tf_eip,    &v->r, ip);
	V2P(regs, tf_eflags, &v->r, flags);
	V2P(regs, tf_esp,    &v->r, sp);
//	V2P(regs, tf_fs,    &v->r, fs);
//	V2P(regs, tf_gs,    &v->r, gs);
	V2P(regs, tf_trapno,    &v->r, trapno);
	V2P(regs, tf_err,    &v->r, err);
	if (v->saved_state & L4_VCPU_F_IRQ)
	        regs->tf_eflags |= PSL_I;
	else
	        regs->tf_eflags &= ~PSL_I;

	/* XXX hshoexer: upper 30 bits of cs are undefined */
	regs->tf_cs = (regs->tf_cs & ~3) | ((v->saved_state & L4_VCPU_F_USER_MODE) ? SEL_UPL : SEL_KPL);
}
#undef V2P

#define P2V(p, pr, v, vr)   do { (v)->vr = (p)->pr; } while (0)
static inline void ptregs_to_vcpu(l4_vcpu_state_t *v,
                                  struct trapframe *regs)
{
	P2V(regs, tf_eax,    &v->r, ax);
	P2V(regs, tf_ebx,    &v->r, bx);
	P2V(regs, tf_ecx,    &v->r, cx);
	P2V(regs, tf_edx,    &v->r, dx);
	P2V(regs, tf_edi,    &v->r, di);
	P2V(regs, tf_esi,    &v->r, si);
	P2V(regs, tf_ebp,    &v->r, bp);
	P2V(regs, tf_eip,    &v->r, ip);
	P2V(regs, tf_eflags, &v->r, flags);
	P2V(regs, tf_esp,    &v->r, sp);
//	P2V(regs, tf_fs,    &v->r, fs);
//	P2V(regs, tf_gs,    &v->r, gs);
	v->saved_state &= ~(L4_VCPU_F_IRQ | L4_VCPU_F_USER_MODE);
	if (regs->tf_eflags & PSL_I)
	        v->saved_state |= L4_VCPU_F_IRQ;
	if (regs->tf_cs & 3)
	        v->saved_state |= L4_VCPU_F_USER_MODE;

	v->r.fs = 0;	/* XXX */
	v->r.gs = 0;	/* XXX */
}
#undef P2V

//#endif

#define UE2P(p, pr, e, er)   do { p->pr = e->er; } while (0)
static inline void utcb_exc_to_ptregs(l4_exc_regs_t *exc, struct trapframe *ptregs)
{
	UE2P(ptregs, tf_eax,    exc, eax);
	UE2P(ptregs, tf_ebx,    exc, ebx);
	UE2P(ptregs, tf_ecx,    exc, ecx);
	UE2P(ptregs, tf_edx,    exc, edx);
	UE2P(ptregs, tf_edi,    exc, edi);
	UE2P(ptregs, tf_esi,    exc, esi);
	UE2P(ptregs, tf_ebp,    exc, ebp);
	UE2P(ptregs, tf_eip,    exc, ip);
	UE2P(ptregs, tf_eflags, exc, flags);
	UE2P(ptregs, tf_esp,    exc, sp);
	ptregs->tf_fs = exc->fs;
}
#undef UE2P

#define P2UE(e, er, p, pr) do { e->er = p->pr; } while (0)
static inline void ptregs_to_utcb_exc(struct trapframe *ptregs, l4_exc_regs_t *exc)
{
	P2UE(exc, eax,    ptregs, tf_eax);
	P2UE(exc, ebx,    ptregs, tf_ebx);
	P2UE(exc, ecx,    ptregs, tf_ecx);
	P2UE(exc, edx,    ptregs, tf_edx);
	P2UE(exc, edi,    ptregs, tf_edi);
	P2UE(exc, esi,    ptregs, tf_esi);
	P2UE(exc, ebp,    ptregs, tf_ebp);
	P2UE(exc, ip,     ptregs, tf_eip);
	P2UE(exc, flags,  ptregs, tf_eflags);
	P2UE(exc, sp,     ptregs, tf_esp);
	exc->fs = ptregs->tf_fs;
}
#undef P2UE

#endif /* ! __ASM_L4__L4X_I386__EXCEPTION_H__ */
