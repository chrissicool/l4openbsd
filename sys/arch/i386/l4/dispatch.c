/*
 * vCPU dispatcher
 *
 * This implements the vCPU entry IP handler. It will be executed, when the vCPU
 * gets interrupted, for signals, traps and IRQs. We need to handle these cases.
 *
 * NOTE: When entering the vCPU handler, IRQ delivery was turned off by L4. We
 *       need to re-enable it at some point. For now, we run on a GIANT LOCK, we
 *       only ever re-enable event delivery, after we finished our job here.
 *       That means, all functions in here are called with vCPU disabled!
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/frame.h>
#include <machine/cpu.h>

#include <machine/l4/l4lxapi/task.h>
#include <machine/l4/vcpu.h>
#include <machine/l4/exception.h>
#include <machine/l4/setup.h>

#include <l4/sys/types.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/debugger.h>
#include <l4/sys/thread.h>
#include <l4/sys/ipc.h>
#include <l4/re/consts.h>
#include <l4/log/log.h>


static inline int l4x_vcpu_is_irq(l4_vcpu_state_t *vcpu);
static inline int l4x_vcpu_is_page_fault(l4_vcpu_state_t *vcpu);
static void l4x_evict_mem(l4_umword_t d);
static inline void l4x_vcpu_entry_user_arch(void);
static void l4x_vcpu_entry_kern(l4_vcpu_state_t *vcpu);
static inline void l4x_vcpu_entry_sanity(l4_vcpu_state_t *vcpu);
static inline void l4x_vcpu_iret(struct proc *p, struct user *u, struct trapframe *regs,
		l4_umword_t fp1, l4_umword_t fp2, int copy_ptregs);
void l4x_vcpu_entry(void);


static inline int
l4x_vcpu_is_irq(l4_vcpu_state_t *vcpu)
{
	return vcpu->r.trapno == 0xfe;
}

static inline int
l4x_vcpu_is_page_fault(l4_vcpu_state_t *vcpu)
{
	return vcpu->r.trapno == 0xe;
}

static void l4x_evict_mem(l4_umword_t d)
{
	l4_task_unmap(L4RE_THIS_TASK_CAP,
			l4_fpage_set_rights((l4_fpage_t)d, L4_FPAGE_RWX),
			L4_FP_OTHER_SPACES);
}

/*
 * vCPU preparations when entering from usermode.
 */
static inline void
l4x_vcpu_entry_user_arch(void)
{
	asm ("cld          \n"
	     "mov %0, %%gs \n"
	     "mov %1, %%fs \n"
	     : : "r" (l4x_x86_utcb_get_orig_segment()),
#ifdef MULTIPROCESSOR
	     "r"((l4x_fiasco_gdt_entry_offset + 2) * 8 + 3)
#else
	     "r"(l4x_x86_utcb_get_orig_segment())
#endif
	     : "memory");
}

/*
 * Special vCPU handler function for kernel entry.
 */
static void
l4x_vcpu_entry_kern(l4_vcpu_state_t *vcpu)
{
	struct trapframe regs;
	struct trapframe *regsp = &regs;
	struct proc *p = curproc;
	struct user *u = p->p_addr;
	int copy_ptregs = 0;

	if (l4x_vcpu_is_irq(vcpu)) {
		vcpu_to_ptregs(vcpu, regsp);
		l4x_vcpu_handle_irq(vcpu, regsp);
		copy_ptregs = 1;

	} else if (0 // this should not happen anymore
			&& l4x_vcpu_is_page_fault(vcpu)) {
//		l4x_vcpu_handle_kernel_pf(vcpu->r.pfa, vcpu->r.ip,
//				l4x_vcpu_is_wr_pf(vcpu));
	} else {
		int ret = l4x_vcpu_handle_kernel_exc(&vcpu->r);

		if (!ret) {
			vcpu_to_ptregs(vcpu, regsp);
//			if (l4x_dispatch_exception(p, u, vcpu, regsp))
//				enter_kdebug("exception handling failed");
			copy_ptregs = 1;
		}
	}

#ifdef MULTIPROCESSOR
	mb();
#endif
	l4x_vcpu_iret(p, u, regsp, 0, 0, copy_ptregs);
}

/* Do some sanety checks. */
static inline void
l4x_vcpu_entry_sanity(l4_vcpu_state_t *vcpu)
{
	if (!(vcpu->saved_state & L4_VCPU_F_IRQ)
			&& l4x_vcpu_is_irq(vcpu)) {
		enter_kdebug("IRQ sanety checks failed.");
	}
}

