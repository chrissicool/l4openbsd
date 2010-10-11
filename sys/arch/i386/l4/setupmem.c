/*
 * setupmem.c -- basic physmem allocator
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include <machine/vmparam.h>

#include <uvm/uvm_extern.h>

#include <machine/l4/bsd_compat.h>
#include <machine/l4/linux_compat.h>
#include <machine/l4/setup.h>

#include <l4/sys/err.h>
#include <l4/sys/types.h>
#include <l4/sys/kdebug.h>
#include <l4/sys/utcb.h>
#include <l4/log/log.h>
#include <l4/re/c/dataspace.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/debug.h>
#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/c/util/cap.h>

/*
 * P H Y S I C A L - T O - V I R T U A L
 * M E M O R Y   M A P P I N G
 */

struct l4x_phys_virt_mem {
	l4_addr_t phys; /* physical address */
	vaddr_t   virt; /* virtual address */
	l4_size_t size; /* size of chunk in Bytes */
};

#define L4X_PHYS_VIRT_ADDRS_MAX_ITEMS 20
static struct l4x_phys_virt_mem l4x_phys_virt_addrs[L4X_PHYS_VIRT_ADDRS_MAX_ITEMS];
static int l4x_phys_virt_addr_items;

void l4x_v2p_init(void)
{
	l4x_phys_virt_addr_items = 0;
}

void l4x_v2p_add_item(l4_addr_t phys, vaddr_t virt, l4_size_t size)
{
	if (l4x_phys_virt_addr_items == L4X_PHYS_VIRT_ADDRS_MAX_ITEMS)
		panic("v2p filled up!");

	l4x_phys_virt_addrs[l4x_phys_virt_addr_items++]
			= (struct l4x_phys_virt_mem) { 	.phys = phys, 
							.virt = virt, 
							.size = size };
}


paddr_t
l4x_virt_to_phys(volatile vaddr_t address)
{
	int i;

	/* sanety check */
	if (address == NULL)
		return NULL;

	for (i = 0; i < l4x_phys_virt_addr_items; i++) {
		if (l4x_phys_virt_addrs[i].virt <= address &&
				address < l4x_phys_virt_addrs[i].virt
				+ l4x_phys_virt_addrs[i].size) {
			return (address - l4x_phys_virt_addrs[i].virt
					+ l4x_phys_virt_addrs[i].phys);
		}
	}

	//l4x_virt_to_phys_show();
	/* Whitelist: */

	/* Debugging check: don't miss a translation, can give nasty
	 *                  DMA problems */
	LOG_printf("%s: Could not translate VA %p\n", __func__, address);

	return NULL;
}

vaddr_t
l4x_phys_to_virt(volatile paddr_t address)
{
	int i;

	/* sanety check */
	if (address == NULL)
		return NULL;

	for (i = 0; i < l4x_phys_virt_addr_items; i++) {
		if (l4x_phys_virt_addrs[i].phys <= address &&
				address < l4x_phys_virt_addrs[i].phys
				        + l4x_phys_virt_addrs[i].size) {
			return (address - l4x_phys_virt_addrs[i].phys
					+ l4x_phys_virt_addrs[i].virt);
		}
	}

	//l4x_virt_to_phys_show();
	/* Whitelist */
	if ((address < 0x1000) ||		/* first pte, direct mapped */
	    (address >= 0xa000 &&		/* VGA and ROM space...     */
	     address <= 0xffff)) {		/*   ... direct mapped      */
		return address;
	}

	/* Debugging check: don't miss a translation, can give nasty
	 *                  DMA problems */
	LOG_printf("%s: Could not translate PA %p\n", __func__, address);

	return NULL;
}

/*
 * M E M O R Y   S E T U P
 */

#ifdef L4_DEBUG
static int l4x_debug_show_ghost_regions = 1;
#else
static int l4x_debug_show_ghost_regions = 0;
#endif

/* Default memory size */
#ifdef L4_MEMSIZE
unsigned long l4x_mainmem_size = L4_MEMSIZE << 20;
#else
#warning Please define L4_MEMSIZE in your kernel config, for default memory size.
#endif

