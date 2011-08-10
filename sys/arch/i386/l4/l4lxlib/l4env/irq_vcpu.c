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
 * Generic interrupt library for vCPU.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/kernel.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <machine/l4/setup.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/irq.h>
#include <machine/l4/l4lxapi/irq.h>
#include <machine/l4/l4lxapi/thread.h>

#include <l4/io/io.h>
#include <l4/log/log.h>
#include <l4/re/c/namespace.h>
#include <l4/re/env.h>
#include <l4/sys/debugger.h>
#include <l4/sys/factory.h>
#include <l4/sys/ipc.h>
#include <l4/sys/irq.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/types.h>
#include <l4/sys/utcb.h>

#define TIMER_IRQ	0
#define STAT_IRQ	8

#define STAT_HZ		128

struct tirq_arg {
	int		hz;
	l4_cap_idx_t	irq_cap;	
};

static struct tirq_arg timer_arg; 
static struct tirq_arg stat_arg; 

#define d_printf(format, args...)  printf(format , ## args)
//#define dd_printf(format, args...) do { printf(format , ## args); } while (0)
#define dd_printf(format, args...) do { } while (0)

static unsigned int l4lx_irq_dev_startup_timer(int, struct intrhand *ih,
    struct tirq_arg *);
void L4_CV timer_irq_thread(void *data);

/*
 * Return the priority of an interrupt thread.
 */
#ifdef notyet
int
l4lx_irq_prio_get(unsigned int irq)
{
	if (irq == 0)
		return CONFIG_L4_PRIO_IRQ_BASE + 1;
	if (irq < NR_IRQS)
		return CONFIG_L4_PRIO_IRQ_BASE;

	enter_kdebug("l4lx_irq_prio_get: wrong IRQ!");
	return -1;
}
#endif

#ifdef notyet
int l4lx_irq_set_type(unsigned int irq, unsigned int type)
{
#ifdef ARCH_x86
	extern struct irq_chip l4x_irq_dev_chip;
#endif
	struct l4x_irq_desc_private *p;

	if (unlikely(irq >= NR_IRQS))
		return -1;

	p = get_irq_chip_data(irq);
	if (!p)
		return -1;

	printf("L4IRQ: set irq type of %u to %x\n", irq, type);
	switch (type & IRQF_TRIGGER_MASK) {
		case IRQF_TRIGGER_RISING:
			p->trigger = L4_IRQ_F_POS_EDGE;
#ifdef ARCH_x86
			set_irq_chip_and_handler_name(irq, &l4x_irq_dev_chip, handle_edge_irq, "edge");
#endif
			break;
		case IRQF_TRIGGER_FALLING:
			p->trigger = L4_IRQ_F_NEG_EDGE;
#ifdef ARCH_x86
			set_irq_chip_and_handler_name(irq, &l4x_irq_dev_chip, handle_edge_irq, "edge");
#endif
			break;
		case IRQF_TRIGGER_HIGH:
			p->trigger = L4_IRQ_F_LEVEL_HIGH;
#ifdef ARCH_x86
			set_irq_chip_and_handler_name(irq, &l4x_irq_dev_chip, handle_fasteoi_irq, "fasteoi");
#endif
			break;
		case IRQF_TRIGGER_LOW:
			p->trigger = L4_IRQ_F_LEVEL_LOW;
#ifdef ARCH_x86
			set_irq_chip_and_handler_name(irq, &l4x_irq_dev_chip, handle_fasteoi_irq, "fasteoi");
#endif
			break;
		default:
			p->trigger = L4_IRQ_F_NONE;
			break;
	};

	return 0;
}
#endif	/* notyet */

static inline void attach_to_irq(struct intrhand *ih)
{
	long	ret;
	int	irq = ih->ih_vec;
	L4XV_V(flags);

	L4XV_L(flags);
	if ((ret  = l4_error(l4_irq_attach(ih->ih_cap, irq << 2,
	    l4x_cpu_thread_get_cap(cpu_number())))))
		printf("%s: can't register to irq %u: return=%ld\n",
		    __func__, irq, ret);
	L4XV_U(flags);
}

