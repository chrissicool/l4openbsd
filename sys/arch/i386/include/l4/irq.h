#ifndef __L4_IRQ_H
#define __L4_IRQ_H

#include <machine/frame.h>

#include <l4/sys/types.h>

unsigned int do_IRQ(int irq, struct trapframe *regs);
int l4x_register_irq(l4_cap_idx_t irqcap);

#endif /* __L4_IRQ_H */