l4re_ds_t l4x_ds_mainmem;
void *l4x_main_memory_start;	/* vaddr_t */
l4re_ds_t l4x_ds_isa_dma;
static void *l4x_isa_dma_memory_start;
static unsigned long l4x_isa_dma_size   = 0;

static void setup_l4x_memory(char **cmdl,
                             vaddr_t *main_mem_startv,
                             vaddr_t *main_mem_endv,
                             vaddr_t *isa_dma_mem_startv,
                             vaddr_t *isa_dma_mem_endv);
static void l4x_map_below_mainmem_print_region(l4_addr_t s, l4_addr_t e);
static void l4x_map_below_mainmem(void);
static void l4x_mbm_request_ghost(l4re_ds_t *ghost_ds);
static unsigned long get_min_virt_address(void);

void l4x_memory_setup(char **cmdl)
{
	vaddr_t mem_startv, mem_endv, isa_startv, isa_endv;
	paddr_t mem_startp, mem_endp, isa_startp, isa_endp;
	extern char _end[];

	setup_l4x_memory(cmdl, &mem_startv, &mem_endv, &isa_startv, &isa_endv);

	if (mem_startv && (vaddr_t)&_end > mem_startv)
		enter_kdebug("Kernel maps into main memory region!");
	if (isa_startv && (vaddr_t)&_end > isa_startv)
		enter_kdebug("Kernel maps into ISA memory region!");

	mem_startp = l4x_virt_to_phys(mem_startv);
	mem_endp   = l4x_virt_to_phys(mem_endv);
	isa_startp = l4x_virt_to_phys(isa_startv);
	isa_endp   = l4x_virt_to_phys(isa_endv);

	if (mem_endv - mem_startv)
		uvm_page_physload(mem_startp, mem_endp, mem_startp, mem_endp,
				VM_FREELIST_DEFAULT);

	if (isa_endv - isa_startv)
		uvm_page_physload(isa_startp, isa_endp, isa_startp, isa_endp,
				VM_FREELIST_DEFAULT);

	/*
	 * Fill global parameters from machdep.c
	 * These are used on other occasions (PCI, ISA, ...)
	 */
	extern vaddr_t avail_end;
	extern u_int ndumpmem;
	extern struct dumpmem {
		vaddr_t start;
		vaddr_t end;
	} dumpmem[VM_PHYSSEG_MAX];
	extern int physmem;

	avail_end = mem_endp;
	ndumpmem = 1;
	dumpmem[0].start = atop(mem_startv);
	dumpmem[0].end = atop(mem_endv);
	physmem = atop(mem_endv - mem_startv);
}

