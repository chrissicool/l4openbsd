/*
 * main.c
 *
 * Main entry point for L4BSD
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/lock.h>
#include <sys/ptrace.h>
#include <sys/types.h>

#include <machine/cpu.h>
#include <machine/specialreg.h>
#include <machine/segments.h>

#include <machine/l4/bsd_compat.h>
#include <machine/l4/linux_compat.h>
#include <machine/l4/api/config.h>
#include <machine/l4/api/macros.h>
#include <machine/l4/l4lxapi/task.h>
#include <machine/l4/l4lxapi/thread.h>
#include <machine/l4/util.h>
#include <machine/l4/iodb.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/smp.h>
#include <machine/l4/exception.h>
#include <machine/l4/setup.h>
#include <machine/l4/stack_id.h>
#include <machine/l4/vcpu.h>
#include <machine/l4/log.h>

// just taken from L4Linux's arch/l4/kernel/main.c
#include <l4/sys/err.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/thread.h>
#include <l4/sys/factory.h>
#include <l4/sys/segment.h>
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
#include <l4/rtc/rtc.h>
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

/* TODO find the equivalent OpenBSD function */
static inline void die(const char *msg, struct reg *regs, int err) {}

/*
 * variables
 */
unsigned long __l4_external_resolver;
extern char __l4sys_invoke_direct[];

l4_kernel_info_t *l4lx_kinfo;
l4_vcpu_state_t *l4x_vcpu_states[MAXCPUS];
unsigned int  l4x_nr_cpus = 1;

static l4lx_thread_t l4x_cpu_threads[MAXCPUS];

int l4x_phys_virt_addr_items;
vaddr_t upage_addr;

l4_cap_idx_t linux_server_thread_id = L4_INVALID_CAP;
l4_cap_idx_t l4x_start_thread_id = L4_INVALID_CAP;
l4_cap_idx_t l4x_start_thread_pager_id = L4_INVALID_CAP;
l4_cap_idx_t l4x_user_gate[MAXCPUS];

unsigned l4x_fiasco_gdt_entry_offset;

struct simplelock l4x_cap_lock;
static char init_stack[2*PAGE_SIZE];	/* XXX cl: this is wrong, but sufficient */
enum {
	L4X_SERVER_EXIT = 1,
};

int l4x_debug_show_exceptions;

struct l4x_exception_func_struct {
	int           (*f)(l4_exc_regs_t *exc);
	unsigned long trap_mask;
	int           for_vcpu;
};

/* Functions for l4x_exception_func_list[] */
static int l4x_handle_msr(l4_exc_regs_t *exc);
static int l4x_handle_clisti(l4_exc_regs_t *exc);

static struct l4x_exception_func_struct l4x_exception_func_list[] = {
#ifdef L4_USE_L4VMM
//	{ .trap_mask = ~0UL,   .for_vcpu = 1, .f = l4vmm_handle_exception_r0 },
#endif
//	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_hlt_for_bugs_test }, // before kprobes!
#ifdef CONFIG_KPROBES
//	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_kprobes },
#endif
//	{ .trap_mask = 0x2000, .for_vcpu = 0, .f = l4x_handle_lxsyscall },
//	{ .trap_mask = 0x0002, .for_vcpu = 0, .f = l4x_handle_int1 },
	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_msr },
	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_clisti },
//	{ .trap_mask = 0x4000, .for_vcpu = 0, .f = l4x_handle_ioport },
};
static const int l4x_exception_funcs
	= sizeof(l4x_exception_func_list) / sizeof(l4x_exception_func_list[0]);

static unsigned long l4x_handle_ioport_pending_pc;

extern struct user *proc0paddr;         /* locore.s */
/*
 * prototypes from others
 */
extern void start(void);	/* locore.s */
L4_CV void l4x_external_exit(int code);		/* jumps back to the wrapper */
l4re_env_t *l4re_global_env;
l4_kernel_info_t *l4lx_kinfo;

/*
 * prototypes
 */
l4lx_thread_t l4x_cpu_thread_get(int cpu);
l4_cap_idx_t l4x_cpu_thread_get_cap(int cpu);
void l4x_cpu_thread_set(int cpu, l4lx_thread_t tid);

int L4_CV l4start(int argc, char **argv);

L4_CV l4_utcb_t *l4_utcb_wrap(void);

static void l4x_configuration_sanity_check(void);

static int l4x_cpu_virt_phys_map_init(void);
static inline void l4x_x86_utcb_save_orig_segment(void);
//unsigned l4x_x86_utcb_get_orig_segment(void);

