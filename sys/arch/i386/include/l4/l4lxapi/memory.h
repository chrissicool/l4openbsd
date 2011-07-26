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
 * This header file defines the L4Linux internal interface for
 * memory handling functionality. All APIs have to implement it.
 *
 */
#ifndef __ASM_L4__L4LXAPI__MEMORY_H__
#define __ASM_L4__L4LXAPI__MEMORY_H__

#include <sys/types.h>

/**
 * \defgroup memory Memory handling functions.
 * \ingroup l4lxapi
 */

/**
 * \brief Map a page into the virtual address space.
 * \ingroup memory
 *
 * \param	address		Virtual address.
 * \param	page		Physical address.
 * \param       map_rw          True if map should be mapped rw.
 *
 * \return	0 on success, != 0 on error
 */
int l4lx_memory_map_virtual_page(vaddr_t address, paddr_t page,
                                 int mapping);

/**
 * \brief Unmap a page from the virtual address space.
 * \ingroup memory
 *
 * \param	address		Virtual adress.
 *
 * \return	0 on success, != 0 on error
 */
int l4lx_memory_unmap_virtual_page(vaddr_t address);

/**
 * \brief Return if something is mapped at given address
 * \ingroup memory
 *
 * \param	address		Address to query
 *
 * \return	1 if a page is mapped, 0 if not
 */
int l4lx_memory_page_mapped(vaddr_t address);

#endif /* ! __ASM_L4__L4LXAPI__MEMORY_H__ */
