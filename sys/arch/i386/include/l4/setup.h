#ifndef __ASM_L4__GENERIC__SETUP_H__
#define __ASM_L4__GENERIC__SETUP_H__

#include <l4/sys/kip.h>
#include <l4/re/c/dataspace.h>

extern l4_kernel_info_t *l4lx_kinfo;
extern unsigned int l4x_kernel_taskno;

/* main memory parameters from setupmem.c */
extern l4re_ds_t l4x_ds_mainmem;
extern l4re_ds_t l4x_ds_isa_dma;
extern unsigned long l4x_mainmem_size;

void l4x_memory_setup(char **cmdl);

unsigned long l4x_get_isa_dma_memory_end(void);

void l4x_register_pointer_section(void *p_in_addr,
		int allow_noncontig, const char *tag);
void l4x_register_region(const l4re_ds_t ds, void *start,
		int allow_noncontig, const char *tag);

//void l4x_free_initrd_mem(void);
//void l4x_load_initrd(char *command_line);
//void l4x_setup_threads(void);
//void l4x_l4io_init(void);

//void l4x_setup_thread_stack(void);

//void l4x_prepare_irq_thread(struct thread_info *ti, unsigned _cpu);

void l4x_exit_l4linux(void);

//void l4x_thread_set_pc(l4_cap_idx_t thread, void *pc);

//int atexit(void (*f)(void));

#endif /* ! __ASM_L4__GENERIC__SETUP_H__ */
