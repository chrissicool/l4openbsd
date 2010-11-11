/*
 * IRQ functions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/proc.h>

#include <uvm/uvm_extern.h>

#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/i8259.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <machine/l4/vcpu.h>
#include <machine/l4/irq.h>
#include <machine/l4/l4lxapi/irq.h>

#include <l4/sys/types.h>
#include <l4/sys/consts.h>

#define NR_REQUESTABLE			256

static int handle_irq(int irq, struct trapframe *regs);
static inline int run_irq_handlers(int irq);
static void init_array(void);

static struct trapframe *irq_regs;
static struct rwlock irq_lock;
static l4_cap_idx_t caps[NR_REQUESTABLE];
static int init_done = 0;

/* On x86 all hardware interrupts end up in one of these two handler lists. */
extern struct intrhand *intrhand[ICU_LEN];	/* (E)ISA */
extern int iminlevel[ICU_LEN], imaxlevel[ICU_LEN];
#if NIOAPIC > 0
#error IOAPIC support not yet implemented!
//extern struct intrhand *apic_intrhand[256];	/*  APIC  */
#endif

/*
 * Recurse into all pending interrupts, when lowering the SPL.
 * We do _not_ actually lower the SPL here!
 */
void
l4x_spllower(void)
{
	struct proc *p = curproc;
	struct trapframe *tf;
	int s;

	/* prepare trapframe */
	tf = p->p_md.md_regs;
	tf->tf_cs |= (SEL_KPL & SEL_RPL);	/* kernel */
	tf->tf_eflags = 0;

	for (s = NIPL; s > NIPL; s--) {
		if (MAKEIPL(s) > lapic_tpr) {
			if (curcpu()->ci_ipending & iunmask[s]) {
				run_irq_handlers(s);
				curcpu()->ci_ipending |= ~(1 << s);
			}
		} else {
			break;
		}
	}
}

static inline struct trapframe *
set_irq_regs(struct trapframe *new_regs)
{
	struct trapframe *old_regs, **pp_regs = &irq_regs;

	old_regs = *pp_regs;
	*pp_regs = new_regs;
	return old_regs;
}

#include <l4/log/log.h>

void l4x_vcpu_handle_irq(l4_vcpu_state_t *t, struct trapframe *regs)
{
	int irq = t->i.label >> 2;

#ifdef MULTIPROCESSOR	/* unported */
	if (irq & L4X_VCPU_IRQ_IPI)
		l4x_vcpu_handle_ipi(regs);
	else
#endif
	{
		do_IRQ(irq, regs);

#ifdef MULTIPROCESSOR	/* unported */
		if (smp_processor_id() == 0 && irq == TIMER_IRQ)
			l4x_smp_broadcast_timer();
#endif
	}
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int do_IRQ(int irq, struct trapframe *regs)
{
	struct trapframe *old_regs = set_irq_regs(regs);

	cpu_unidle();

	if (!handle_irq(irq, regs)) {
		//ack_APIC_irq();

		LOG_printf("%s: %d.%d: Error processing interrupt!\n",
				__func__, cpu_number(), irq);
	}

	set_irq_regs(old_regs);
	return 1;
}

static inline int
run_irq_handlers(int irq)
{
	extern void isa_strayintr(int irq);
	struct intrhand **p, *q;
	int result = 0;

	LOG_printf("%s: cl: Running IRQ%d\n", __func__, irq);

	curcpu()->ci_idepth++;

	for (p = &intrhand[irq]; (q = *p) != NULL; p = &q->ih_next) {
		result |= (*q->ih_fun)(q->ih_arg);
		q->ih_count.ec_count++;
	}
	if (intrhand[irq] == NULL)
		isa_strayintr(irq);

	curcpu()->ci_idepth--;

	/* Ack current handled IRQ. */
	l4lx_irq_dev_eoi(irq);

	return result;
}


/*
 * Note, we only run hardware IRQs here.
 * The code is mainly taken from INTRSTUB() at vector.s
 * Interrupt exit code is mainly inspired from Xdoreti() at icu.s
 */
static int
handle_irq(int irq, struct trapframe *regs)
{
	int s, result = 0;

	/* Sanety checks. */
	if (irq > ICU_LEN)
		return 0;

	/* Count number of interrupts. */
	uvmexp.intrs++;

	/* Check current splx(9) level */
	if (iminlevel[irq] < lapic_tpr) {
		curcpu()->ci_ipending |= iunmask[irq];
		return 1; /* handled */
	}

	s = splraise(imaxlevel[irq]);
	result = run_irq_handlers(irq);
	splx(s);

	return result;
}

static void
init_array(void)
{
	int i;

	rw_init(&irq_lock, "l4x-irqlock");

	for (i = 0; i < NR_REQUESTABLE; i++)
		caps[i] = L4_INVALID_CAP;

	init_done = 1;
}

int
l4x_register_irq_fixed(int irq, l4_cap_idx_t irqcap)
{
	int s;

	if (!init_done)
		init_array();

	if (irq > NR_REQUESTABLE)
		return -1;

	rw_enter_write(&irq_lock);
	s = splhigh();

	if (l4_is_invalid_cap(caps[irq])) {
		caps[irq] = irqcap;
	} else {
		/* IRQ already registered. */
		return -1;
	}

	splx(s);
	rw_exit_write(&irq_lock);

	return irq;
}

int
l4x_register_irq(l4_cap_idx_t irqcap)
{
	int i, ret = -1;
	int s;

	if (!init_done)
		init_array();

	rw_enter_write(&irq_lock);
	s = splhigh();

	for (i = 0; i < NR_REQUESTABLE; ++i) {
		if (l4_is_invalid_cap(caps[i])) {
			caps[i] = irqcap;
			break;
		}
	}

	splx(s);
	rw_exit_write(&irq_lock);

	return ret;
}

l4_cap_idx_t
l4x_have_irqcap(int irq)
{
	if (!init_done)
		init_array();

	if (irq < NR_REQUESTABLE)
		return caps[irq];

	return L4_INVALID_CAP;
}
