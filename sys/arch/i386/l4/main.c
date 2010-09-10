/*
 * main.c
 *
 * Main entry point for L4BSD
 */

#include "linux_compat.h"
#include <machine/cpu.h>

#include <sys/l4/l4lib.h>
// just taken from L4Linux's arch/l4/kernel/main.c
#include <l4/sys/err.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/thread.h>
#include <l4/sys/factory.h>
#include <l4/sys/scheduler.h>
#include <l4/sys/task.h>
#include <l4/sys/debugger.h>
#include <l4/sys/vcpu.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/debug.h>
#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/c/util/cap.h>
#include <l4/re/c/namespace.h>
#include <l4/re/env.h>
#include <l4/log/log.h>
#include <l4/util/cpu.h>
#include <l4/util/l4_macros.h>
#include <l4/util/kip.h>
#include <l4/util/util.h>
#include <l4/io/io.h>

/*
 * sanety check
 */
#ifdef MULTIPROCESSOR
#error We are not ready for option MULTIPROCESSOR yet!
#endif

/*
 * variables
 */
unsigned long __l4_external_resolver;
extern char __l4sys_invoke_direct[];

struct l4x_cpu_physmap_struct {
	unsigned int phys_id;
};
static struct l4x_cpu_physmap_struct l4x_cpu_physmap[MAXCPUS];

l4_kernel_info_t *l4lx_kinfo;
l4_vcpu_state_t *l4x_vcpu_states[MAXCPUS];
unsigned int  l4x_nr_cpus = 1;

struct l4x_phys_virt_mem {
	l4_addr_t phys; /* physical address */
	void    * virt; /* virtual address */
	l4_size_t size; /* size of chunk in Bytes */
};
#define L4X_PHYS_VIRT_ADDRS_MAX_ITEMS 20
//static struct l4x_phys_virt_mem l4x_phys_virt_addrs[L4X_PHYS_VIRT_ADDRS_MAX_ITEMS];
int l4x_phys_virt_addr_items;


l4_cap_idx_t linux_server_thread_id = L4_INVALID_CAP;
//l4_cap_idx_t l4x_start_thread_id = L4_INVALID_CAP;
//l4_cap_idx_t l4x_start_thread_pager_id = L4_INVALID_CAP;

/*
 * used external functions from L4
 */
L4_EXTERNAL_FUNC(LOG_printf);
L4_EXTERNAL_FUNC(l4util_kip_kernel_has_feature);
L4_EXTERNAL_FUNC(l4util_kip_kernel_abi_version);

/*
 * prototypes from others
 */
extern void start(void);	/* locore.s */
l4re_env_t *l4re_global_env;
l4_kernel_info_t *l4lx_kinfo;

/*
 * prototypes
 */
int L4_CV l4start(int argc, char **argv);

L4_CV l4_utcb_t *l4_utcb_wrap(void);

static void l4x_configuration_sanity_check(void);

static int l4x_cpu_virt_phys_map_init(void);
static inline void l4x_x86_utcb_save_orig_segment(void);
unsigned l4x_x86_utcb_get_orig_segment(void);

static void get_initial_cpu_capabilities(void);
void l4x_v2p_init(void);

/*
 * implentation
 */

