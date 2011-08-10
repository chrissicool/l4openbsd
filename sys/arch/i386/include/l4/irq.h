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

#ifndef __L4_IRQ_H
#define __L4_IRQ_H

#include <machine/frame.h>

#include <l4/sys/types.h>

unsigned int do_IRQ(int irq, struct trapframe *regs);
int l4x_register_irq(l4_cap_idx_t irqcap);
int l4x_register_irq_fixed(int irq, l4_cap_idx_t irqcap);
l4_cap_idx_t l4x_have_irqcap(int irq);


#endif /* __L4_IRQ_H */