static void detach_from_interrupt(struct intrhand *ih)
{
	L4XV_V(flags);

	L4XV_L(flags);
	if (l4_error(l4_irq_detach(ih->ih_cap)))
		dd_printf("%02d: Unable to detach from IRQ\n", ih->ih_vec);
	L4XV_U(flags);
}

#ifdef notyet
void l4lx_irq_init(void)
{
	l4lx_irq_max = NR_IRQS;
	printf("%s: l4lx_irq_max = %d\n", __func__, l4lx_irq_max);
}
#endif

void L4_CV
timer_irq_thread(void *data)
{
	l4_timeout_t to;
	l4_kernel_clock_t pint;
	l4_utcb_t *u = l4_utcb();
	struct tirq_arg *arg = (struct tirq_arg *)data;

	LOG_printf("%s: Starting %d Hz timer IRQ thread.\n", __func__, arg->hz);

	pint = l4lx_kinfo->clock;
	for (;;) {
		pint += 1000000 / arg->hz;

		if (pint > l4lx_kinfo->clock) {
			l4_rcv_timeout(l4_timeout_abs_u(pint, 1, u), &to);
			l4_ipc_receive(L4_INVALID_CAP, u, to);
		} else {
			//printf("I'm too slow (%lld vs. %lld [%lld])!\n", l4lx_kinfo->clock, pint, l4lx_kinfo->clock - pint);
		}

		if (l4_error(l4_irq_trigger(arg->irq_cap)) != -1)
			LOG_printf("IRQ timer trigger failed\n");
	}
} /* timer_irq_thread */

unsigned int
l4lx_irq_dev_startup_timer(int freq, struct intrhand *ih, struct tirq_arg *arg)
{
	int cpu = cpu_number();
	l4_msgtag_t res;
	l4lx_thread_t timer_thread;
	int irq = ih->ih_vec;
	L4XV_V(timer_f);

	L4XV_L(timer_f);
	ih->ih_cap = l4x_cap_alloc();
	if (l4_is_invalid_cap(ih->ih_cap)) {
		printf("Cap alloc failed for timer\n");
		l4x_exit_l4linux();
		/* NOTREACHED */
		return 0;
	}

	res = l4_factory_create_irq(l4re_env()->factory, ih->ih_cap);
	if (l4_error(res)) {
		printf("Failed to create timer IRQ\n");
		l4x_cap_free(ih->ih_cap);
		L4XV_U(timer_f);
		l4x_exit_l4linux();
		/* NOTREACHED */
		return 0;
	}
	L4XV_U(timer_f);

#ifdef L4_DEBUG_REGISTER_NAMES
	char name[15];
	snprintf(name, 15, "%dhz.t.%d", freq, irq);

	L4XV_L(timer_f);
	l4_debugger_set_object_name(ih->ih_cap, name);
	L4XV_U(timer_f);
#endif

	if (l4x_register_irq_fixed(irq, ih->ih_cap) == -1) {
		printf("Error registering timer irq %d!", irq);
		l4x_exit_l4linux();
		/* NOTREACHED */
		return 0;
	}

	arg->hz = freq;
	arg->irq_cap = ih->ih_cap;

	L4XV_L(timer_f);
	timer_thread = l4lx_thread_create
			(timer_irq_thread,	      /* thread function */
	                 cpu,                         /* cpu */
			 NULL,			      /* stack */
			 arg, sizeof(*arg),           /* data */
			 /* XXX hshoexer */
			 -1,                          /* prio */
			 0,                           /* vcpup */
#ifdef L4_DEBUG_REGISTER_NAMES
			 name);			      /* name */
#else
			 "");
#endif

	if (!l4lx_thread_is_valid(timer_thread)) {
		printf("Error creating timer thread!");
		l4x_exit_l4linux();
		/* NOTREACHED */
		return 0;
	}
	L4XV_U(timer_f);

	l4lx_irq_dev_enable(ih);
	return 1;
}

static void
l4lx_irq_dev_shutdown_timer(struct intrhand *ih)
{
	// No one is calling this, right? Why?
	ih = ih;
}

