/*
 * Implementation of include/asm-l4/l4lxapi/memory.h
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <machine/pte.h>

#include <machine/l4/l4lxapi/memory.h>
#include <machine/l4/api/api.h>
#include <machine/l4/vcpu.h>

#include <l4/sys/kdebug.h>
#include <l4/re/c/rm.h>

#include <l4/log/log.h>

/*
 * Check if an address is correctly aligned.
 */
#define IS_ALIGNED(addr)	(addr & 0xfffffffcUL)

int l4lx_memory_map_virtual_page(vaddr_t address, paddr_t page,
                                 int map_rw)
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
	/* Sanety checks */
	if(!IS_ALIGNED(addr))
		printf("WARNING: Trying to map unaligned virtual page!\n");
	if(!IS_ALIGNED(page))
		printf("WARNING: Trying to map unaligned physical page!\n");

//	LOG_printf("Attaching DS: PA=0x%08lx, VA=0x%08lx, Vaddr=%08lx, off=%08lx\n",
//			page, address, addr, offset);
	if ((r = l4re_rm_attach((void **)&addr, PAGE_SIZE,
	                        L4RE_RM_IN_AREA | L4RE_RM_EAGER_MAP
	                        | (map_rw & PG_RW ? 0 : L4RE_RM_READ_ONLY),
	                        ds, offset, L4_PAGESHIFT))) {
		L4XV_U(f);
		// FIXME wrt L4_EUSED
		printf("%s: cannot attach vpage (0x%08lx, 0x%08lx): %d\n",
		       __func__, address, page, r);
		return -1;
	}
	L4XV_U(f);
#ifdef DIAGNOSTIC
	if (addr != address) {
		printf("%s: ERROR: Did not attach vpage @0x%08lx."
				"Attached to 0x%08lx instead\n",
				__func__, address, addr);
		l4lx_memory_unmap_virtual_page(addr);
		return -1;
	}
#endif
	return 0;
}

int l4lx_memory_unmap_virtual_page(vaddr_t address)
{
	L4XV_V(f);

	/* Sanety check. */
	if(!IS_ALIGNED(address))
		printf("WARNING: Trying to unmap unaligned virtual page!\n");

	L4XV_L(f);
//	LOG_printf("Detaching DS: VA=0x%08lx\n", address);
	if (l4re_rm_detach((void *)address)) {
		L4XV_U(f);
		// Do not complain: someone might vfree a reserved area
		// that has not been completely filled
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

	ret = !l4re_rm_find(&addr, &size, &off, &flags, &ds);

	L4XV_U(f);
	return ret;
}
