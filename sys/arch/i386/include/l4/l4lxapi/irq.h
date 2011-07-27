/*
 * This header files defines the IRQ functions which need to be provided by
 * every implementation of this interface.
 * The functions mostly correspond to the "struct hw_interrupt_type"
 * members.
 *
 *
 */
#ifndef __ASM_L4__L4LXAPI__IRQ_H__
#define __ASM_L4__L4LXAPI__IRQ_H__

//#include <machine/l4/l4lxapi/generic/irq_gen.h>
#include <l4/sys/types.h>
#include <machine/psl.h>


/**
 * \defgroup irq Interrupt handling functionality.
 * \ingroup l4lxapi
 */

/**
 * \defgroup irq_dev Device IRQ handling functionality.
 * \ingroup irq
 */

/**
 * \brief Initialize the interrupt handling.
 * \ingroup irq
 */
void l4lx_irq_init(void);

/**
 * \brief Get defined priority of a certain interrupt thread.
 * \ingroup irq
 *
 * \param	irq	Interrupt.
 * \return	Defined priority of the interrupt thread.
 *
 * Every API implementation has to define this function which
 * returns the priority of the specific interrupt thread. This function does
 * not return the actual thread priority!
 */
//int l4lx_irq_prio_get(unsigned int irq);

/**
 * Startup of a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 * \return 1 if successful, 0 on failure.
 */
unsigned int l4lx_irq_dev_startup(struct intrhand *irq);

/**
 * \brief Shutdown of a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
void l4lx_irq_dev_shutdown(struct intrhand *irq);

/**
 * \brief Set IRQ type.
 *
 * \param irq   IRQ.
 * \param type  Type.
 *
 * \return 0 on success, negative on error
 */
//int l4lx_irq_set_type(unsigned int irq, unsigned int type);

/**
 * \brief Enable a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
void l4lx_irq_dev_enable(struct intrhand *irq);


/**
 * \brief Disable a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
void l4lx_irq_dev_disable(struct intrhand *irq);

/**
 * \brief Acknowledge (and possibly mask) a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_dev_ack(unsigned int irq);

/**
 * \brief Mask a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
void l4lx_irq_dev_mask(unsigned int irq);

/**
 * \brief Unmask a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
void l4lx_irq_dev_unmask(unsigned int irq);

/**
 * \brief Unmask a device IRQ.
 * \ingroup irq_dev
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_dev_end(unsigned int irq);

/**
 * \brief Set affinity of an IRQ.
 * \ingroup irq_dev
 *
 * \param irq   IRQ.
 * \param mask  CPU mask.
 */
//int l4lx_irq_dev_set_affinity(unsigned irq, const struct cpumask *dest);

/**
 * \brief EOI IRQ.
 * \ingroup irq_dev
 *
 * \param irq   IRQ.
 */
void l4lx_irq_dev_eoi(struct intrhand *irq);

/**
 * \defgroup irq_timer Timer interrupt functionality.
 * \ingroup  irq
 */

/**
 * \brief Enable an IRQ.
 * \ingroup irq_timer
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_timer_enable(int irq);

/**
 * \brief Disable an IRQ.
 * \ingroup irq_timer
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_timer_disable(unsigned int irq);

/**
 * \brief Acknowledge (and possibly mask) an IRQ.
 * \ingroup irq_timer
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_timer_ack(unsigned int irq);

/**
 * \brief Mask an IRQ.
 * \ingroup irq_timer
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_timer_mask(unsigned int irq);

/**
 * \brief Unmask an IRQ.
 * \ingroup irq_timer
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_timer_unmask(unsigned int irq);

/**
 * \brief Unmask an IRQ.
 * \ingroup irq_timer
 *
 * \param irq	IRQ.
 */
//void l4lx_irq_timer_end(unsigned int irq);

/**
 * \brief Set affinity of an IRQ.
 * \ingroup irq_timer
 *
 * \param irq   IRQ.
 * \param mask  CPU mask.
 */
//int l4lx_irq_timer_set_affinity(unsigned irq, const struct cpumask *dest);

#endif /* ! __ASM_L4__L4LXAPI__IRQ_H__ */
