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
 * Implementation of include/asm-l4/l4lxapi/memory.h
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <machine/pte.h>
#include <machine/cpufunc.h>

#include <machine/l4/l4lxapi/memory.h>
#include <machine/l4/api/api.h>
#include <machine/l4/vcpu.h>
#include <machine/l4/log.h>

#include <l4/sys/kdebug.h>
#include <l4/re/c/rm.h>

#include <l4/log/log.h>

/*
 * Check if an address is correctly aligned.
 */
#define IS_ALIGNED(addr)	(addr == (addr & 0xfffffffcUL))

int
l4lx_memory_map_virtual_page(vaddr_t address, paddr_t page, int map_rw)
{
	l4re_ds_t ds;
	l4_addr_t offset;
	l4_addr_t addr;
	unsigned flags;
	unsigned long size;
	int r;
	L4XV_V(f);

	addr = page;
	size = PAGE_SIZE;
	L4XV_L(f);
	if (l4re_rm_find(&addr, &size, &offset, &flags, &ds)) {
		L4XV_U(f);
		printf("%s: Cannot get dataspace of %08lx.\n",
		       __func__, page);
		return -1;
	}

	offset += page - addr;
	addr    = address;
#ifdef DIAGNOSTIC
	if (page == 0 || address == 0)
		panic("l4lx_memory_map_virtual_page: address 0x%08lx page "
		    "0x%08lx\n", (unsigned long)address, (unsigned long)page);
	/* Sanity checks */
	if (!IS_ALIGNED(addr) || !IS_ALIGNED(page))
		printf("l4lx_memory_map_virtual_page: address 0x%08lx page "
		    "0x%08x flags 0x%08lx\n", (unsigned long)address,
		    (unsigned long)page, map_rw);
	if (!IS_ALIGNED(addr))
		panic("WARNING: Trying to map unaligned virtual page!\n");
	if (!IS_ALIGNED(page))
		panic("WARNING: Trying to map unaligned physical page!\n");
#endif
//	l4x_printf("Attaching DS: PA=0x%08lx, VA=0x%08lx, Vaddr=%08lx, off=%08lx\n",
//			page, address, addr, offset);
	if ((r = l4re_rm_attach((void **)&addr, PAGE_SIZE,
	                        L4RE_RM_IN_AREA | L4RE_RM_EAGER_MAP
	                        | (map_rw & PG_RW ? 0 : L4RE_RM_READ_ONLY),
	                        ds, offset, L4_PAGESHIFT))) {
		L4XV_U(f);
#if defined(DIAGNOSTIC) && defined(DDB)
		// FIXME wrt L4_EUSED
		panic("%s: cannot attach vpage (0x%08lx, 0x%08lx): %d\n",
		       __func__, address, page, r);
		Debugger();
#endif
		return -1;
	}
	L4XV_U(f);
#if defined(DIAGNOSTIC) && defined(DDB)
	if (addr != address) {
		panic("%s: ERROR: Did not attach vpage @0x%08lx."
				"Attached to 0x%08lx instead\n",
				__func__, address, addr);
		Debugger();
		return -1;
	}
#endif
	return 0;
}

int l4lx_memory_unmap_virtual_page(vaddr_t address)
{
	int r;
	L4XV_V(f);

	/* Sanity check. */
	if (!IS_ALIGNED(address)) {
		printf("WARNING: Trying to unmap unaligned virtual page!\n");
		printf("l4lx_memory_unmap_virtual_page: address 0x%08lx",
		    (unsigned long)address);
	}
	L4XV_L(f);
//	l4x_printf("Detaching DS: VA=0x%08lx\n", address);
	if ((r = l4re_rm_detach((void *)address))) {
		L4XV_U(f);
#if defined(DIAGNOSTIC) && defined(DDB)
		printf("%s: cannot detach vpage 0x%08lx: %d\n",
				__func__, address, r);
		Debugger();
#endif
		return -1;
	}
	L4XV_U(f);
	return 0;
}

/* Returns 0 if not mapped, not-0 if mapped */
int l4lx_memory_page_mapped(vaddr_t address)
{
	l4re_ds_t ds;
	unsigned flags;
	unsigned long addr = address;
	unsigned long size = 1, off;
	int ret;
	L4XV_V(f);

	L4XV_L(f);
	ret = l4re_rm_find(&addr, &size, &off, &flags, &ds);
	L4XV_U(f);

	if ((addr == address) && (ret == 0))
		return 1;

	return 0;
}