void
l4x_vcpu_create_user_task(struct proc *p)
{

	if (l4lx_task_get_new_task(L4_INVALID_CAP,
				&p->p_md.task)
			|| l4_is_invalid_cap(p->p_md.task)) {
		printf("l4x_thread_create: No task no left for user\n");
		return;
	}

	if (l4lx_task_create(p->p_md.task)) {
		printf("%s: Failed to create user task\n", __func__);
		return;
	}
// #ifdef CONFIG_L4_DEBUG_REGISTER_NAMES
	{
		char s[20];
		snprintf(s, sizeof(s), "%s", p->p_comm);
		s[sizeof(s)-1] = 0;
		l4_debugger_set_object_name(p->p_md.task, s);
	}
// #endif
/*
	l4x_arch_task_setup(&p->p_addr);
*/
}

static inline void
l4x_vcpu_iret(struct proc *p, struct user *u, struct trapframe *regs,
		l4_umword_t fp1, l4_umword_t fp2, int copy_ptregs)
{
	l4_utcb_t *utcb = l4_utcb();
	l4_vcpu_state_t *vcpu = l4x_vcpu_state_u(utcb);
	l4_msgtag_t tag;

	if (copy_ptregs)
		ptregs_to_vcpu(vcpu, regs);
	else
		vcpu->saved_state &= ~L4_VCPU_F_USER_MODE;

	if (vcpu->saved_state & L4_VCPU_F_USER_MODE) {

		/* TODO cl: implement return to user context. */

	} else {
		vcpu->saved_state |= L4_VCPU_F_EXCEPTIONS;
		vcpu->saved_state &= ~(L4_VCPU_F_DEBUG_EXC | L4_VCPU_F_PAGE_FAULTS);
		vcpu->r.gs = l4x_x86_utcb_get_orig_segment();
	}

	while( /* CONSTCOND */ 1 ) {
		tag = l4_thread_vcpu_resume_start_u(utcb);
		if (fp1)
			l4_sndfpage_add_u((l4_fpage_t)fp2, fp1, &tag, utcb);

		tag = l4_thread_vcpu_resume_commit_u(L4_INVALID_CAP, tag, utcb);

		if (l4_ipc_error(tag, utcb) == L4_IPC_SEMAPFAILED)
			l4x_evict_mem(fp2);
		else {
			enter_kdebug("IRET returned");
			while( /* CONSTCOND */ 1 ) ;
			/* NOTREACHED */
		}
	}
}

/*
 * Main entry point for asynchrnonous vCPU events.
 * TODO THERE IS STILL A LOT OF WORK TODO
 */
void
l4x_vcpu_entry(void)
{
	struct proc *p;
	struct user *u;
	struct trapframe *regsp;
	l4_vcpu_state_t *vcpu = l4x_vcpu_state(cpu);

	vcpu->state = 0;

	if (vcpu->saved_state & L4_VCPU_F_USER_MODE)
		l4x_vcpu_entry_user_arch();

	l4x_vcpu_entry_sanity(vcpu);

	if (!vcpu->saved_state & L4_VCPU_F_USER_MODE) {
		l4x_vcpu_entry_kern(vcpu);
		enter_kdebug("l4x_vcpu_entry_kern() returned but should not!");
		/* NOTREACHED */
	}

	p = curproc;			/* current thread */
	u = p->p_addr;			/* current thread's UAREA */
	regsp = p->p_md.md_regs;	/* current trapframe */

	vcpu_to_ptregs(vcpu, regsp);

	if (l4x_vcpu_is_irq(vcpu)) {
		l4x_vcpu_handle_irq(vcpu, regsp);
	} else {
		/* page fault || exception */
		extern void trap(struct trapframe *frame);
#if 0
		LOG_printf("vCPU state: trapno=%d, err=%d, pfa=%08lx, "
				"ax=%08lx, bx=%08lx, cx=%08lx, dx=%08lx, "
				"di=%08lx, si=%08lx, ss=%08lx, sp=%08lx, "
				"bp=%08lx, flags=%08lx\n",
				vcpu->r.trapno, vcpu->r.err, vcpu->r.pfa,
				vcpu->r.ax, vcpu->r.bx, vcpu->r.cx, vcpu->r.dx,
				vcpu->r.di, vcpu->r.si, vcpu->r.ss, vcpu->r.sp,
				vcpu->r.bp, vcpu->r.flags);
#endif
		trap(regsp);
	}

	l4x_vcpu_iret(p, u, regsp, -1, 0, 1);
}
