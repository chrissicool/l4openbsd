/*
 * Header file for generic task handling code.
 */
#ifndef __ASM_L4__L4LXAPI__GENERIC__TASK_GEN_H__
#define __ASM_L4__L4LXAPI__GENERIC__TASK_GEN_H__

#include <asm/api/config.h>

#define L4LX_TASK_BITVEC_SIZE	(TASK_NO_MAX - TASK_NO_MIN + 1)
#define L4LX_TASK_VEC_SIZE	(L4LX_TASK_BITVEC_SIZE / (sizeof(unsigned) * 8) + 1)

extern unsigned int l4lx_task_used[];

/**
 * \defgroup task_generic Generic task code used by some API library code.
 * \ingroup task
 */

/**
 * \brief Generic task number allocation (bit vector version).
 *
 * \return Allocate task number, -1 if no free task number could be found.
 *
 * This function implements l4lx_task_allocate in a way it is useful for
 * most API implementation but not for all.
 */
inline int l4lx_task_number_allocate_vector(void);

/**
 * \brief Free a previously used task number so that it can be reused (bit
 *        vector version).
 *
 * \param task_no	Task number to free.
 *
 * \return 0 on succes, -1 on invalid task number or when task was already
 * 			free. 
 *
 * This function implements a generic version of the l4lx_task_used[]
 * freeing code which is useful most API implementation but not all.
 */
inline int l4lx_task_number_free_vector(int task_no);


/**
 * \brief Generic task number allocation (direct RMGR version).
 *
 * \return Allocate task number, -1 if no free task number could be found.
 */
inline int l4lx_task_number_allocate_rmgr(void);

/**
 * \brief Free a previously used task number so that it can be reused
 *        (direct RMGR version).
 *
 * \param task_no	Task number to free.
 *
 * \return 0 on succes, -1 on invalid task number or when task was already
 * 			free. 
 */
inline int l4lx_task_number_free_rmgr(int task_no);

#endif /* ! __ASM_L4__L4LXAPI__GENERIC__TASK_GEN_H__ */
