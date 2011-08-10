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
#define FIRST_REQUESTABLE		128

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

		q = l4x_intrhand[irq];

		/*
		 * XXX hshoexer: If a handler can be disestablished()
		 * while an interrupt is pending for it, q == NULL would
		 * be ok.
		 */
		if (q == NULL)
			panic("%s: no handler for irq %d", __func__, irq);
		if (q->ih_level <= lapic_tpr)
			continue;

		l4x_pending[i] &= ~(1 << (irq % 32));
		l4x_recurse_irq_handlers(irq);
	}
	enable_intr();	/* XXX hshoexer */

retry:
	disable_intr();
	if ((pending = curcpu()->ci_ipending & IUNMASK(lapic_tpr)) != 0) {
		enable_intr();

		irq = ffs(pending) - 1;

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
		l4x_pending[irq >> 5] |= (1 << (irq % 32));

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
	l4lx_irq_dev_eoi(q);

	splx(s);

	return result;
}

static void
init_array(void)
{
	int i;

	rw_init(&irq_lock, "l4x-irqlock");

	for (i = 0; i < NR_REQUESTABLE; i++) {
		caps[i] = L4_INVALID_CAP;
		l4x_intrhand[i] = NULL;
	}

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
	for (i = FIRST_REQUESTABLE; i < NR_REQUESTABLE; ++i) {
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

	/*
	 * Poke the real handler in now.
	 */
	ih->ih_fun = ih_fun;
	ih->ih_arg = ih_arg;
	ih->ih_next = NULL;
	ih->ih_level = level;
	ih->ih_irq = irq;	/* XXX CEH: Probably bogus, don't use! */
	ih->ih_vec = irq;
	if (!l4lx_irq_dev_startup(ih)) {
		panic("l4x_intr_establish: l4lx_irq_dev_startup() "
		    "failed");
		free(ih, M_DEVBUF);
		return (NULL);
	}

	evcount_attach(&ih->ih_count, ih_what, (void *)&ih->ih_vec,
	    &evcount_intr);
	l4x_intrhand[irq] = ih;

	/* calculate imask[] and iunmask[] for softinterrupts. */
	intr_calculatemasks();	/* XXX only needed once? */

	return (ih);
}

void
l4x_intr_set(int irq, struct intrhand *ih)
{
	if (l4x_intrhand[irq])
		panic("%s: Cannot shore irq %d\n", __func__, irq);
	if (ih->ih_vec != irq || l4_is_invalid_cap(ih->ih_cap))
		panic("%s: Bad interupt handle in for %d", __func__, irq);
	l4x_intrhand[irq] = ih;
}

void
l4x_intr_clear(int irq)
{
	l4x_intrhand[irq] = NULL;
}
