/*
 * main.c
 *
 * Main entry point for L4BSD
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/ptrace.h>

#include <machine/cpu.h>

#include <machine/l4/linux_compat.h>
#include <machine/l4/api/macros.h>
#include <machine/l4/l4lxapi/task.h>
#include <machine/l4/l4lxapi/thread.h>
#include <machine/l4/iodb.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/smp.h>
#include <machine/l4/exception.h>

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

/*
 * variables
 */
unsigned long __l4_external_resolver;
extern char __l4sys_invoke_direct[];

l4_kernel_info_t *l4lx_kinfo;
l4_vcpu_state_t *l4x_vcpu_states[MAXCPUS];
unsigned int  l4x_nr_cpus = 1;

struct l4x_phys_virt_mem {
	l4_addr_t phys; /* physical address */
	void    * virt; /* virtual address */
	l4_size_t size; /* size of chunk in Bytes */
};
#define L4X_PHYS_VIRT_ADDRS_MAX_ITEMS 20
static struct l4x_phys_virt_mem l4x_phys_virt_addrs[L4X_PHYS_VIRT_ADDRS_MAX_ITEMS];
int l4x_phys_virt_addr_items;

l4_cap_idx_t linux_server_thread_id = L4_INVALID_CAP;
l4_cap_idx_t l4x_start_thread_id = L4_INVALID_CAP;
l4_cap_idx_t l4x_start_thread_pager_id = L4_INVALID_CAP;

unsigned l4x_fiasco_gdt_entry_offset;

struct simplelock l4x_cap_lock;
static char init_stack[PAGE_SIZE];	/* XXX cl: this is plain wrong */
enum {
	L4X_SERVER_EXIT = 1,
};

int l4x_debug_show_exceptions;

struct l4x_exception_func_struct {
	int           (*f)(l4_exc_regs_t *exc);
	unsigned long trap_mask;
	int           for_vcpu;
};
static struct l4x_exception_func_struct l4x_exception_func_list[] = {
#ifdef L4_USE_L4VMM
	/* TODO THERE IS A BIG FAT TODO HERE */
//	{ .trap_mask = ~0UL,   .for_vcpu = 1, .f = l4vmm_handle_exception_r0 },
#endif
#ifdef ARCH_x86
//	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_hlt_for_bugs_test }, // before kprobes!
#ifdef CONFIG_KPROBES
//	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_kprobes },
#endif
//	{ .trap_mask = 0x2000, .for_vcpu = 0, .f = l4x_handle_lxsyscall },
//	{ .trap_mask = 0x0002, .for_vcpu = 0, .f = l4x_handle_int1 },
//	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_msr },
//	{ .trap_mask = 0x2000, .for_vcpu = 1, .f = l4x_handle_clisti },
//	{ .trap_mask = 0x4000, .for_vcpu = 0, .f = l4x_handle_ioport },
#endif
#ifdef ARCH_arm
//	{ .trap_mask = ~0UL,   .for_vcpu = 1, .f = l4x_arm_instruction_emu },
#endif
};
static const int l4x_exception_funcs
	= sizeof(l4x_exception_func_list) / sizeof(l4x_exception_func_list[0]);

static unsigned long l4x_handle_ioport_pending_pc;

/*
 * prototypes from others
 */
extern void start(void);	/* locore.s */
void l4x_external_exit(int code);	/* jumps back to the wrapper */
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
void l4x_v2p_add_item(l4_addr_t phys, void *virt, l4_size_t size);

static L4_CV void l4x_bsd_startup(void *data);
static void l4x_server_loop(void) __attribute__((__noreturn__));
void l4x_linux_main_exit(void);
static int l4x_default(l4_cap_idx_t *src_id, l4_msgtag_t *tag);
static inline void l4x_print_exception(l4_cap_idx_t t, l4_exc_regs_t *exc);
static inline int l4x_handle_pagefault(unsigned long pfa,
					unsigned long ip, int wr);
static void l4x_setup_die_utcb(l4_exc_regs_t *exc);
static int l4x_forward_pf(l4_umword_t addr, l4_umword_t pc, int extra_write);

