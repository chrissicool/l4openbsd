#ifndef __L4_IRQ_H
#define __L4_IRQ_H

#include <machine/reg.h>

unsigned int do_IRQ(int irq, struct reg *regs);

#endif /* __L4_IRQ_H */