static void get_initial_cpu_capabilities(void);

void l4x_load_gdt_register(struct region_descriptor *gdt);
void l4x_register_pointer_section(void *p_in_addr,
		int allow_noncontig, const char *tag);
void l4x_register_region(const l4re_ds_t ds, void *start,
		int allow_noncontig, const char *tag);

static void l4x_setup_upage(void);

static L4_CV void l4x_bsd_startup(void *data);
static void l4x_server_loop(void) __attribute__((__noreturn__));
void l4x_linux_main_exit(void);
void l4x_setup_curproc(void);
static int l4x_default(l4_cap_idx_t *src_id, l4_msgtag_t *tag);
static inline void l4x_print_exception(l4_cap_idx_t t, l4_exc_regs_t *exc);
static inline int l4x_handle_pagefault(unsigned long pfa,
					unsigned long ip, int wr);
static void l4x_setup_die_utcb(l4_exc_regs_t *exc);
static int l4x_forward_pf(l4_umword_t addr, l4_umword_t pc, int extra_write);

static void l4x_create_ugate(l4_cap_idx_t forthread, unsigned cpu);

void exit(int code);

#ifdef RAMDISK_HOOKS

extern u_int32_t rd_root_size;		/* from dev/rd.c */
extern char rd_root_image[];		/* from dev/rd.c */

#define L4X_MAX_RD_PATH 200
static char l4x_rd_path[L4X_MAX_RD_PATH];

void l4x_load_initrd(char **command_line);
static int fprov_load_initrd(const char *filename,
                             void  *rd_start,
                             size_t size);
int l4x_query_and_get_ds(const char *name, const char *logprefix,
		l4_cap_idx_t *dscap, void **addr,
		l4re_ds_stats_t *dsstat);

#endif /* RAMDISK_HOOKS */

/*
 * ===>>> IMPLEMENTATION <<<===
 */

/*
 * kthreads related functions
 */

l4lx_thread_t l4x_cpu_thread_get(int cpu)
{
       KASSERT(cpu <= MAXCPUS);
       return l4x_cpu_threads[cpu];
}

l4_cap_idx_t l4x_cpu_thread_get_cap(int cpu)
{
	return l4lx_thread_get_cap(l4x_cpu_thread_get(cpu));
}

void l4x_cpu_thread_set(int cpu, l4lx_thread_t tid)
{
       l4x_cpu_threads[cpu] = tid;
}

/*
 * The starting point.
 */