unsigned int
l4lx_irq_dev_startup(struct intrhand *ih)
{
	int	irq = ih->ih_vec;

	if (irq == TIMER_IRQ)	/* The Timer interrupt. */
		return l4lx_irq_dev_startup_timer(hz, ih, &timer_arg);
	if (irq == STAT_IRQ)
		return l4lx_irq_dev_startup_timer(STAT_HZ, ih, &stat_arg);

	/* First test whether a capability has been registered with
	 * this IRQ number */
	ih->ih_cap = l4x_have_irqcap(irq);
	if (l4_is_invalid_cap(ih->ih_cap)) {
		/* No, get IRQ from IO service */
		L4XV_V(irq_f);

		L4XV_L(irq_f);
		ih->ih_cap = l4x_cap_alloc();
		if (l4_is_invalid_cap(ih->ih_cap)) {
			LOG_printf("l4lx_irq_dev_startup: did not get valid "
			    "capability for irq %d\n", irq);
			L4XV_U(irq_f);
			return 0;
		}
		if (l4io_request_irq(irq, ih->ih_cap)) {
			/* "reset" handler ... */
			//irq_desc[irq].chip = &no_irq_type;
			/* ... and bail out  */
			LOG_printf("l4lx_irq_dev_startup: did not get irq %d\n",
			    irq);
			L4XV_U(irq_f);
			return 0;
		}
		L4XV_U(irq_f);
	}
	l4lx_irq_dev_enable(ih);
	return 1;
}

void
l4lx_irq_dev_shutdown(struct intrhand *ih)
{
	int	irq = ih->ih_vec;

	if (irq == TIMER_IRQ || irq == STAT_IRQ) {
		l4lx_irq_dev_shutdown_timer(ih);
		return;
	}

	dd_printf("%s: %u\n", __func__, irq);
	l4lx_irq_dev_disable(ih);

	if (l4_is_invalid_cap(l4x_have_irqcap(irq)))
		l4io_release_irq(irq, ih->ih_cap);
}

void
l4lx_irq_dev_enable(struct intrhand *ih)
{

	attach_to_irq(ih);
	l4lx_irq_dev_eoi(ih);
}

void
l4lx_irq_dev_disable(struct intrhand *ih)
{
	dd_printf("%s: %u\n", __func__, ih->ih_vec);
	detach_from_interrupt(ih);
}

#ifdef notyet
void
l4lx_irq_dev_ack(unsigned int irq)
{
	dd_printf("%s: %u\n", __func__, irq);
}
#endif

void
l4lx_irq_dev_mask(unsigned int irq)
{
	dd_printf("%s: %u\n", __func__, irq);
}

void
l4lx_irq_dev_unmask(unsigned int irq)
{
	dd_printf("%s: %u\n", __func__, irq);
}

#ifdef notyet
void
l4lx_irq_dev_end(unsigned int irq)
{
	dd_printf("%s: %u\n", __func__, irq);
}
#endif

void
l4lx_irq_dev_eoi(struct intrhand *ih)
{
	dd_printf("%s: %u\n", __func__, ih->ih_vec);
	L4XV_V(flags);

	L4XV_L(flags);
	l4_irq_unmask(ih->ih_cap);
	L4XV_U(flags);
}
#ifdef MULTIPROCESSOR
#ifdef notyet
static spinlock_t migrate_lock;

int
l4lx_irq_dev_set_affinity(unsigned int irq, const struct cpumask *dest)
{
        unsigned target_cpu;
	unsigned long flags;
	struct irq_desc *desc = irq_to_desc(irq);
	struct l4x_irq_desc_private *p = desc->chip_data;

	if (!p->irq_cap)
		return 0;

	if (!cpumask_intersects(dest, cpu_online_mask))
                return 1;

        target_cpu = cpumask_any_and(dest, cpu_online_mask);

	if (target_cpu == p->cpu)
		return 0;

	spin_lock_irqsave(&migrate_lock, flags);
	detach_from_interrupt(desc);

	cpumask_copy(desc->affinity, dest);
	p->cpu = target_cpu;
	attach_to_irq(desc);
	if (p->enabled);
		l4_irq_unmask(p->irq_cap);
	spin_unlock_irqrestore(&migrate_lock, flags);

	return 0;
}
#endif	/* notyet */
#endif
