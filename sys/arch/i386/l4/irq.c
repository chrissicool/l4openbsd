/*
 * IRQ functions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/proc.h>

#include <uvm/uvm_extern.h>

#include <machine/frame.h>
#include <machine/segments.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/i8259.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <machine/l4/vcpu.h>
#include <machine/l4/irq.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/l4lxapi/irq.h>

#include <l4/sys/types.h>
#include <l4/sys/consts.h>
#include <l4/log/log.h>

#define NR_REQUESTABLE			256

/*
 * XXX trap()  should be in a header file.
 *     Unfortunately, we get a mess of #includes if in trap.h.
 */
extern void trap(struct trapframe *frame);

static void init_array(void);

static struct rwlock irq_lock;
static l4_cap_idx_t caps[NR_REQUESTABLE];
static int init_done = 0;

struct intrhand *l4x_intrhand[NR_REQUESTABLE];
u_int32_t l4x_pending[NR_REQUESTABLE / (sizeof(u_int32_t) * 8)];

/*
 * Recurse into all interrupts pending on the new lower IPL.
 * We do _not_ actually lower the SPL here! See _SPLX for that.
 */
void
l4x_spllower(void)
{
	struct intrhand *q;
	int i, irq, pending;

	/*
	 * XXX hshoexer:  Proof-of-concept, still needs rewrite.
	 */

	disable_intr();
	for (i = 0; i < 8; i++) {
		if (l4x_pending[i] == 0)
			continue;

		irq = ffs(l4x_pending[i]) - 1;
		irq += i * 32;
#if 0
		if (irq != 0 && irq != 8 && irq != 16)
			LOG_printf("%s: irq %d l4x_pending[%d] = 0x%08lx\n",
			    __func__, irq, i, l4x_pending[i]);
#endif

		q = l4x_intrhand[irq];
		if (q == NULL)
			panic("%s: no handler for irq %d", __func__, irq);
#if 0
		if (irq != 0 && irq != 8 && irq != 16)
			LOG_printf("%s: pending irq %d %x at %x\n", __func__,
			    irq, q->ih_level, lapic_tpr);
#endif
		if (q->ih_level <= lapic_tpr)
			continue;

		l4x_pending[i] &= ~(1 << (irq % 32));
		l4x_recurse_irq_handlers(irq);
#if 0
		if (irq != 0 && irq != 8 && irq != 16)
			LOG_printf("%s: irq %d cleared, l4x_pending[%d] = "
			    "0x%08lx\n", __func__, irq, i, l4x_pending[i]);
#endif
	}
	enable_intr();	/* XXX hshoexer */

retry:
	disable_intr();
	if ((pending = curcpu()->ci_ipending & IUNMASK(lapic_tpr)) != 0) {
		enable_intr();

		irq = ffs(pending) - 1;

#if 0
		if (irq != SIR_CLOCK)
			LOG_printf("%s: pending softirq %d at %x\n", __func__,
			    irq, lapic_tpr);
#endif

		if (!(curcpu()->ci_ipending & (1 << irq)))
			goto retry;

		disable_intr();
		i386_atomic_clearbits_l(&curcpu()->ci_ipending, (1 << irq));
		enable_intr();

		switch (irq) {
		case SIR_TTY:
			l4x_exec_softintr(IPL_SOFTTTY);
			break;
		case SIR_NET:
			l4x_exec_softintr(IPL_SOFTNET);
			break;
		case SIR_CLOCK:
			l4x_exec_softintr(IPL_SOFTCLOCK);
			break;
		default:
			panic("l4x_spllower: unknown softint %d", irq);
		}

		goto retry;
	}
	enable_intr();
}

/*
 * Run asynchronous system traps, if necessary.
 * Needs to be called with IRQ events disabled!
 */
void
l4x_run_asts(struct trapframe *tf)
{
	while (curproc && curproc->p_md.md_astpending) {
		i386_atomic_testset_i(&curproc->p_md.md_astpending, 0);
		enable_intr();
		tf->tf_trapno = T_ASTFLT;
		trap(tf);
		disable_intr();
	}
}

/*
 * Handle a pending IRQ event.
 * => Needs to be called with IRQ events disabled on t.
 * => Enables IRQ event delivery on t. This is recursive. XXX hshoexer:  ?
*/
void
l4x_vcpu_handle_irq(l4_vcpu_state_t *t, struct trapframe *regs)
{
	struct intrhand *q;
	int level, irq = t->i.label >> 2;

#if 0
	if (irq != 0 && irq != 8 && irq != 16)
		LOG_printf("%s: irq %d\n", __func__, irq);
#endif

	if (irq >= NR_REQUESTABLE)
		panic("%s: bogus irq %d\n", irq);

	regs->tf_err = 0;
	regs->tf_trapno = T_ASTFLT;

	/* Count number of interrupts. XXX hshoexer maybe somewhere else. */
	uvmexp.intrs++;

	q = l4x_intrhand[irq];
	if (q == NULL)
		panic("%s: no handler for L4 interrupt %d", __func__, irq);
	level = q->ih_level;

	/* Check current splx(9) level */
	if (level <= lapic_tpr) {
#if 0
		if (irq != 0 && irq != 8 && irq != 16)
			LOG_printf("%s: blocking irq %d/%x at %x\n", __func__,
			    irq, level, lapic_tpr);
#endif
		l4x_pending[irq >> 5] |= (1 << (irq % 32));
#if 0
		if (irq != 0 && irq != 8 && irq != 16)
			LOG_printf("%s: l4x_pending[%d] 0x%08lx\n", __func__,
			    irq >> 5, l4x_pending[irq >> 5]);
#endif
		/* XXX hshoexer:  enable_intr()? */
		return;
	}

	l4x_run_irq_handlers(irq, regs);

	return;
}