int L4_CV l4start(int argc, char **argv)
{
	l4lx_thread_t main_id;

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
	char **iterate_argv = argv;
	while (*iterate_argv) {
		LOG_printf("%s ", *argv);
		iterate_argv++;
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

	l4lx_task_init();
	l4lx_thread_init();

	l4x_start_thread_id = l4re_env()->main_thread;

// #ifdef CONFIG_L4_DEBUG_REGISTER_NAMES
	l4_debugger_set_object_name(l4x_start_thread_id, "l4bsd.main");
// #endif /* CONFIG_L4_DEBUG_REGISTER_NAMES */

	l4_thread_control_start();
	l4_thread_control_commit(l4x_start_thread_id);
	l4x_start_thread_pager_id
		= l4_utcb_mr()->mr[L4_THREAD_CONTROL_MR_IDX_PAGER];

	l4x_iodb_init();

	/* Initialize GDT entry offset */
	l4x_fiasco_gdt_entry_offset 
		= fiasco_gdt_get_entry_offset(l4re_env()->main_thread, l4_utcb());
	LOG_printf("l4x_fiasco_gdt_entry_offset = %x\n",
			l4x_fiasco_gdt_entry_offset);

	/* register physmem for (r/o)-data sections */
	l4x_register_pointer_section(&__rodata_start, 0, "sec-rodata");
	l4x_register_pointer_section(&__data_start, 0, "sec-data");

	/* VGA BIOS memory hole */
	l4x_v2p_add_item(0xa0000, 0xa0000, 0xfffff - 0xa0000);

#ifdef L4_EXTERNAL_RTC
	l4_uint32_t seconds;
	if (l4rtc_get_seconds_since_1970(&seconds))
		LOG_printf("WARNING: RTC server does not seem there!\n");
#endif

	l4x_setup_upage();

	/* Set name of startup thread */
	l4lx_thread_name_set(l4x_start_thread_id, "l4x-start");

	/* create simple_lock for l4x_bsd_startup thread */
	simple_lock_init(&l4x_cap_lock);

	/* fire up Linux server, will wait until start message */
	main_id = l4lx_thread_create(l4x_bsd_startup, 0,
			(char *)init_stack + sizeof(init_stack),
			&l4x_start_thread_id, sizeof(l4x_start_thread_id),
			-1,
			&l4x_vcpu_states[0],
			"cpu0");

	if (!l4lx_thread_is_valid(main_id)) {
		LOG_printf("No caps to create main thread\n");
		return 1;
	}

	LOG_printf("main thread will be " PRINTF_L4TASK_FORM "\n",
			PRINTF_L4TASK_ARG(l4lx_thread_get_cap(main_id)));

	l4x_cpu_thread_set(0, main_id);
//	l4x_cpu_thread_set(0, l4x_start_thread_id);

//	l4x_register_pointer_section(&__init_begin, 0, "sec-w-init");

	/* Send start message to main thread. */
	LOG_printf("Main thread running, waiting...\n");
	l4_ipc_send(l4lx_thread_get_cap(main_id),
			l4_utcb(), l4_msgtag(0, 0, 0, 0), L4_IPC_NEVER);

	l4x_server_loop();
	/* NOTREACHED */

	/* Just in case... */
	return 0;
}

/*
 * overrides L4re's default utcb handling
 */
L4_CV l4_utcb_t *l4_utcb_wrap(void)
{
#ifdef ARCH_x86
	unsigned long v;
	__asm __volatile ("mov %%gs, %0": "=r" (v));
	if (v == 0x43 || v == 7) {
		__asm __volatile("mov %%gs:0, %0" : "=r" (v));
		return (l4_utcb_t *)v;
	}
	return l4x_stack_utcb_get();
#else
#error Add your arch
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
	/* XXX cl: we should set a l4util_cpu_capabilities() here */
}

/*
 * Set the global descriptor table.
 */
void l4x_load_gdt_register(struct region_descriptor *gdt)
{
	if (fiasco_gdt_set(L4_INVALID_CAP,
				gdt, 8, 2, l4_utcb()))
		LOG_printf("GDT setting failed\n");
	asm("mov %0, %%fs"
			: : "r" ((l4x_fiasco_gdt_entry_offset + 2) * 8 + 3) : "memory");
}

/*
 * Register program section(s) for virt_to_phys.
 */
void l4x_register_pointer_section(void *p_in_addr,
		int allow_noncontig,
		const char *tag)
{
	l4re_ds_t ds;
	l4_addr_t off;
	l4_addr_t addr;
	unsigned long size;
	unsigned flags;

	addr = (l4_addr_t)p_in_addr;
	size = 1;
	if (l4re_rm_find(&addr, &size, &off, &flags, &ds)) {
		LOG_printf("Cannot anything at %p?!", p_in_addr);
		l4re_rm_show_lists();
		enter_kdebug("l4re_rm_find failed");
		return;
	}

	LOG_printf("%s: addr = %08lx size = %ld\n", __func__, addr, size);

	l4x_register_region(ds, (void *)addr, allow_noncontig, tag);
}

void l4x_register_region(const l4re_ds_t ds, void *start,
		int allow_noncontig, const char *tag)
{
	l4_size_t ds_size;
	l4_addr_t offset = 0;
	l4_addr_t phys_addr;
	l4_size_t phys_size;

	ds_size = l4re_ds_size(ds);

	LOG_printf("%15s: virt: %08p to %08p [%u KiB]\n",
			tag, start, start + ds_size - 1, ds_size >> 10);

	while (offset < ds_size) {
		if (l4re_ds_phys(ds, offset, &phys_addr, &phys_size)) {
			LOG_printf("error: failed to get physical address for %lx.\n",
					(unsigned long)start + offset);
			break;
		}

		if (!allow_noncontig && phys_size != ds_size)
			LOG_printf("Noncontiguous region for %s\n", tag);

		l4x_v2p_add_item(phys_addr, (vaddr_t)(start + offset), phys_size);

		LOG_printf("%15s: Phys: 0x%08lx to 0x%08lx, Size: %8u\n",
				tag, phys_addr, phys_addr + phys_size - 1, phys_size);

		offset += phys_size;
	}
}

static void l4x_setup_upage(void)
{
	l4re_ds_t ds;

	if (l4_is_invalid_cap(ds = l4re_util_cap_alloc())) {
		LOG_printf("%s: Cap alloc failed\n", __func__);
		l4x_linux_main_exit();
	}

	if (l4re_ma_alloc(USPACE, ds, L4RE_MA_PINNED)) {
		LOG_printf("Memory request for upage failed\n");
		l4x_linux_main_exit();
	}

	upage_addr = UPAGE_USER_ADDRESS;
	if (l4re_rm_attach((void **)&upage_addr, USPACE,
			L4RE_RM_SEARCH_ADDR,
			ds, 0, L4_PAGESHIFT)) {
		LOG_printf("Cannot attach upage properly\n");
		l4x_linux_main_exit();
	}

	memset((void *)upage_addr, 0, USPACE);

	/* The UPAGE is used as kernel stack, too. */
	proc0paddr = (struct user *) upage_addr;

	LOG_printf("Attached upage to 0x%08x\n", proc0paddr);
}

static L4_CV void l4x_bsd_startup(void *data)
{
	l4_cap_idx_t caller_id = *(l4_cap_idx_t *)data;
	extern int main(void *framep);		/* see: sys/kern/init_main.c */
	extern char **bootargv;

	/* Wait for start signal */
	l4_ipc_receive(caller_id, l4_utcb(), L4_IPC_NEVER);
	LOG_printf("l4x_bsd_startup: received startup message.\n");

	/* Setup kernel stack for init386().  */
	l4x_setup_curproc();
	l4x_stack_setup(proc0paddr, l4_utcb(), 0);

	/* Initialize vCPU state now, we need it for init386()::consinit() */
	l4x_vcpu_init(l4x_vcpu_states[0]);
	l4lx_thread_pager_change(l4x_stack_id_get(), caller_id);

	/* Enumerate our CPU type. */
	l4x_enumerate_cpu();

	/*
	 * Setup (allocate) some main memory. On i386, init386() will also
	 * enumerate all available RAM according to bios_memmap from E820,
	 * which we will skip here.
	 */
	l4x_memory_setup(bootargv);
	paddr_t first_avail = l4x_setup_kernel_ptd();

	/*
	 * At this point we have a halfway usable proc0 structure.
	 */
	l4x_printf("%s: thread "l4util_idfmt".\n",
			__func__, l4util_idstr(l4x_stack_id_get()));

	l4x_create_ugate(l4x_stack_id_get(), 0);

#ifdef RAMDISK_HOOKS
	l4x_load_initrd(bootargv);
#endif

	/*
	 * init386() may set up interrupt handler routines.
	 */
	enable_intr();

	extern void init386(paddr_t first_avail); 	/* machdep.c */
	init386(first_avail);


	/* Finally, fasten your seatbelts... */
	main(data);

	/* NOTREACHED */
	l4x_linux_main_exit();
}

static void l4x_server_loop(void)
{
	int do_wait = 1;
	l4_msgtag_t tag = (l4_msgtag_t){0};
	l4_umword_t src = 0;

	while (1) {
		while (do_wait)
			do_wait = l4_msgtag_has_error(tag = l4_ipc_wait(l4_utcb(), &src, L4_IPC_NEVER));

		switch (l4_msgtag_label(tag)) {
			case L4X_SERVER_EXIT:
				l4x_linux_main_exit(); // will not return anyway
				do_wait = 1;
				break;
			default:
				if (l4x_default(&src, &tag)) {
					do_wait = 1;
					continue; // do not reply
				}
		}

		tag = l4_ipc_reply_and_wait(l4_utcb(), tag,
				&src, L4_IPC_SEND_TIMEOUT_0);
		do_wait = l4_msgtag_has_error(tag);
	}
	/* NOTREACHED */
}

void l4x_linux_main_exit(void)
{
	extern void exit(int);
	LOG_printf("Terminating L4Linux.\n");
	exit(0);
}

void l4x_exit_l4linux(void)
{
	l4_msgtag_t tag = l4_msgtag(L4X_SERVER_EXIT, 0, 0, 0);

/* TODO	__cxa_finalize(0); */

	if (l4_msgtag_has_error(l4_ipc_send(l4x_start_thread_id, l4_utcb(), tag, L4_IPC_NEVER)))
		outstring("IPC ERROR l4x_exit_l4linux\n");
	l4_sleep_forever();
	/* NOTREACHED */
}

void l4x_printf(const char *fmt, ...)
{
	va_list list;
	L4XV_V(f);

	va_start(list, fmt);
	L4XV_L(f);
	LOG_vprintf(fmt, list);
	L4XV_U(f);
	va_end(list);
}

/*
 * Since we never ran locore.s, we need to setup curproc early.
 * A whole lot of stuff depends on it.
 */
void
l4x_setup_curproc(void)
{
	struct proc *p;

	/* this is current running process */
	curproc = p = &proc0;
	p->p_cpu = curcpu();
	p->p_addr = proc0paddr;
}

/*
 * Exception scheduler
 */
static int l4x_default(l4_cap_idx_t *src_id, l4_msgtag_t *tag)
{
	l4_exc_regs_t exc;
	l4_umword_t pfa;
	l4_umword_t pc;

	pfa = l4_utcb_mr()->mr[0];
	pc  = l4_utcb_mr()->mr[1];

	memcpy(&exc, l4_utcb_exc(), sizeof(exc));

#if 0
	if (!l4_msgtag_is_exception(*tag)
			&& !l4_msgtag_is_io_page_fault(*tag)) {
		static unsigned long old_pf_addr = ~0UL, old_pf_pc = ~0UL;
		if (old_pf_addr == (pfa & ~1) && old_pf_pc == pc) {
			LOG_printf("Double page fault pfa=%08lx pc=%08lx\n",
					pfa, pc);
			enter_kdebug("Double pagefault");
		}
		old_pf_addr = pfa & ~1;
		old_pf_pc   = pc;
	}
#endif

	if (l4_msgtag_is_exception(*tag)) {
		int i;

		if (l4x_debug_show_exceptions)
			l4x_print_exception(*src_id, &exc);

		// check handlers for this exception
		for (i = 0; i < l4x_exception_funcs; i++)
			if (((1 << l4_utcb_exc_typeval(&exc)) & l4x_exception_func_list[i].trap_mask)
					&& !l4x_exception_func_list[i].f(&exc))
				break;

		// no handler wanted to handle this exception
		if (i == l4x_exception_funcs) {
			if (l4_utcb_exc_is_pf(&exc)
			    && l4x_handle_pagefault(l4_utcb_exc_pfa(&exc),
						    l4_utcb_exc_pc(&exc), 0)) {
				*tag = l4_msgtag(0, 0, 0, 0);
				pfa = pc = 0;
				return 0; // reply
			}
			l4x_setup_die_utcb(&exc);
		}
		*tag = l4_msgtag(0, L4_UTCB_EXCEPTION_REGS_SIZE, 0, 0);
		memcpy(l4_utcb_exc(), &exc, sizeof(exc));
		return 0; // reply

#ifdef ARCH_x86
	} else if (l4_msgtag_is_io_page_fault(*tag)) {
		LOG_printf("Invalid IO-Port access at pc = "l4_addr_fmt
				" port=0x%lx\n", pc, pfa >> 12);

		l4x_handle_ioport_pending_pc = pc;

		/* Make exception out of PF */
		*tag = l4_msgtag(-1, 1, 0, 0);
		l4_utcb_mr()->mr[0] = -1;
		return 0; // reply
#endif
	} else if (l4_msgtag_is_page_fault(*tag)) {
		if (l4x_debug_show_exceptions)
			LOG_printf("Page fault: addr = " l4_addr_fmt
					" pc = " l4_addr_fmt " (%s%s)\n",
					pfa, pc,
					pfa & 2 ? "rw" : "ro",
					pfa & 1 ? ", T" : "");

		if (l4x_handle_pagefault(pfa, pc, !!(pfa & 2))) {
			*tag = l4_msgtag(0, 0, 0, 0);
			return 0;
		}

		/* Make exception out of PF */
		*tag = l4_msgtag(-1, 1, 0, 0);
		l4_utcb_mr()->mr[0] = -1;
		return 0; // reply
	}

	l4x_print_exception(*src_id, &exc);

	return 1; // no reply
}

static inline
l4_exc_regs_t *cast_to_utcb_exc(l4_vcpu_regs_t *vcpu_regs)
{
	enum { OFF = offsetof(l4_vcpu_regs_t, gs), };

	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, gs)     - OFF != offsetof(l4_exc_regs_t, gs));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, fs)     - OFF != offsetof(l4_exc_regs_t, fs));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, di)     - OFF != offsetof(l4_exc_regs_t, edi));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, si)     - OFF != offsetof(l4_exc_regs_t, esi));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, bp)     - OFF != offsetof(l4_exc_regs_t, ebp));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, pfa)    - OFF != offsetof(l4_exc_regs_t, pfa));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, bx)     - OFF != offsetof(l4_exc_regs_t, ebx));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, dx)     - OFF != offsetof(l4_exc_regs_t, edx));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, cx)     - OFF != offsetof(l4_exc_regs_t, ecx));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, ax)     - OFF != offsetof(l4_exc_regs_t, eax));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, trapno) - OFF != offsetof(l4_exc_regs_t, trapno));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, err)    - OFF != offsetof(l4_exc_regs_t, err));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, ip)     - OFF != offsetof(l4_exc_regs_t, ip));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, flags)  - OFF != offsetof(l4_exc_regs_t, flags));
	BUILD_BUG_ON(offsetof(l4_vcpu_regs_t, sp)     - OFF != offsetof(l4_exc_regs_t, sp));

	return (l4_exc_regs_t *)((char *)vcpu_regs + OFF);
}

