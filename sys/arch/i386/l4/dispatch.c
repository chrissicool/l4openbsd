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

#include "npx.h"

#include <machine/l4/l4lxapi/task.h>
#include <machine/l4/vcpu.h>
#include <machine/l4/exception.h>
#include <machine/l4/stack_id.h>
#include <machine/l4/setup.h>

#include <machine/l4/log.h>

#include <l4/sys/types.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/debugger.h>
#include <l4/sys/thread.h>
#include <l4/sys/ipc.h>
#include <l4/util/util.h>
#include <l4/re/consts.h>
#include <l4/log/log.h>

//#define TRAP_DEBUG
#ifdef TRAP_DEBUG
#include <machine/l4/log.h>
#include <kern/syscalls.c>
extern char *trap_type[];
#define dbg_printf(...)				\
	do {						\
		L4XV_V(n);				\
		L4XV_L(n);				\
		printf("trap: " __VA_ARGS__);		\
		L4XV_U(n);				\
	} while(0)
#else
#define dbg_printf(...)
#endif

static inline int l4x_vcpu_is_write_pf(l4_vcpu_state_t *vcpu);
static inline l4_umword_t l4x_l4pfa(l4_vcpu_state_t *vcpu);
static inline int l4x_vcpu_is_user(l4_vcpu_state_t *vcpu);
static inline int l4x_vcpu_is_syscall(l4_vcpu_state_t *vcpu);
static inline int l4x_msgtag_fpu(int cpunum);
static void l4x_evict_mem(l4_umword_t d);
static inline void l4x_vcpu_entry_user_arch(void);
static inline void l4x_vcpu_entry_sanity(l4_vcpu_state_t *vcpu);
static void l4x_arch_task_start_setup(struct proc *p);
void l4x_ret_from_fork(void);
void l4x_handle_user_pf(l4_vcpu_state_t *vcpu, struct proc *p, struct user *u,
		struct trapframe *regsp);
void l4x_handle_kernel_pf(l4_vcpu_state_t *vcpu, struct proc *p, struct user *u,
		struct trapframe *regsp);
static inline void l4x_vcpu_iret(struct proc *p, struct user *u, struct trapframe *regs,
		l4_umword_t fp1, l4_umword_t fp2, int copy_ptregs);
void l4x_vcpu_entry(void);

/* Convert vCPU traps to trap()'s trapno. See IDTVEC's and init386(). */
static int vcpu2trapno[] = {
	/* 00 */ T_DIVIDE, T_TRCTRAP, T_NMI, T_BPTFLT, T_OFLOW, T_BOUND,
	/* 06 */ T_PRIVINFLT, T_DNA, T_DOUBLEFLT, T_FPOPFLT, T_TSSFLT,
	/* 11 */ T_SEGNPFLT, T_STKFLT, T_PROTFLT, T_PAGEFLT, T_RESERVED,
	/* 16 */ T_ARITHTRAP, T_ALIGNFLT, T_MACHK, T_XFTRAP,
};



static inline int
l4x_vcpu_is_write_pf(l4_vcpu_state_t *vcpu)
{
	return(l4vcpu_is_page_fault_entry(vcpu) &&
	       (vcpu->r.err & 2));
}

static inline l4_umword_t
l4x_l4pfa(l4_vcpu_state_t *vcpu)
{
	return ((vcpu->r.pfa & ~3) | (vcpu->r.err & 2));
}

static inline int
l4x_vcpu_is_user(l4_vcpu_state_t *vcpu)
{
	return ((vcpu->saved_state & L4_VCPU_F_USER_MODE) != 0);
}

static inline int
l4x_vcpu_is_syscall(l4_vcpu_state_t *vcpu)
{
	return (l4x_vcpu_is_user(vcpu) &&
			(vcpu->r.trapno == 0xd) && (vcpu->r.err == 0x402));
}

void l4x_fpu_set(int on_off)
{
	l4x_cpu_fpu_state[cpu_number()].enabled = on_off;
}

struct l4x_arch_cpu_fpu_state *l4x_fpu_get(unsigned cpu)
{
	return &l4x_cpu_fpu_state[cpu];
}

