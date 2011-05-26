/*
 * setupmem.c -- basic physmem allocator
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/vmparam.h>
#include <machine/pmap.h>

#include <machine/l4/bsd_compat.h>
#include <machine/l4/linux_compat.h>
#include <machine/l4/setup.h>
#include <machine/l4/l4lxapi/memory.h>
#include <machine/l4/api/config.h>

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

/* Default memory size */
#ifdef L4_MEMSIZE
unsigned long l4x_mainmem_size = L4_MEMSIZE << 20;
#else
#warning Please define L4_MEMSIZE in your kernel config, for default memory size.
#endif

l4re_ds_t l4x_ds_mainmem;
void *l4x_main_memory_start;	/* paddr_t */
l4re_ds_t l4x_ds_isa_dma;
static void *l4x_isa_dma_memory_start;
static unsigned long l4x_isa_dma_size   = 0;

unsigned long l4x_kvmem_size;
l4re_ds_t l4x_ds_kvmem;
void *l4x_kv_memory_start;	/* vaddr_t */

static void l4x_setup_memory(char **cmdl,
                             vaddr_t *main_mem_startv,
                             vaddr_t *main_mem_endv,
                             vaddr_t *isa_dma_mem_startv,
                             vaddr_t *isa_dma_mem_endv);

/*
 * Allocate a range of contiguous memory host virtual memory from L4.
 * Use it as OpenBSD physical memory.
 */
void l4x_memory_setup(char **cmdl)
{
	paddr_t mem_startp, mem_endp, isa_startp, isa_endp;

	l4x_setup_memory(cmdl, &mem_startp, &mem_endp, &isa_startp, &isa_endp);

	if (!mem_startp)
		enter_kdebug("Could not get requested main memory!");

	/*
	 * Fill global parameters from machdep.c
	 * These are used on other occasions (PCI, ISA, ...)
	 */
	extern paddr_t avail_end;

	avail_end = mem_endp;
}

static void l4x_setup_memory(char **cmdl,
                             paddr_t *main_mem_startp,
                             paddr_t *main_mem_endp,
                             paddr_t *isa_dma_mem_startp,
                             paddr_t *isa_dma_mem_endp)
{
	unsigned long res;
	char *memstr, **i_cmdl;
	unsigned long memory_area_size;
	l4_addr_t memory_area_id;
	l4_addr_t memory_area_addr;
	l4_addr_t kvm_area_id;
//	l4_addr_t kvm_area_addr;

	//l4_size_t poolsize, poolfree;
	l4_uint32_t dm_flags = L4RE_MA_CONTINUOUS | L4RE_MA_PINNED;

	/* See if we find a mem=xxx option in the command line */
	i_cmdl = cmdl;
	while(*i_cmdl) {
		if ((memstr = strstr(*i_cmdl, "mem="))
				&& (res = memparse(memstr + 4, &memstr))) {
			l4x_mainmem_size = res;
		}
		i_cmdl++;
	}

	LOG_printf("utcb %p\n", l4_utcb());

	/*
	 * Compute space and place to map our "physical" memory in our VM.
	 * Pretend to have the kernel mapped in at the front, so there is no
	 * need to waste real memory for it.
	 */

	memory_area_id = PA_START;
	LOG_printf("Main memory size: %ldMB\n", l4x_mainmem_size >> 20);
	if (l4x_mainmem_size < (4 << 20)) {
		LOG_printf("Need at least 4MB RAM - aborting!\n");
		l4x_exit_l4linux();
	}
	if ((l4x_mainmem_size % L4_SUPERPAGESIZE) == 0) {
		LOG_printf("%s: Forcing superpages for main memory\n", __func__);
		/* force ds-mgr to allocate superpages */
		dm_flags |= L4RE_MA_SUPER_PAGES;
	}

	/* Allocate main memory */
	if (l4_is_invalid_cap(l4x_ds_mainmem = l4re_util_cap_alloc())) {
		LOG_printf("%s: No capability for main memory left\n", __func__);
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
	                         L4_PAGESHIFT)) {
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
	                   l4x_ds_mainmem, 0, L4_PAGESHIFT)) {
		LOG_printf("Error attaching to L4Linux main memory\n");
		l4x_exit_l4linux();
	}

	/* Release area ... make possible hole available again */