void exit(int code);

/* Functions for l4x_exception_func_list[] */

/*
 * implentation
 */

int L4_CV l4start(int argc, char **argv) {

	l4_cap_idx_t main_id;
	l4_msgtag_t tag;

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

	l4lx_task_init();
	l4lx_thread_init();

	l4x_start_thread_id = l4re_env()->main_thread;

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
	l4x_v2p_add_item(0xa0000, (void *)0xa0000, 0xfffff - 0xa0000);

#ifdef L4_EXTERNAL_RTC
	l4_uint32_t seconds;
	if (l4rtc_get_seconds_since_1970(&seconds))
		LOG_printf("WARNING: RTC server does not seem there!\n");
#endif

	/* Set name of startup thread */
	l4lx_thread_name_set(l4x_start_thread_id, "l4x-start");

	/* create simple_lock for l4x_bsd_startup thread */
	simple_lock_init(&l4x_cap_lock);

	/* fire up Linux server, will wait until start message */
	main_id = l4lx_thread_create(l4x_bsd_startup, 0,
			(char *)init_stack + sizeof(init_stack),
			&l4x_start_thread_id, sizeof(l4x_start_thread_id),
			-1,
			L4_THREAD_CONTROL_VCPU_ENABLED,
			"cpu0");

	if (l4_is_invalid_cap(main_id)) {
		LOG_printf("No caps to create main thread\n");
		return 1;
	}

#ifdef MULTIPROCESSOR
	/* XXX this does not happen, ATM */
	l4x_cpu_thread_set(0, main_id);
#endif

	LOG_printf("main thread will be " PRINTF_L4TASK_FORM "\n",
			PRINTF_L4TASK_ARG(main_id));

//	l4x_register_pointer_section(&__init_begin, 0, "sec-w-init");

	/* Send start message to main thread. */
	tag = l4_msgtag(0, 0, 0, 0);
	l4_ipc_send(main_id, l4_utcb(), tag, L4_IPC_NEVER);
	LOG_printf("Main thread running, waiting...\n");
	l4x_server_loop();
	
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
	//return 42; /* XXX cl: l4x_stack_utcb_get(); */
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

void l4x_v2p_init(void)
{
	l4x_phys_virt_addr_items = 0;
}

void l4x_v2p_add_item(l4_addr_t phys, void *virt, l4_size_t size)
{
	if (l4x_phys_virt_addr_items == L4X_PHYS_VIRT_ADDRS_MAX_ITEMS)
		panic("v2p filled up!");

	l4x_phys_virt_addrs[l4x_phys_virt_addr_items++]
			= (struct l4x_phys_virt_mem){.phys = phys, .virt = virt, .size = size};
}

static L4_CV void l4x_bsd_startup(void *data)
{
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
}

void l4x_linux_main_exit(void)
{
	extern void exit(int);
	LOG_printf("Terminating L4Linux.\n");
	exit(0);
}

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

static inline void l4x_print_exception(l4_cap_idx_t t, l4_exc_regs_t *exc)
{
	LOG_printf("EX: "l4util_idfmt": pc = "l4_addr_fmt
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
	struct reg regs;
	unsigned long regs_addr;
	extern void die(const char *msg, struct reg *regs, int err);
	static char message[40];

	snprintf(message, sizeof(message), "Trap: %ld", exc->trapno);
	message[sizeof(message) - 1] = 0;

	memset(&regs, 0, sizeof(regs));
	utcb_exc_to_ptregs(exc, &regs);

	/* XXX: check stack boundaries, i.e. exc->esp & (THREAD_SIZE-1)
	 * >= THREAD_SIZE - sizeof(thread_struct) - sizeof(struct reg)
	 * - ...)
	 */
	/* Copy regs on the stack */
	exc->sp -= sizeof(struct reg);
	*(struct reg *)exc->sp = regs;
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

void exit(int code)
{
/* TODO	__cxa_finalize(0); */
	l4x_external_exit(code);
	LOG_printf("Still alive, going zombie???\n");
	l4_sleep_forever();
}

