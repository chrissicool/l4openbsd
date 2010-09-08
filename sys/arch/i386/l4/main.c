/*
 * main.c
 *
 * Main entry point for L4BSD
 */

#include <sys/l4/l4lib.h>
// just taken from L4Linux's arch/l4/kernel/main.c
#include <l4/sys/err.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/thread.h>
#include <l4/sys/factory.h>
#include <l4/sys/scheduler.h>
#include <l4/sys/task.h>
#include <l4/sys/debugger.h>
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
#include <l4/io/io.h>

unsigned long __l4_external_resolver;

/*
 * used external functions from L4
 */
L4_EXTERNAL_FUNC(LOG_printf);

/*
 * prototypes for head.S
 */
l4re_env_t *l4re_global_env;
l4_kernel_info_t *l4lx_kinfo;

/*
 * prototypes
 */
int L4_CV l4start(int argc, char **argv);
L4_CV l4_utcb_t *l4_utcb_wrap(void);

/*
 * implentation
 */

int L4_CV l4start(int argc, char **argv) {
	LOG_printf("\033[34;1m======> L4OpenBSD starting... <========\033[0m\n");
	LOG_printf("Binary name: %s\n", (*argv)?*argv:"NULL??");
	argv++;
	LOG_printf("OpenBSD kernel command line (%d args): ", argc - 1);
	while (*argv) {
		LOG_printf("%s ", *argv);
		argv++;
	}
	LOG_printf("\n");

	return 0;
}

L4_CV l4_utcb_t *l4_utcb_wrap(void)
{
#ifdef ARCH_arm
	return l4_utcb_direct();
#else
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
#endif
}