//	if (l4re_rm_free_area(memory_area_id)) {
//		LOG_printf("Error releasing area\n");
//		l4x_exit_l4linux();
//	}

	*main_mem_startp = (paddr_t)l4x_main_memory_start;
	*main_mem_endp   = (paddr_t)(l4x_main_memory_start + l4x_mainmem_size - 1);

	if (l4_is_invalid_cap(l4x_ds_isa_dma))
		*isa_dma_mem_startp = *isa_dma_mem_endp = 0;
	else {
		*isa_dma_mem_startp = (paddr_t)l4x_isa_dma_memory_start;
		*isa_dma_mem_endp   = (paddr_t)(l4x_isa_dma_memory_start
				+ l4x_isa_dma_size - 1);
	}

	if (!l4_is_invalid_cap(l4x_ds_isa_dma))
		l4x_register_region(l4x_ds_isa_dma, l4x_isa_dma_memory_start,
		                      0, "ISA DMA memory");
	l4x_register_region(l4x_ds_mainmem, l4x_main_memory_start,
	                    0, "Main memory");

	/*
	 * Reserve some part of the virtual address space
	 * as kernel virtual memory for use by uvm_km_alloc(9).
	 */
	kvm_area_id = (l4_addr_t)KVA_START;
	l4x_kvmem_size = L4LX_USER_KERN_AREA_END - KVA_START;
	if (l4re_rm_reserve_area(&kvm_area_id,
	                          l4x_kvmem_size,
	                          L4RE_RM_SEARCH_ADDR, L4_PAGESHIFT)) {
		LOG_printf("%s: Error reserving kernel virtual memory!\n", __func__);
		l4x_exit_l4linux();
	}
	l4x_kv_memory_start = (void *)kvm_area_id;
	LOG_printf("Registered kernel virtual memory at: VA=0x%08lx, size=0x%08lx\n",
			kvm_area_id, l4x_kvmem_size);

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

	// that happened with some version of ld...
	if ((unsigned long)esym < 0x100000)
		LOG_printf("esym == %p, unreasonable small\n", esym);

	l4x_register_pointer_section((void *)((unsigned long)&esym - 1), 0, "esym");

	return;
}

extern u_long atdevbase;	/* KVA of ISA memory hole */
extern int nkpde;		/* number of kernel page tables (pmap.c) */

extern struct user *proc0paddr;

/*
 * Setup kernel page table directory, return the PA of the lowest
 * free memory region.
 *  PA starts at: l4x_main_memory_start
 * KVA starts at: l4x_kv_memory_start
 */
paddr_t
l4x_setup_kernel_ptd(void)
{
	pd_entry_t *pd;
	int kva_off = 0, pa_off = 0;
	int i;

	/*
	 * Since we are running as a monolithic kernel BLOB,
	 * we do not need to map the kernel in a special way.
	 */

	if (nkpde < NKPTP_MIN)
		nkpde = NKPTP_MIN;
	if (nkpde > NKPTP_MAX)
		nkpde = NKPTP_MAX;

	/* Clear physical memory for a page directory and bootstrap tables. */
	bzero((void *)(PA_START+pa_off), (nkpde+1) * NBPG);

	/* Map our PTD  */
	l4lx_memory_map_virtual_page((vaddr_t)(KVA_START+kva_off),
			(paddr_t)(PA_START+pa_off), PG_V | PG_KW);
	proc0paddr->u_pcb.pcb_cr3 = PA_START + pa_off;
	pd = PTD = (pd_entry_t *)(KVA_START + kva_off);
	kva_off += PAGE_SIZE;
	pa_off += PAGE_SIZE;


	/* Reserve nkpde page tables. */
	for (i = pdei(KVA_START); i < pdei(KVA_START) + nkpde; i++) {
		pd[i] = (pd_entry_t)((PA_START + pa_off) |
				     (PG_V | PG_KW | PG_M | PG_U));
		pa_off += PAGE_SIZE;
	}

	/* The following implicitly sets KVA_START for pmap(9). */
	atdevbase = KVA_START + kva_off;

	return(PA_START + pa_off);
}