/*
 * Return true, if an exception was handled successfully.
 */
int l4x_vcpu_handle_kernel_exc(l4_vcpu_regs_t *vr)
{
	int i;
	l4_exc_regs_t *exc = cast_to_utcb_exc(vr);

	/* Check handlers for this exception. */
	for (i = 0; i < l4x_exception_funcs; i++) {
		struct l4x_exception_func_struct *f
			= &l4x_exception_func_list[i];

		if (f->for_vcpu
				&& ((1 << l4_utcb_exc_typeval(exc)) & f->trap_mask)
				&& !f->f(exc))
			break;
	}

	return i != l4x_exception_funcs;
}

static inline void l4x_print_exception(l4_cap_idx_t t, l4_exc_regs_t *exc)
{
	LOG_printf("Exception: "l4util_idfmt": pc = "l4_addr_fmt
			" trapno = 0x%lx err/pfa = 0x%lx%s\n",
			l4util_idstr(t), exc->ip,
			exc->trapno,
			exc->trapno == 14 ? exc->pfa : exc->err,
			exc->trapno == 14 ? (exc->err & 2) ? " w" : " r" : "");

	if (l4x_debug_show_exceptions >= 2
			&& !l4_utcb_exc_is_pf(exc)) {
		/* Lets assume we can do the following... */
		unsigned len = 72, i;
		unsigned long ip = exc->ip - 43;

		LOG_printf("Dump: ");
		for (i = 0; i < len; i++, ip++)
			if (ip == exc->ip)
				LOG_printf("<%02x> ", *(unsigned char *)ip);
			else
				LOG_printf("%02x ", *(unsigned char *)ip);

		LOG_printf(".\n");
	}
}

