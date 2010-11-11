/*
 * dispatcher
 *
 * This implements the vCPU entry IP. It will be executed, when the vCPU gets
 * interrupted, for SIGILL, SIGSEGV, SIGFPE and so on. We need to generate and
 * deliver the signals to the offending process.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <machine/frame.h>
#include <machine/cpu.h>

#include <machine/l4/vcpu.h>
#include <machine/l4/exception.h>
#include <machine/l4/setup.h>

#include <l4/sys/types.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/thread.h>
#include <l4/sys/ipc.h>
#include <l4/re/consts.h>
#include <l4/log/log.h>

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


/* Do some sanety checks. */
static inline void
l4x_vcpu_entry_sanity(l4_vcpu_state_t *vcpu)
{
	if (!(vcpu->saved_state & L4_VCPU_F_IRQ)
			&& l4x_vcpu_is_irq(vcpu)) {
		enter_kdebug("IRQ sanety checks failed.");
	}
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

//	if (vcpu->saved_state & L4_VCPU_F_USER_MODE)
//		l4x_vcpu_entry_user_arch();

	l4x_vcpu_entry_sanity(vcpu);

//	if (!vcpu->saved_state & L4_VCPU_F_USER_MODE) {
//		l4x_vcpu_entry_kern(vcpu);
//		enter_kdebug("l4x_vcpu_entry_kern() returned but should not!");
//		/* NOTREACHED */
//	}

	p = curproc;			/* current thread */
	u = p->p_addr;			/* current thread's UAREA */
	regsp = p->p_md.md_regs;	/* current trapframe */

	vcpu_to_ptregs(vcpu, regsp);

	if (l4x_vcpu_is_irq(vcpu)) {
		l4x_vcpu_handle_irq(vcpu, regsp);

//		if (aston(p))
//			handle AST
	} else if (l4x_vcpu_is_page_fault(vcpu)) {
//		l4x_dispatch_page_fault(...);
	} else {
//		l4x_dispatch_exception(p, u, vcpu, regsp);
	}

	l4x_vcpu_iret(p, u, regsp, -1, 0, 1);
}