int
l4x_run_irq_handlers(int irq, struct trapframe *regs)
{
	struct intrhand  *q;
	int level, s, r, result = 0;

	if (irq >= NR_REQUESTABLE)
		panic("%s: bogus irq %d\n", irq);

	q = l4x_intrhand[irq];
	if (q == NULL)		/* XXX hshoexer */
		panic("%s: no hander for L4 interrupt %d", __func__,
		    irq);
	level = q->ih_level;

	s = splraise(level);

	/* XXX hshoexer:  Also on recurse? */
	enable_intr();

	i386_atomic_inc_i(&curcpu()->ci_idepth);

	q = l4x_intrhand[irq];
	if (q == NULL)
		panic("%s: no handler for L4 interrupt %d", __func__, irq);

#if 0
	if (irq != 0 && irq != 8 && irq != 16)
		LOG_printf("%s: irq %d level %x handler %p\n", __func__, irq,
		    level, q);
#endif

	/* NULL means framepointer */
	if (q->ih_arg == NULL)
		r = (*q->ih_fun)(regs);
	else
		r = (*q->ih_fun)(q->ih_arg);
	if (r != 0)
		q->ih_count.ec_count++;

	result |= r;

	i386_atomic_dec_i(&curcpu()->ci_idepth);

	/* Ack currently handled IRQ. */
	l4lx_irq_dev_eoi(irq);

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

	if (irq >= NR_REQUESTABLE)
		return -1;

	rw_enter_write(&irq_lock);
	s = splhigh();

	if (l4_is_invalid_cap(caps[irq])) {
		caps[irq] = irqcap;
	} else {
		/* IRQ already registered. */
		irq = -1;
	}

	splx(s);
	rw_exit_write(&irq_lock);

	return irq;
}

int
l4x_register_irq(l4_cap_idx_t irqcap)
{
	int i, irq = -1;
	int s;

	if (!init_done)
		init_array();

	rw_enter_write(&irq_lock);
	s = splhigh();

	/* Reserve ICU_LEN IRQs for fixed interrupts, eg. ISA */
	for (i = ICU_LEN; i < NR_REQUESTABLE; ++i) {
		if (l4_is_invalid_cap(caps[i])) {
			caps[i] = irqcap;
			irq = i;
			break;
		}
	}

	splx(s);
	rw_exit_write(&irq_lock);

	return irq;
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

void *
l4x_intr_establish(int irq, int type, int level, int (*ih_fun)(void *),
    void *ih_arg, const char *ih_what)
{
	struct intrhand *ih;

	/* XXX hshoexer */
	extern void intr_calculatemasks(void);
	extern int fakeintr(void *);
	static struct intrhand fakehand = {fakeintr};

	/* no point in sleeping unless someone can free memory. */
	ih = malloc(sizeof *ih, M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL) {
		printf("%s: l4x_intr_establish: can't malloc handler info\n",
		    ih_what);
		return (NULL);
	}

	if (l4x_intrhand[irq] != NULL)
		panic("%s: cannot share interrupt %d", __func__, irq);

	/*
	 * Actually install a fake handler momentarily, since we might be doing
	 * this with interrupts enabled and don't want the real routine called
	 * until masking is set up.
	 *
	 * Register with IO server when establishing first handler.
	 */
	fakehand.ih_level = level;
	l4x_intrhand[irq] = &fakehand;
	if (!l4lx_irq_dev_startup(irq)) {
		panic("l4x_intr_establish: l4lx_irq_dev_startup() "
		    "failed");
		free(ih, M_DEVBUF);
		return (NULL);
	}

	/*
	 * Poke the real handler in now.
	 */
	ih->ih_fun = ih_fun;
	ih->ih_arg = ih_arg;
	ih->ih_next = NULL;
	ih->ih_level = level;
	ih->ih_irq = irq;
	evcount_attach(&ih->ih_count, ih_what, (void *)&ih->ih_irq,
	    &evcount_intr);
	l4x_intrhand[irq] = ih;

	/* calculate imask[] and iunmask[] for softinterrupts. */
	intr_calculatemasks();	/* XXX only needed once? */

	return (ih);
}