static void setup_l4x_memory(char **cmdl,
                             vaddr_t *main_mem_startv,
                             vaddr_t *main_mem_endv,
                             vaddr_t *isa_dma_mem_startv,
                             vaddr_t *isa_dma_mem_endv)
{
	extern char _end[];
	int res;
	char *memstr, **i_cmdl;
	unsigned long memory_area_size;
	l4_addr_t memory_area_id = 0;
	l4_addr_t memory_area_addr;

	//l4_size_t poolsize, poolfree;
	l4_uint32_t dm_flags = L4RE_MA_CONTINUOUS | L4RE_MA_PINNED;

	/* See if we find a mem=xxx option in the command line */
	i_cmdl = cmdl;;
	while(*i_cmdl) {
		if ((memstr = strstr(*i_cmdl, "mem="))
				&& (res = memparse(memstr + 4, &memstr)))
			l4x_mainmem_size = res;
		i_cmdl++;
	}

	LOG_printf("utcb %p\n", l4_utcb());

	if ((l4x_mainmem_size % L4_SUPERPAGESIZE) == 0) {
		LOG_printf("%s: Forcing superpages for main memory\n", __func__);
		/* force ds-mgr to allocate superpages */
		dm_flags |= L4RE_MA_SUPER_PAGES;
	}

	/* Allocate main memory */
	if (l4_is_invalid_cap(l4x_ds_mainmem = l4re_util_cap_alloc())) {
		LOG_printf("%s: Out of caps\n", __func__);
		l4x_exit_l4linux();
	}
	if (l4re_ma_alloc(l4x_mainmem_size, l4x_ds_mainmem, dm_flags)) {
		LOG_printf("%s: Can't get main memory of %ldMB!\n",
		           __func__, l4x_mainmem_size >> 20);
		l4re_debug_obj_debug(l4re_env()->mem_alloc, 0);
		l4x_exit_l4linux();
	}

#if 0
	/* if there's a '+' at the end of the mem=-option try to get
	 * more memory */
	if (memstr && *memstr == '+') {
		unsigned long chunksize = 64 << 20;

		while (chunksize > (4 << 20)) {
			if ((res = l4dm_mem_resize(&l4x_ds_mainmem,
						   l4x_mainmem_size
			                           + chunksize)))
				chunksize >>= 1; /* failed */
			else
				l4x_mainmem_size += chunksize;
		}
	}
#endif

	LOG_printf("Main memory size: %ldMB\n", l4x_mainmem_size >> 20);
	if (l4x_mainmem_size < (4 << 20)) {
		LOG_printf("Need at least 4MB RAM - aborting!\n");
		l4x_exit_l4linux();
	}

	memory_area_size = l4x_mainmem_size;

	/* Try to get ISA DMA memory */

	/* See if we find a memisadma=xxx option in the command line */
	i_cmdl = cmdl;
	while(*i_cmdl) {
		if ((memstr = strstr(*i_cmdl, "memisadma="))) {
			LOG_printf("'memisadma' currently not supported\n");
			//l4x_isa_dma_size = memparse(memstr + 10, &memstr);
		}
		i_cmdl++;
	}

#if 0
	/** In the default config dm_phys prints out messages if
	 ** the allocation fails, so query the size separately to
	 ** not disturb users with error messages */
	if (l4x_isa_dma_size
	    && (res = l4dm_memphys_poolsize(L4DM_MEMPHYS_ISA_DMA9,
	                                    &poolsize, &poolfree))) {
		LOG_printf("Cannot query ISA DMA pool size: %s(%d)\n",
		           l4sys_errtostr(res), res);
		l4x_exit_l4linux();
	}
	if (l4x_isa_dma_size
	    && poolfree >= l4x_isa_dma_size
	    && !l4dm_memphys_open(L4DM_MEMPHYS_ISA_DMA9,
	                          L4DM_MEMPHYS_ANY_ADDR9,
	                          l4x_isa_dma_size, L4_PAGESIZE,
	                          L4DM_CONTIGUOUS9, "L4Linux ISA DMA memory",
	                          &l4x_ds_isa_dma)) {
		LOG_printf("Got %lukB of ISA DMA memory.\n",
		           l4x_isa_dma_size >> 10);
		memory_area_size += l4_round_size(l4x_isa_dma_size, L4_LOG2_SUPERPAGESIZE);
	} else
#endif
		l4x_ds_isa_dma = L4_INVALID_CAP;

	/* Get contiguous region in our virtual address space to put
	 * the dataspaces in */
	if (l4re_rm_reserve_area(&memory_area_id, memory_area_size,
	                         L4RE_RM_SEARCH_ADDR,
	                         L4_SUPERPAGESHIFT)) {
		LOG_printf("Error reserving memory area\n");
		l4x_exit_l4linux();
	}
	memory_area_addr = memory_area_id;

	/* Attach data spaces to local address space */
	/** First: the ISA DMA memory */
#if 0
	if (!l4_is_invalid_cap(l4x_ds_isa_dma)) {
		l4x_isa_dma_memory_start = (void *)memory_area_addr;
		l4x_main_memory_start =
			(void *)(l4x_isa_dma_memory_start
		                 + l4_round_size(l4x_isa_dma_size, L4_LOG2_SUPERPAGESIZE));
		if ((res = l4rm_area_attach_to_region(&l4x_ds_isa_dma,
	                                              memory_area_id,
	                                              l4x_isa_dma_memory_start,
	                                              l4x_isa_dma_size, 0,
	                                              L4DM_RW | L4RM_MAP))) {
			LOG_printf("Error attaching to ISA DMA memory: %s(%d)\n",
				   l4sys_errtostr(res), res);
			l4x_exit_l4linux();
		}
	} else
#endif
	l4x_main_memory_start = (void *)memory_area_addr;

	/** Second: the main memory */
	if (l4re_rm_attach(&l4x_main_memory_start, l4x_mainmem_size,
	                   L4RE_RM_IN_AREA | L4RE_RM_EAGER_MAP,
	                   l4x_ds_mainmem, 0, L4_SUPERPAGESHIFT)) {
		LOG_printf("Error attaching to L4Linux main memory\n");
		l4x_exit_l4linux();
	}

	/* Release area ... make possible hole available again */
	if (l4re_rm_free_area(memory_area_id)) {
		LOG_printf("Error releasing area\n");
		l4x_exit_l4linux();
	}

	*main_mem_startv = (vaddr_t)l4x_main_memory_start;
	*main_mem_endv   = (vaddr_t)(l4x_main_memory_start + l4x_mainmem_size - 1);

	if (l4_is_invalid_cap(l4x_ds_isa_dma))
		*isa_dma_mem_startv = *isa_dma_mem_endv = 0;
	else {
		*isa_dma_mem_startv = (vaddr_t)l4x_isa_dma_memory_start;
		*isa_dma_mem_endv   = (vaddr_t)(l4x_isa_dma_memory_start
				+ l4x_isa_dma_size - 1);
	}

	if (!l4_is_invalid_cap(l4x_ds_isa_dma))
		l4x_register_region(l4x_ds_isa_dma, l4x_isa_dma_memory_start,
		                      0, "ISA DMA memory");
	l4x_register_region(l4x_ds_mainmem, l4x_main_memory_start,
	                    0, "Main memory");

	/* Reserve some part of the virtual address space for vmalloc */
//	l4x_vmalloc_memory_start = (unsigned long)l4x_main_memory_start;
//	if (l4re_rm_reserve_area(&l4x_vmalloc_memory_start,
#ifdef ARCH_x86
//	                          __VMALLOC_RESERVE,
#else
//	                          VMALLOC_SIZE << 20,
#endif
//	                          L4RE_RM_SEARCH_ADDR, PGDIR_SHIFT)) {
//		LOG_printf("%s: Error reserving vmalloc memory!\n", __func__);
//		l4x_exit_l4linux();
//	}
//	l4x_vmalloc_areaid = l4x_vmalloc_memory_start;

#ifdef ARCH_x86
	// fixmap area
/*	l4x_fixmap_space_start = 0x100000; // not inside BIOS region
	if (l4re_rm_reserve_area(&l4x_fixmap_space_start,
	                         __end_of_permanent_fixed_addresses * PAGE_SIZE,
	                         L4RE_RM_SEARCH_ADDR, PAGE_SHIFT) < 0) {
		LOG_printf("%s: Failed reserving fixmap space!\n", __func__);
		l4x_exit_l4linux();
	}
	__FIXADDR_TOP = l4x_fixmap_space_start
	                 + __end_of_permanent_fixed_addresses * PAGE_SIZE;	*/
#endif
	l4x_map_below_mainmem();

	// that happened with some version of ld...
	if ((unsigned long)&_end < 0x100000)
		LOG_printf("_end == %p, unreasonable small\n", &_end);

	l4x_register_pointer_section((void *)((unsigned long)&_end - 1), 0, "end");
}

