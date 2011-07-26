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

/*
 * Header file for generic thread handling functions.
 */
#ifndef __ASM_L4__L4LXAPI__GENERIC__THREAD_GEN_H__
#define __ASM_L4__L4LXAPI__GENERIC__THREAD_GEN_H__

#include <l4/sys/types.h>

#include <machine/l4/linux_compat.h>

/**
 * \defgroup thread_stack_int Thread stack management functions (internal).
 * \ingroup thread
 */

/**
 * \defgroup thread_stack Thread stack management functions.
 * \ingroup thread
 */

/**
 * Number of maximal available stacks for threads.
 * \ingroup thread_stack_int
 *
 * There's no problem increasing this value if needed, it'll only use
 * more memory from BSS. Needing more stacks than available will make the
 * kernel print an error message.
 */
#define L4LX_THREAD_NO_THREADS (48)

/**
 * Size of the stack for every thread, in Bytes.
 * \ingroup thread_stack
 *
 * Don't change the stack size without understanding the consequences!
 */
#define L4LX_THREAD_STACK_SIZE THREAD_SIZE

/**
 * Mask of the stack size.
 * \ingroup thread_stack
 */
//#define L4LX_THREAD_STACK_MASK (L4LX_THREAD_STACK_SIZE-1)

/**
 * \defgroup thread_name Thread name management functions.
 * \ingroup thread
 */

/**
 * Maximum length of a thread name.
 * \ingroup thread_name
 */
#define L4LX_THREAD_NAME_LEN (32)

/**
 * Structure holding a thread ID and a name.
 * \ingroup thread_name
 */
struct l4lx_thread_name_struct {
	l4_cap_idx_t	id;				///< Thread ID
	char		name[L4LX_THREAD_NAME_LEN];	///< Name of the thread
};

/**
 * Data structure holding the names.
 * \ingroup thread_name
 *
 * This is exported because writing export functions (like
 * l4lx_thread_name_get(l4_threadid_t, char *name)) is probably overkill.
 */
//extern struct l4lx_thread_name_struct l4lx_thread_names[L4LX_THREAD_NO_THREADS];



/**
 * \brief Get a stack for a thread.
 * \ingroup thread_stack_int
 *
 * \return The stack pointer (of the top of the stack), NULL if no free
 * stack could be found.
 */
void *l4lx_thread_stack_get(void);

/**
 * \brief Register a stack to a thread.
 * \ingroup thread_stack_int
 *
 * \param thread	The thread.
 * \param stack_pointer	The stack given back by l4lx_thread_stack_get.
 */
void l4lx_thread_stack_register(l4_cap_idx_t thread, void *stack_pointer);

/**
 * \brief  Get and register a thread.
 * \ingroup thread_stack_int
 *
 * \param thread	The thread for which the stack is.
 *
 * \return The stack pointer, NULL if no free stack could be found.
 */
void *l4lx_thread_stack_alloc(l4_cap_idx_t thread);

/**
 * \brief Free used stack again.
 * \ingroup thread_stack_int
 *
 * \param thread	ID of the thread from which the stack to free.
 *
 * Since it is possible to supply ones own stack on thread creation
 * we only free the stack which matches the thread number.
 */
//void l4lx_thread_stack_return(l4_cap_idx_t thread);


/**
 * \brief Generic stack setup for the Thread ID.
 * \ingroup thread_stack_int
 *
 * \param thread	Thread id to put onto the stack.
 * \param stack_pointer Pointer to the stack.
 * 			The stack pointer has to point to the end
 * 			of the stack (as returned by
 * 			l4lx_thread_stack_alloc), it won't be adjusted!
 *
 * \return The new stack pointer.
 *
 * This function is used to set up the stack. It puts the thread ID on top
 * of the stack.
 */
//void *l4lx_thread_stack_setup_id(l4_cap_idx_t thread,
//				 void *stack_pointer);
/**
 * \brief Generic stack setup for the stack data.
 * \ingroup thread_stack_int
 *
 * \param stack_pointer	Pointer to the stack pointer. The stack pointer
 * 			has to point to the right position.
 * \param stack_data	Pointer to the stack data which will be copied onto
 * 			the stack. The source of this memory will not be
 * 			used anymore after this function.
 * \param stack_data_size Size of the data pointed to by the stack_data
 * 			pointer. In bytes.
 * \retval data		Pointer to the data section.
 *
 * \return The new stack pointer.
 */
//void *l4lx_thread_stack_setup_data(void *stack_pointer,
//				   void *stack_data, unsigned stack_data_size,
//				   void **data);

/**
 * \brief Generic stack setup for function parameters.
 * \ingroup thread_stack_int
 *
 * \param stack_pointer	Stack pointer.
 * \param data		Pointer pointing to the data memory for the thread.
 *
 * \return The new stack pointer.
 */
//void *l4lx_thread_stack_setup_thread(void *stack_pointer, void *data);

/**
 * \brief Return a pointer to the stack of a thread.
 * \ingroup thread_stack
 *
 * \param thread	Thread id of the thread the stack is returned.
 *
 * \return Pointer to the stack of the thread, NULL if there's no stack
 * 	   for this thread number.
 */
//void *l4lx_thread_stack_get_base_pointer(l4_cap_idx_t thread);


/**
 * Set/Save the name of a thread.
 * \ingroup thread_name
 *
 * \param thread	Thread id of the thread.
 * \param name		Name of the thread.
 */
void l4lx_thread_name_set(l4_cap_idx_t thread, const char *name);

/**
 * Delete the name of a thread.
 * \ingroup thread_name
 *
 * \param thread	Thread id of the thread.
 */
//void l4lx_thread_name_delete(l4_cap_idx_t thread);


#endif /* ! __ASM_L4__L4LXAPI__GENERIC__THREAD_GEN_H__ */