int L4_CV l4start(int argc, char **argv) {

/*	l4_cap_idx_t main_id;
	l4_msgtag_t tag; */

	/* bsd.lds */
	extern char __text_start[], _etext[];
	extern char __rodata_start[], _erodata[];
	extern char __data_start[], _edata[];
	extern char __bss_start[], _end[];

	/* locore.s */
	extern int bootargc;
	extern char **bootargv;

	LOG_printf("\033[34;1m======> L4OpenBSD starting... <========\033[0m\n");
	LOG_printf("Binary name: %s\n", (*argv)?*argv:"NULL??");
	argv++;
	LOG_printf("OpenBSD kernel command line (%d args): ", argc - 1);
	while (*argv) {
		LOG_printf("%s ", *argv);
		argv++;
	}
	LOG_printf("\n");

	/*
	 * handle command line
	 */
	bootargc = argc;
	bootargv = argv;

	/*
	 * initialization
	 */
	l4x_configuration_sanity_check();

	if (l4x_cpu_virt_phys_map_init())
		return 1;

	l4x_x86_utcb_save_orig_segment();

	/* Touch areas to avoid pagefaults
	 * Data needs to be touched rw, code ro
	 */
	l4_touch_ro(&__text_start,
			(unsigned long)&_etext - (unsigned long)&__text_start);
	l4_touch_ro(&__rodata_start,
			(unsigned long)&_erodata - (unsigned long)&__rodata_start);
	l4_touch_rw(&__data_start,
			(unsigned long)&_edata - (unsigned long)&__data_start);
	l4_touch_rw(&__bss_start,
			(unsigned long)&_end - (unsigned long)&__bss_start);

	LOG_printf("Image:   Text:  %08lx - %08lx [%ldkB]\n",
			(unsigned long)&__text_start, (unsigned long)&_etext,
			((unsigned long)&_etext - (unsigned long)&__text_start) >> 10);
	LOG_printf("       ROData:  %08lx - %08lx [%ldkB]\n",
			(unsigned long)&__rodata_start, (unsigned long)&_erodata,
			((unsigned long)&_erodata - (unsigned long)&__rodata_start) >> 10);
	LOG_printf("         Data:  %08lx - %08lx [%ldkB]\n",
			(unsigned long)&__data_start, (unsigned long)&_edata,
			((unsigned long)&_edata - (unsigned long)&__data_start) >> 10);
	LOG_printf("          BSS:  %08lx - %08lx [%ldkB]\n",
			(unsigned long)&__bss_start, (unsigned long)&_end,
			((unsigned long)&_end - (unsigned long)&__bss_start) >> 10);

	/* some things from head.S */
	get_initial_cpu_capabilities();

	/* This is not the final try to set linux_server_thread_id
	 * but l4lx_thread_create needs some initial data for the return
	 * value... */
	linux_server_thread_id = l4re_env()->main_thread;

#ifdef ARCH_x86
	{
		unsigned long gs, fs;
		asm volatile("mov %%gs, %0" : "=r"(gs));
		asm volatile("mov %%fs, %0" : "=r"(fs));
		LOG_printf("gs=%lx   fs=%lx\n", gs, fs);
	}
#endif

	/* Init v2p list */
	l4x_v2p_init();

	start();	/* main OpenBSD entry point from locore.s */
	return 0;
}

/*
 * overrides L4re's default function
 */
L4_CV l4_utcb_t *l4_utcb_wrap(void)
{
#if 0
	if (l4x_utcb_check() != l4_utcb_direct())
	enter_kdebug("UTCB mismatch");
	return l4_utcb_direct();
#else
	unsigned gsvalue;
	__asm __volatile ("mov %%gs, %0": "=r" (gsvalue));
	//if (gsvalue == 0x43 || gsvalue == 7)
		return l4_utcb_direct();
	//return 42; /* XXX l4x_stack_utcb_get(); */
#endif
}

static void l4x_configuration_sanity_check(void) {
	char *required_kernel_features[] = {
		"io_prot", "segments",
	};
	unsigned long required_kernel_abi_version = 0;
	int i = 0;

	for (i = 0; i < sizeof(required_kernel_features)
			/ sizeof(required_kernel_features[0]); i++) {
		if (!l4util_kip_kernel_has_feature(l4lx_kinfo, required_kernel_features[i])) {
			LOG_printf("The running microkernel does not have the\n"
					"      %s\n"
					"feature enabled!\n",
					required_kernel_features[i]);
			while (1)
				enter_kdebug("Microkernel feature missing!");
		}
	}

	if (l4util_kip_kernel_abi_version(l4lx_kinfo) < required_kernel_abi_version) {
		LOG_printf("The ABI version of the microkernel is too low: it has %ld, "
				"I need %ld\n",
				l4util_kip_kernel_abi_version(l4lx_kinfo),
				required_kernel_abi_version);
		while (1)
			enter_kdebug("Stop!");
	}
}


static int l4x_cpu_virt_phys_map_init(void) {
	l4_umword_t max_cpus;
	l4_sched_cpu_set_t cs = l4_sched_cpu_set(0, 0, 0);
	unsigned i;

	if (l4_error(l4_scheduler_info(l4re_env()->scheduler,
					&max_cpus, &cs)) == L4_EOK) {
		int p = 0;
		if(cs.map)
			p = find_first_bit(&cs.map, sizeof(cs.map) * 8);
		l4x_cpu_physmap[0].phys_id = p;
	}

	LOG_printf("CPU mapping (l:p): ");
	for (i = 0; i < l4x_nr_cpus; i++)
		LOG_printf("%u:%u%s", i, l4x_cpu_physmap[i].phys_id,
				i == l4x_nr_cpus - 1 ? "\n" : ", ");

	return 0;
}

static unsigned l4x_x86_orig_utcb_segment;
static inline void l4x_x86_utcb_save_orig_segment(void)
{
	asm volatile ("mov %%gs, %0": "=r" (l4x_x86_orig_utcb_segment));
}

unsigned l4x_x86_utcb_get_orig_segment(void)
{
	return l4x_x86_orig_utcb_segment;
}

static void get_initial_cpu_capabilities(void)
{
	/* XXX we should set a l4util_cpu_capabilities() here */
}

void l4x_v2p_init(void)
{
	l4x_phys_virt_addr_items = 0;
}