static void l4x_map_below_mainmem_print_region(l4_addr_t s, l4_addr_t e)
{
	if (!l4x_debug_show_ghost_regions)
		return;
	if (s == ~0UL)
		return;

	LOG_printf("Ghost region: %08lx - %08lx [%4ld]\n", s, e, (e - s) >> 12);
}

static void l4x_map_below_mainmem(void)
{
	unsigned long i;
	l4re_ds_t ds, ghost_ds = L4_INVALID_CAP;
	l4_addr_t off;
	l4_addr_t map_addr;
	unsigned long map_size;
	unsigned flags;
	long ret;
	unsigned long i_inc;
	int map_count = 0, map_count_all = 0;
	l4_addr_t reg_start = ~0UL;

	LOG_printf("Filling lower ptabs... ");
	LOG_flush();

	/* Loop through free address space before mainmem */
	for (i = get_min_virt_address();
	     i < (unsigned long)l4x_main_memory_start; i += i_inc) {
		map_addr = i;
		map_size = L4_PAGESIZE;
		ret = l4re_rm_find(&map_addr, &map_size, &off, &flags, &ds);
		if (ret == 0) {
			// success, something there
			if (i != map_addr)
				enter_kdebug("shouldn't be, hmm?");
			i_inc = map_size;
			l4x_map_below_mainmem_print_region(reg_start, i);
			reg_start = ~0UL;
			continue;
		}

		if (reg_start == ~0UL)
			reg_start = i;

		i_inc = L4_PAGESIZE;

		if (ret != -L4_ENOENT) {
			LOG_printf("l4re_rm_find call failure: %s(%ld)\n",
			           l4sys_errtostr(ret), ret);
			l4x_exit_l4linux();
		}

		if (!map_count) {
			/* Get new ghost page every 1024 mappings
			 * to overcome a Fiasco mapping db
			 * limitation. */
			l4x_mbm_request_ghost(&ghost_ds);
			map_count = 1014;
		}
		map_count--;
		map_count_all++;
		map_addr = i;
		if (l4re_rm_attach((void **)&map_addr, L4_PAGESIZE,
		                   L4RE_RM_READ_ONLY | L4RE_RM_EAGER_MAP,
		                   ghost_ds, 0, L4_PAGESHIFT)) {
			LOG_printf("%s: Can't attach ghost page at %lx!\n",
			           __func__, i);
			l4x_exit_l4linux();
		}
	}
	l4x_map_below_mainmem_print_region(reg_start, i);
	LOG_printf("done (%d entries).\n", map_count_all);
	LOG_flush();
}