static inline int l4x_handle_pagefault(unsigned long pfa, unsigned long ip,
		int wr)
{
	l4_addr_t addr;
	unsigned long size;
	l4re_ds_t ds;
	l4_addr_t offset;
	unsigned flags;
	int r;

	/* Check if the page-fault is a resolvable one */
	addr = pfa;
	if (addr < L4_PAGESIZE)
		return 0; // will trigger an Ooops
	size = 1;
	r = l4re_rm_find(&addr, &size, &offset, &flags, &ds);
	if (r == -L4_ENOENT) {
		/* Not resolvable: Ooops */
		LOG_printf("Non-resolvable page fault at %lx, ip %lx.\n", pfa, ip);
		// will trigger an oops in caller
		return 0;
	}

	/* Forward PF to our pager */
	return l4x_forward_pf(pfa, ip, wr);
}

static void l4x_setup_die_utcb(l4_exc_regs_t *exc)
{
	struct trapframe regs;
	unsigned long regs_addr;
	extern void die(const char *msg, struct reg *regs, int err);
	static char message[40];

	snprintf(message, sizeof(message), "Trap: 0x%02lx @0x%08lx",
			exc->trapno, l4_utcb_exc_pc(exc));
	message[sizeof(message) - 1] = 0;

	memset(&regs, 0, sizeof(regs));
	utcb_exc_to_ptregs(exc, &regs);

	/* XXX: check stack boundaries, i.e. exc->esp & (THREAD_SIZE-1)
	 * >= THREAD_SIZE - sizeof(thread_struct) - sizeof(struct reg)
	 * - ...)
	 */
	/* Copy regs on the stack */
	exc->sp -= sizeof(struct trapframe);
	*(struct trapframe *)exc->sp = regs;
	regs_addr = exc->sp;

	/* Fill arguments in regs for die params */
	exc->ecx = exc->err;
	exc->edx = regs_addr;
	exc->eax = (unsigned long)message;

	exc->sp -= sizeof(unsigned long);
	*(unsigned long *)exc->sp = 0;
	/* Set PC to die function */
	exc->ip = (unsigned long)die;

	outstring("Die message: ");
	outstring(message);
	outstring("\n");
}

