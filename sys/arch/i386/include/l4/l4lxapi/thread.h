/*
 * This header file defines the L4Linux internal interface
 * for thread management. All API must implement it.
 */
#ifndef __ASM_L4__L4LXAPI__THREAD_H__
#define __ASM_L4__L4LXAPI__THREAD_H__

#include <l4/sys/types.h>

/* Convenience include */
#include <machine/l4/l4lxapi/generic/thread_gen.h>

/**
 * \defgroup thread Thread management functions.
 * \ingroup l4lxapi
 */


/**
 * \brief Initialize thread handling.
 * \ingroup thread
 *
 * Call before any thread is created.
 */
void l4lx_thread_init(void);

/**
 * \brief Create a thread.
 * \ingroup thread
 *
 * \param thread_func	Thread function.
 * \param cpu_nr        CPU to start thread on.
 * \param stack_pointer	The stack, if set to NULL a stack will be allocated.
 *			This is the stack pointer from the top of the stack
 *			which means that you can put other data on the top
 *			of the stack yourself. If you supply your stack
 *			yourself you have to make sure your stack is big
 *			enough.
 * \param stack_data	Pointer to some data which will be copied to the
 *			stack. It can be used to transfer data to your new
 *			thread.
 * \param stack_data_size Size of your data pointed to by the stack_data
 *			pointer.
 * \param prio		Priority of the thread. If set to -1 the default
 *			priority will be choosen (i.e. no prio will be set).
 * \param name		String describing the thread. Only used for
 *			debugging purposes.
 *
 * \return Thread ID of the new thread, L4_INVALID_ID if an error occured.
 *
 * The stack layout for non L4Env threads:
 *
 * <pre>
 * Stack                                    Stack
 * bottom                                    top
 * ___________________________________...._____
 * |                            | | |      |  |
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *      <===============       ^ ^ ^   ^     ^
 *         Stack growth        | | |   |     |--- thread ID (not always)
 *                             | | |   |--------- data for new thread
 *                             | | |------------- pointer of data section
 *                             | |		  given to the new thread
 *                             | |--------------- fake return address
 *                             |----------------- ESP for new thread
 * </pre>
 */
l4_cap_idx_t l4lx_thread_create(L4_CV void (*thread_func)(void *data),
                                unsigned cpu_nr,
                                void *stack_pointer,
                                void *stack_data, unsigned stack_data_size,
                                int prio,
                                unsigned thread_control_flags, const char *name);

/**
 * \brief Change the pager of a (kernel) thread.
 * \ingroup thread
 *
 * \param thread	Thread to modify.
 * \param pager		Pager thread.
 */
void l4lx_thread_pager_change(l4_cap_idx_t thread, l4_cap_idx_t pager);

/**
 * \brief Change pager of given thread to the kernel pager.
 * \ingroup thread
 *
 * \param thread        Thread to modify.
 */
//void l4lx_thread_set_kernel_pager(l4_cap_idx_t thread);

/**
 * \brief Shutdown a thread.
 * \ingroup thread
 *
 * \param thread	Thread id of the thread to kill.
 */
//void l4lx_thread_shutdown(l4_cap_idx_t thread);

/**
 * \brief Check if two thread ids are equal. Do not use with different
 *        tasks (at least in L4ENV)!
 * \ingroup thread
 *
 * \param  t1		Thread 1.
 * \param  t2		Thread 2.
 *
 * \return 1 if threads are equal, 0 if not.
 */
//L4_INLINE int l4lx_thread_equal(l4_cap_idx_t t1, l4_cap_idx_t t2);

/*
 * Somewhat private init function.
 */
void l4lx_thread_utcb_alloc_init(void);

/*****************************************************************************
 * Include inlined implementations
 *****************************************************************************/

//#include <machine/l4/l4lxapi/impl/thread.h>

#endif /* ! __ASM_L4__L4LXAPI__THREAD_H__ */