/* To get mmap of /dev/mem working, map address space before
 * start of mainmem with a ro page,
 * lets try with this... */
static void l4x_mbm_request_ghost(l4re_ds_t *ghost_ds)
{
	unsigned int i;
	void *addr;

	if (l4_is_invalid_cap(*ghost_ds = l4re_util_cap_alloc())) {
		LOG_printf("%s: Out of caps\n", __func__);
		l4x_exit_l4linux();
	}

	/* Get a page from our dataspace manager */
	if (l4re_ma_alloc(L4_PAGESIZE, *ghost_ds, L4RE_MA_PINNED)) {
		LOG_printf("%s: Can't get ghost page!\n", __func__);
		l4x_exit_l4linux();
	}


	/* Poison the page, this is optional */

	/* Map page RW */
	addr = 0;
	if (l4re_rm_attach(&addr, L4_PAGESIZE, L4RE_RM_SEARCH_ADDR,
	                   *ghost_ds, 0, L4_PAGESHIFT)) {
		LOG_printf("%s: Can't map ghost page\n", __func__);
		l4x_exit_l4linux();
	}

	/* Write a certain value in to the page so that we can
	 * easily recognize it */
	for (i = 0; i < L4_PAGESIZE; i += sizeof(i))
		*(unsigned long *)((unsigned long)addr + i) = 0xcafeface;

	/* Detach it again */
	if (l4re_rm_detach(addr)) {
		LOG_printf("%s: Can't unmap ghost page\n", __func__);
		l4x_exit_l4linux();
	}

}

/*
 * We need to get the lowest virtual address and we can find out in the
 * KIP's memory descriptors. Fiasco-UX on system like Ubuntu set the minimum
 * mappable address to 0x10000 so we cannot just plain begin at 0x1000.
 */
static unsigned long get_min_virt_address(void)
{
	l4_kernel_info_t *kip = l4re_kip();
	struct md_t {
		unsigned long _l, _h;
	};
	struct md_t *md
	  = (struct md_t *)((char *)kip +
			    (kip->mem_info >> ((sizeof(unsigned long) / 2) * 8)));
	unsigned long count
	  = kip->mem_info & ((1UL << ((sizeof(unsigned long)/2)*8)) - 1);
	unsigned long i;

	for (i = 0; i < count; ++i, md++) {
		/* Not a virtual descriptor? */
		if (!(md->_l & 0x200))
			continue;

		/* Return start address of descriptor, there should only be
		 * a single one, so just return the first */
		return md->_l & ~0x3ffUL;
	}

	return 0x1000;
}