static int l4x_forward_pf(l4_umword_t addr, l4_umword_t pc, int extra_write)
{
	l4_msgtag_t tag;
	l4_umword_t err;
	l4_utcb_t *u = l4_utcb();

	do {
		l4_msg_regs_t *mr = l4_utcb_mr_u(u);
		mr->mr[0] = addr | (extra_write ? 2 : 0);
		mr->mr[1] = pc;
		mr->mr[2] = L4_ITEM_MAP | addr;
		mr->mr[3] = l4_fpage(addr, L4_LOG2_PAGESIZE, L4_FPAGE_RWX).raw;
		tag = l4_msgtag(L4_PROTO_PAGE_FAULT, 2, 1, 0);
		tag = l4_ipc_call(l4x_start_thread_pager_id, u, tag,
				L4_IPC_NEVER);
		err = l4_ipc_error(tag, u);
	} while (err == L4_IPC_SECANCELED || err == L4_IPC_SEABORTED);

	if (l4_msgtag_has_error(tag))
		LOG_printf("Error forwarding page fault: %lx\n",
				l4_utcb_tcr_u(u)->error);

	if (l4_msgtag_words(tag) > 0
			&& l4_utcb_mr_u(u)->mr[0] == ~0)
		// unresolvable page fault, we're supposed to trigger an
		// exception
		return 0;

	return 1;
}

