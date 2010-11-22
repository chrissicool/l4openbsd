/*
 * This file has API specific definitions which don't fit anywhere else
 * (or are too generic).
 *
 */
#ifndef __L4_ASM__API_L4ENV__API_H__
#define __L4_ASM__API_L4ENV__API_H__

/* from arch/l4/kernel/main.c */
extern unsigned long l4x_vmalloc_memory_start;

unsigned long l4x_virt_to_phys(volatile void * address);
void * l4x_phys_to_virt(unsigned long address);

#endif /* ! __L4_ASM__API_L4ENV__API_H__ */
