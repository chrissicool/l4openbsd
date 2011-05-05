/*
 * Interrupt disable/enable implemented with a queue
 *
 * For deadlock reasons this file must _not_ use any Linux functionality!
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/segments.h>

#include <machine/l4/vcpu.h>
#include <machine/l4/stack_id.h>

#include <l4/sys/types.h>
#include <l4/sys/vcpu.h>
#include <l4/sys/ipc.h>
#include <l4/sys/utcb.h>

void do_vcpu_irq(l4_vcpu_state_t *v);
void l4x_srv_setup_recv_wrap(l4_utcb_t *utcb);

void
l4x_srv_setup_recv_wrap(l4_utcb_t *utcb)
{
	/*
	 * There is a "bug" in older libvcpu,
	 * which need this function to be present.
	 */
}

void l4x_global_cli(void)
{
	l4vcpu_irq_disable(l4x_stack_vcpu_state_get());
}

void l4x_global_sti(void)
{
	l4vcpu_irq_enable(l4x_stack_vcpu_state_get(), l4x_stack_utcb_get(),
			do_vcpu_irq, l4x_srv_setup_recv_wrap);
}

void do_vcpu_irq(l4_vcpu_state_t *v)
{
	struct trapframe *regs;
	struct proc *p = curproc;
	int cs;

	regs = p->p_md.md_regs;			/* current trapframe */
	cs = regs->tf_cs;
	regs->tf_cs = GSEL(GCODE_SEL, SEL_KPL);		/* fake kernel mode */
	l4x_vcpu_handle_irq(v, regs);
	regs->tf_cs = cs;
}

/*
 * Halt the current vCPU, if there is nothing to do.
 */
void
l4x_global_halt(void)
{
	l4vcpu_halt(l4x_stack_vcpu_state_get(), l4x_stack_utcb_get(),
			do_vcpu_irq, l4x_srv_setup_recv_wrap);
}