static void l4x_create_ugate(l4_cap_idx_t forthread, unsigned cpu)
{
	l4_msgtag_t r;
	L4XV_V(flags);

	L4XV_L(flags);
	l4x_user_gate[cpu] = l4x_cap_alloc();
	if (l4_is_invalid_cap(l4x_user_gate[cpu]))
		LOG_printf("Error getting cap\n");
	r = l4_factory_create_gate(l4re_env()->factory,
			l4x_user_gate[cpu],
			forthread, 0x10);
	if (l4_error(r))
		LOG_printf("Error creating user-gate %d\n", cpu);

	char n[14];
	snprintf(n, sizeof(n), "l4x ugate-%d", cpu);
	n[sizeof(n) - 1] = 0;
/* #ifdef CONFIG_L4_DEBUG_REGISTER_NAMES */
	l4_debugger_set_object_name(l4x_user_gate[cpu], n);
/* #endif */
	L4XV_U(flags);
}

/*
 * Exit the VM and return to the L4 runtime.
 */
void exit(int code)
{
/* TODO	__cxa_finalize(0); */
	l4x_external_exit(code);
	LOG_printf("Still alive, going zombie???\n");
	l4_sleep_forever();
	/* NOTREACHED */
}

/*
 * Full fledged name resolving, including cap allocation.
 */
int l4x_re_resolve_name(const char *name, l4_cap_idx_t *cap)
{
	const char *n = name;
	int r;
	l4re_env_cap_entry_t const *entry;
	L4XV_V(f);

	for (; *n && *n != '/'; ++n)
		;

	L4XV_L(f);
	entry = l4re_get_env_cap_l(name, n - name, l4re_env());
	if (!entry) {
		L4XV_U(f);
		return ENOENT;
	}

	if (!*n) {
		// full name resolved, no slashes, return
		*cap = entry->cap;
		L4XV_U(f);
		return 0;
	}

	if (l4_is_invalid_cap(*cap = l4x_cap_alloc())) {
		L4XV_U(f);
		return ENOMEM;
	}

	r = l4re_ns_query_srv(entry->cap, n + 1, *cap);
	if (r) {
		LOG_printf("Failed to query name '%s': %s(%d)\n",
				name, l4sys_errtostr(r), r);
		L4XV_U(f);
		return ENOENT;
	}

	return 0;
}

/*
 * BEGIN Functions for l4x_exception_func_list[]
 */

static int l4x_handle_msr(l4_exc_regs_t *exc)
{
	void *pc = (void *)l4_utcb_exc_pc(exc);
	unsigned long reg = exc->ecx;

	/* wrmsr */
	if (*(unsigned short *)pc == 0x300f) {
		if (reg != MSR_SYSENTER_CS
				&& reg != MSR_SYSENTER_ESP
				&& reg != MSR_SYSENTER_EIP)
			LOG_printf("WARNING: Unknown wrmsr: %08lx at %p\n", reg, pc);

		exc->ip += 2;
		return 0; // handled
	}

	/* rdmsr */
	if (*(unsigned short *)pc == 0x320f) {
		if (reg == MSR_MISC_ENABLE) {
			exc->eax = exc->edx = 0;
/*		} else if (reg == MSR_K7_CLK_CTL) {
			exc->eax = 0x20000000;		*/
		} else
			LOG_printf("WARNING: Unknown rdmsr: %08lx at %p\n", reg, pc);

		exc->ip += 2;
		return 0; // handled
	}

	return 1; // not for us
}

