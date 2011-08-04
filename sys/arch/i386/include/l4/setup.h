#ifndef __ASM_L4__GENERIC__SETUP_H__
#define __ASM_L4__GENERIC__SETUP_H__

#include <sys/types.h>

#include <machine/l4/api/config.h>

#include <l4/sys/types.h>
#include <l4/sys/kip.h>
#include <l4/sys/vcpu.h>
#include <l4/re/c/dataspace.h>

extern l4_kernel_info_t *l4lx_kinfo;
extern unsigned int l4x_kernel_taskno;
extern l4_cap_idx_t linux_server_thread_id;

/* main memory parameters from setupmem.c */
extern char *esym;	/* End of symbol table */
#define KVA_START	(round_page((unsigned long)esym))
#define PA_START	(L4LX_USER_KERN_AREA_END + round_page((unsigned long)esym) - KERNBASE)
extern l4re_ds_t l4x_ds_mainmem;
extern l4re_ds_t l4x_ds_isa_dma;
extern paddr_t l4x_main_memory_start;
extern vaddr_t l4x_kv_memory_start;
extern unsigned long l4x_mainmem_size;

void l4x_v2p_init(void);
void l4x_v2p_add_item(l4_addr_t phys, vaddr_t virt, l4_size_t size);
paddr_t l4x_virt_to_phys(volatile vaddr_t address);
vaddr_t l4x_phys_to_virt(volatile paddr_t address);
void l4x_memory_setup(char **cmdl);
paddr_t l4x_setup_kernel_ptd(l4_addr_t, size_t);
int l4x_pagein(unsigned long, unsigned long, int);

unsigned l4x_x86_utcb_get_orig_segment(void);

unsigned long l4x_get_isa_dma_memory_end(void);

void l4x_register_pointer_section(void *p_in_addr,
		int allow_noncontig, const char *tag);
void l4x_register_region(const l4re_ds_t ds, void *start,
		int allow_noncontig, const char *tag);

/* initial ramdisk */
//void l4x_free_initrd_mem(void);
//void l4x_load_initrd(char *command_line);
//void l4x_setup_threads(void);
//void l4x_l4io_init(void);

//void l4x_prepare_irq_thread(struct thread_info *ti, unsigned _cpu);

void l4x_exit_l4linux(void);

int l4x_vcpu_handle_kernel_exc(l4_vcpu_regs_t *vr);

//void l4x_thread_set_pc(l4_cap_idx_t thread, void *pc);

//int atexit(void (*f)(void));

#endif /* ! __ASM_L4__GENERIC__SETUP_H__ */