static inline int l4x_msgtag_fpu(int cpunum)
{
	return l4x_fpu_get(cpunum)->enabled
		?  L4_MSGTAG_TRANSFER_FPU : 0;
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

/* Do some sanety checks. */
static inline void
l4x_vcpu_entry_sanity(l4_vcpu_state_t *vcpu)
{
	if (!(vcpu->saved_state & L4_VCPU_F_IRQ)
			&& l4vcpu_is_irq_entry(vcpu)) {
		enter_kdebug("IRQ sanety checks failed.");
	}
}

void
l4x_vcpu_create_user_task(struct proc *p)
{
	struct pmap *pmap = p->p_vmspace->vm_map.pmap;

	if (l4lx_task_get_new_task(L4_INVALID_CAP,
				&pmap->task)
			|| l4_is_invalid_cap(pmap->task)) {
		printf("l4x_thread_create: No task no left for user\n");
		return;
	}

	if (l4lx_task_create(pmap->task)) {
		printf("%s: Failed to create user task\n", __func__);
		return;
	}
#ifdef L4_DEBUG_REGISTER_NAMES
	{
		char s[20];
		snprintf(s, sizeof(s), "l4bsd.%s", p->p_comm);
		s[sizeof(s)-1] = 0;
		l4_debugger_set_object_name(pmap->task, s);
	}
#endif
/*
	l4x_arch_task_setup(&p->p_addr);
*/
}

static void
l4x_arch_task_start_setup(struct proc *p)
{
	struct trapframe *tf = p->p_md.md_regs;
	unsigned int gs = l4x_vcpu_state(cpu_number())->r.gs;
	unsigned int v = (gs & 0xffff) >> 3;

	/*
	 * - Remember GS in FS, so that pograms can find their UTCB
	 * - libl4sys-l4x.a uses %fs to get the UTCB address.
	 * - Do not set GS because glibc does not seem to like if gs is not 0
	 * - Only do this if this is the first usage of the L4 thread in
	 *   this task, otherwise gs will have the glibc-gs
	 * - Ensure this by checking if the segment is one of the user ones or
	 *   another one (then it's the utcb one)
	 */
	if (    v < l4x_fiasco_gdt_entry_offset
	     || v > l4x_fiasco_gdt_entry_offset + 3)
		tf->tf_fs = gs;

	/*
	 * TODO Setup LDTs
	 */
}

/*
 * After fork, we only reach here after the scheduler has chosen us.
 */
void
l4x_ret_from_fork(void)
{
	struct proc *p = curproc;
	struct user *u = p->p_addr;
	struct trapframe *tf = p->p_md.md_regs;
	l4_vcpu_state_t *vcpu = l4x_stack_vcpu_state_get();

	tf->tf_cs = GSEL(GUCODE_SEL, SEL_UPL);		/* set USERMODE */
	vcpu->saved_state |= L4_VCPU_F_USER_MODE;

	l4x_vcpu_iret(p, u, tf, 0, 0, 1);
}

/*
 * Handle user page faults differently. We need to find out, what to map
 * into user address space. If we cannot figure out the right address,
 * we return and trap() needs to SIGSEGV the proc.
 */
void
l4x_handle_user_pf(l4_vcpu_state_t *vcpu, struct proc *p, struct user *u,
		struct trapframe *regsp)
{
	paddr_t *kpa;
	vaddr_t  uva = (vaddr_t)trunc_page(rcr2());
	vm_prot_t prot = VM_PROT_READ;
	l4_umword_t upage = 0, kpage = 0;
	unsigned fpage_size = L4_LOG2_PAGESIZE;

	upage = rcr2();

	prot |= (regsp->tf_err & 2) ? VM_PROT_WRITE : 0;

	kpa = l4x_pmap_walk_pd(p, uva, prot);

	if (!kpa || uva > VM_MAXUSER_ADDRESS)
		return;

#ifdef DIAGNOSTIC
	/*
	 * Sanety check results. Page fault handler might have gone crazy.
	 */
	if (((unsigned long)kpa < PA_START) || ((unsigned long)kpa > PA_START + l4x_mainmem_size)) {
		printf("Got invalid PA %p for VA %p (%d, %s)\n",
				kpa, uva, p->p_pid, p->p_comm);
		return;
	}
#endif

	upage = (upage & L4_PAGEMASK) | L4_ITEM_MAP;

	if (prot & VM_PROT_WRITE)
		kpage = l4_fpage((unsigned long)kpa, fpage_size,
				L4_FPAGE_RW).fpage;
	else
		kpage = l4_fpage((unsigned long)kpa, fpage_size,
				L4_FPAGE_RO).fpage;

	l4x_vcpu_iret(p, u, regsp, upage, kpage, 1);
	/* NOTREACHED */
}

/*
 * Handle pagefaults, which OpenBSD cannot handle.
 * L4re may steal memory from us. Since we are our own pager, we need to handle
 * that case. Touching the page is usually enough.
 */
void
l4x_handle_kernel_pf(l4_vcpu_state_t *vcpu, struct proc *p, struct user *u,
		struct trapframe *regsp)
{
	if (l4x_vcpu_is_write_pf(vcpu))
		l4_touch_rw((const void *)l4x_l4pfa(vcpu), 1);
	else
		l4_touch_ro((const void *)l4x_l4pfa(vcpu), 1);

	l4x_vcpu_iret(p, u, regsp, 0, 0, 1);
	/* NOTREACHED */
}

static inline void
l4x_vcpu_iret(struct proc *p, struct user *u, struct trapframe *regs,
		l4_umword_t fp1, l4_umword_t fp2, int copy_ptregs)
{
	l4_msgtag_t tag;
	l4_utcb_t *utcb;
	struct pmap *pmap = p->p_vmspace->vm_map.pmap;
	l4_vcpu_state_t *vcpu = l4x_vcpu_state(cpu_number());
	L4XV_V(n);

	L4XV_L(n);
	utcb = l4_utcb();
	L4XV_U(n);

#ifdef MULTIPROCESSOR
	l4x_stack_struct_get(p->p_addr)->vcpu = vcpu;
	l4x_stack_struct_get(p->p_addr)->l4utcb =
		l4x_stack_struct_get(o->p_addr)->l4utcb;
#endif

	/*
	 * The vCPU saved state area is not re-entrant safe.
	 * Disable interrupt delivery while playing around with it.
	 */
	l4vcpu_irq_disable(vcpu);

	if (USERMODE(regs->tf_cs, regs->tf_eflags)) {
		l4x_run_asts(regs);

		/* Create user thread on first invocation. */
		if (l4_is_invalid_cap(pmap->task)) {
			L4XV_L(n);
			l4x_vcpu_create_user_task(p);
			L4XV_U(n);
			l4x_arch_task_start_setup(p);
		}

		vcpu->user_task = pmap->task;
		vcpu->entry_sp = (l4_umword_t)regs;

		if (l4x_msgtag_fpu(cpu_number()))
			vcpu->saved_state |= L4_VCPU_F_FPU_ENABLED;
		else
			vcpu->saved_state &= ~L4_VCPU_F_FPU_ENABLED;

		splassert(IPL_NONE);
	} else {
		vcpu->r.gs = l4x_x86_utcb_get_orig_segment();
	}
	ptregs_to_vcpu(vcpu, regs);


	dbg_printf("vCPU exit: trapno=%d, err=%d, pfa=%08lx, "
			"ax=%08lx, bx=%08lx, cx=%08lx, dx=%08lx, "
			"di=%08lx, si=%08lx, ss=%08lx, sp=%08lx, "
			"bp=%08lx, flags=%08lx, eip=%08lx, fs=%08lx, "
			"gs=%08lx (%s,%s)\n",
			vcpu->r.trapno, vcpu->r.err, vcpu->r.pfa,
			vcpu->r.ax, vcpu->r.bx, vcpu->r.cx, vcpu->r.dx,
			vcpu->r.di, vcpu->r.si, vcpu->r.ss, vcpu->r.sp,
			vcpu->r.bp, vcpu->r.flags, vcpu->r.ip, vcpu->r.fs,
			vcpu->r.gs,
			copy_ptregs ? "USR" : "KRN",
			l4vcpu_is_irq_entry(vcpu) ? "IRQ" :
			l4vcpu_is_page_fault_entry(vcpu) ? "PF" : "EXC");

	vcpu->saved_state |= L4_VCPU_F_EXCEPTIONS | L4_VCPU_F_PAGE_FAULTS;

	/*
	 * Resume from vCPU event.
	 * Map fp1 to the target's task vm space as fp2, if necessary.
	 */
	while( /* CONSTCOND */ 1 ) {
		tag = l4_thread_vcpu_resume_start_u(utcb);

		if (fp1)
			l4_sndfpage_add_u((l4_fpage_t)fp2, fp1, &tag, utcb);

		tag = l4_thread_vcpu_resume_commit_u(L4_INVALID_CAP, tag, utcb);

		if (l4_ipc_error(tag, utcb) == L4_IPC_SEMAPFAILED)
			l4x_evict_mem(fp2);
		else {
			LOG_printf("l4x: resume returned: %d\n",
					l4_ipc_error(tag, utcb));
			enter_kdebug("IRET returned");
			while( /* CONSTCOND */ 1 ) ;
			/* NOTREACHED */
		}
	}
}

/*
 * Main entry point for asynchrnonous vCPU events.
 */
void
l4x_vcpu_entry(void)
{
	extern void trap(struct trapframe *frame);
	extern void syscall(struct trapframe *);

	struct proc *p;
	struct user *u;
	struct trapframe kernel_tf;		/* kernel mode frame */
	struct trapframe *regsp;
	l4_vcpu_state_t *vcpu;
	int l4x_vcpu_was_syscall = 0;
	L4XV_V(n);

	L4XV_L(n);
	vcpu = l4x_vcpu_state(cpu_number());
	L4XV_U(n);

	vcpu->state = L4_VCPU_F_EXCEPTIONS | L4_VCPU_F_PAGE_FAULTS;

	l4x_vcpu_entry_sanity(vcpu);

	p = curproc;			/* current thread */
	u = p->p_addr;			/* current thread's UAREA */

	/*
	 * Setup current trapframe.
	 * => came from userland: curproc's tf
	 * => came from kernel:   tf on stack
	 */
	if (l4x_vcpu_is_user(vcpu)) {
		regsp = p->p_md.md_regs;
		l4x_vcpu_entry_user_arch();
	} else {
		regsp = &kernel_tf;
	}

	/* save registers and pagefault address */
	vcpu_to_ptregs(vcpu, regsp);
	if (l4vcpu_is_page_fault_entry(vcpu))
		p->p_md.md_pfa = l4x_l4pfa(vcpu);	/* read with rcr2() */

	dbg_printf("vCPU entry: trapno=%d, err=%d, pfa=%08lx, "
			"ax=%08lx, bx=%08lx, cx=%08lx, dx=%08lx, "
			"di=%08lx, si=%08lx, ss=%08lx, sp=%08lx, "
			"bp=%08lx, flags=%08lx, eip=%08lx, fs=%08lx, "
			"gs=%08lx (%s,%s)\n",
			vcpu->r.trapno, vcpu->r.err, vcpu->r.pfa,
			vcpu->r.ax, vcpu->r.bx, vcpu->r.cx, vcpu->r.dx,
			vcpu->r.di, vcpu->r.si, vcpu->r.ss, vcpu->r.sp,
			vcpu->r.bp, vcpu->r.flags, vcpu->r.ip, vcpu->r.fs,
			vcpu->r.gs,
			vcpu->saved_state & L4_VCPU_F_USER_MODE ? "USR" : "KRN",
			l4vcpu_is_irq_entry(vcpu) ? "IRQ" :
			l4vcpu_is_page_fault_entry(vcpu) ? "PF" : "EXC");

	/* handle IRQs */
	if (l4vcpu_is_irq_entry(vcpu)) {
		l4x_vcpu_handle_irq(vcpu, regsp);	/* enables IRQs */
		l4x_vcpu_iret(p, u, regsp, 0, 0, 1);
		/* NOTREACHED */
	}

	/* flag syscalls for later */
	if (l4x_vcpu_is_syscall(vcpu))
		l4x_vcpu_was_syscall = 1;

	/*
	 * BIG RED LINE: Do not touch vCPU saved state below here!
	 */
	if (vcpu->saved_state & L4_VCPU_F_IRQ)
		enable_intr();

	/* Special page fault handling for kernel addresses other than KVA. */
	if ((regsp->tf_trapno == 0xe) &&
	    KERNELMODE(regsp->tf_cs, regsp->tf_eflags) &&
	    ((rcr2() < KVA_START) || (rcr2() > L4LX_USER_KERN_AREA_END))) {
		l4x_handle_kernel_pf(vcpu, p, u, regsp);
		/* NOTREACHED */
	}

	/* Special page fault handling for user mode. */
	if ((regsp->tf_trapno == 0xe) &&
	    USERMODE(regsp->tf_cs, regsp->tf_eflags)) {
		l4x_handle_user_pf(vcpu, p, u, regsp);
	}

	/* handle syscalls */
	if (l4x_vcpu_was_syscall) {
		dbg_printf("%s: Executing syscall: %s (%d) for %d\n", __func__,
				syscallnames[regsp->tf_eax], regsp->tf_eax,
				curproc->p_pid);

		regsp->tf_eip += 2;	/* return behind "int 0x80" */
		regsp->tf_err = 2;	/* size of instruction for restart */
		syscall(regsp);
		l4x_vcpu_iret(p, u, regsp, 0, 0, 1);
		/* NOTREACHED */
	}

	regsp->tf_trapno = vcpu2trapno[regsp->tf_trapno];
	dbg_printf("%s: Executing trap: %s (%d) [%s]\n", __func__,
			trap_type[regsp->tf_trapno], regsp->tf_trapno,
			USERMODE(regsp->tf_cs, regsp->tf_eflags) ? "USR" : "KRN");
#if NNPX > 0
	extern int (*npxdna_func)(struct cpu_info *);	/* isa/npx.c */
	/* Treat FPU traps special */
	if ((regsp->tf_trapno == T_DNA) &&
	    (npxdna_func(curcpu()) != 0)) {
		l4x_vcpu_iret(p, u, regsp, 0, 0, 1);
		/* NOTREACHED */
	}
#endif
	trap(regsp);	/* Generic OpenBSD trap handler. */

	l4x_vcpu_iret(p, u, regsp, 0, 0, USERMODE(regsp->tf_cs, regsp->tf_eflags));
	/* NOTREACHED */
}