static int l4x_handle_clisti(l4_exc_regs_t *exc)
{
	unsigned char opcode = *(unsigned char *)l4_utcb_exc_pc(exc);
	extern void exit(int);

	/* check for cli or sti instruction */
	if (opcode != 0xfa && opcode != 0xfb)
		return 1; /* not handled if not those instructions */

	/* If we trap those instructions it's most likely a configuration
	 * error and quite early in the boot-up phase, so just quit. */
	LOG_printf("Aborting L4Linux due to unexpected CLI/STI instructions"
			" at %lx.\n", l4_utcb_exc_pc(exc));
	enter_kdebug("abort");
	exit(0);

	/* NOTREACHED */
	return 0;
}

/*
 * END Functions for l4x_exception_func_list[]
 */

#ifdef RAMDISK_HOOKS

void l4x_load_initrd(char **command_line)
{
	char **i_cmdl;
	char *sa = NULL, *se;
	char param_str[] = "l4x_rd=";

	*l4x_rd_path = 0;
	i_cmdl = command_line;
	while (*i_cmdl) {
		sa = strstr(*i_cmdl, param_str);
		if (sa) {
			sa = *i_cmdl + strlen(param_str);
			se = sa;

			while (*se && *se != ' ')
				se++;
			if (se - sa > L4X_MAX_RD_PATH) {
				enter_kdebug("l4x_rd_path > L4X_MAX_RD_PATH");
				return;
			}
			strncpy(l4x_rd_path, sa, se - sa);
			LOG_printf("l4x_rd_path: %s\n", l4x_rd_path);
		}
		i_cmdl++;
	}

	if (*l4x_rd_path) {
		LOG_printf("Loading ramdisk: %s... ", l4x_rd_path);

		if (fprov_load_initrd(l4x_rd_path,
		                      rd_root_image,
		                      rd_root_size)) {
			LOG_printf("failed\n");
			LOG_flush();
			return;
		}

		LOG_printf("from 0x%08lx to 0x%08lx [%ldKiB]\n",
		           &rd_root_image[0],
			   &rd_root_image[0] + rd_root_size - 1,
		           rd_root_size >> 10);
	}
}

static int fprov_load_initrd(const char *filename,
                             void *rd_start,
                             size_t size)
{
	int ret;
	l4re_ds_stats_t dsstat;
	l4re_ds_t l4x_initrd_ds;
	l4_cap_idx_t *dscap = &l4x_initrd_ds;
	l4_addr_t addr;

	/*
	 * Try to map the ramdisk way before KVA_START.
	 */
	addr = KVA_START/4;

	if ((ret = l4x_query_and_get_ds(filename, "initrd", &l4x_initrd_ds,
	                                (void **)&addr, &dsstat))) {
		LOG_printf("error mapping ds %d: %s. ",
		           ret, l4sys_errtostr(ret));
		return -1;
	}

	/* copy the ramdisk */
	memcpy(rd_start, (void *)addr, dsstat.size);
	rd_root_size = dsstat.size;
//	LOG_printf("Data copied.\n");

	/* detach ds again */
//	l4re_rm_detach(&addr);
	l4x_cap_free(*dscap);
//	LOG_printf("Detached DS.\n");

	LOG_flush();
	return 0;
}

int l4x_query_and_get_ds(const char *name, const char *logprefix,
		l4_cap_idx_t *dscap, void **addr,
		l4re_ds_stats_t *dsstat)
{
	int ret;
	L4XV_V(f);

	ret = l4x_re_resolve_name(name, dscap);
	if (ret)
		return ret;
//	LOG_printf("Resolved name of DS\n");

	L4XV_L(f);
	if ((ret = l4re_ds_info(*dscap, dsstat)))
		goto out;
//	LOG_printf("Got dsinfo, size=%d, flags=%d\n", dsstat->size, dsstat->flags);

	/* attach ds somewhere */
	if ((ret = l4re_rm_attach(addr, dsstat->size,
			L4RE_RM_SEARCH_ADDR | L4RE_RM_EAGER_MAP | L4RE_RM_READ_ONLY,
			*dscap, 0, L4_PAGESHIFT)))
		goto out;
//	LOG_printf("Attached mapping at 0x%08lx\n", *addr);

	L4XV_U(f);

	return 0;

out:
	l4x_cap_free(*dscap);
	L4XV_U(f);
	return ret;
}

#endif /* RAMDISK_HOOKS */
