/*
 * Process handling functions.
 *
 *  - copy data from/to userspace
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/errno.h>

#include <uvm/uvm_extern.h>
#include <machine/cpu.h>

//#define COPY_DEBUG
#ifdef COPY_DEBUG
#define debug_printf(...)	printf("copy(9): " __VA_ARGS__)
#else
#define debug_printf(...)
#endif

paddr_t	*l4x_run_uvm_fault(vm_map_t map, vaddr_t uva, vm_prot_t access_type);
int	l4x_copyout(void *, void *, size_t);
int	l4x_copyin(void *, void *, size_t);
int	l4x_copyoutstr(char *, char *, size_t, size_t *);
int	l4x_copyinstr(char *, char *, size_t, size_t *);

/*
 * Conditionally run the page fault handler on the given map,
 * if user space address uva is not already mapped.
 * Return the equivalent physical RAM address.
 */
paddr_t *
l4x_run_uvm_fault(vm_map_t map, vaddr_t uva, vm_prot_t access_type)
{
	struct pmap *pmap = map->pmap;
	pd_entry_t *pd;
	pt_entry_t *ptes, *pte;

	if (pmap == NULL)
		return NULL;

	pd = pmap_map_pdes(pmap);			/* lock pmap */
	ptes = (pt_entry_t *)pd[pdei(uva)];
	pmap_unmap_pdes(pmap);				/* unlock pmap */

	if (!ptes && !pmap_valid_entry((pt_entry_t)ptes)) {
		if (uvm_fault(map, uva, 0, access_type))
			return NULL;

		debug_printf("Page was not mapped in\n");
		pd = pmap_map_pdes(pmap);		/* lock pmap */
		ptes = (pt_entry_t *)pd[pdei(uva)];
		pmap_unmap_pdes(pmap);			/* unlock pmap */
	}

	pte = (pt_entry_t *)ptes[ptei(uva)];
	if (!pte && !pmap_valid_entry((pt_entry_t)pte)) {
		/* This should not happen, but better safe than sorry. */
		if (uvm_fault(map, uva, 0, access_type))
			return NULL;

		debug_printf("Page was not mapped in\n");
		pte = (pt_entry_t *)ptes[ptei(uva)];
	}

	return ((paddr_t *)vtophys(uva));
}

int
l4x_copyout(void *src, void *dst, size_t len)
{
	struct vm_map *map;
	paddr_t *dst_p, upper_bound;
	size_t copy_len;

	debug_printf("l4x_copyout(%p, %p, %d)\n", src, dst, len);

	if (curproc->p_vmspace == NULL ||
	    &curproc->p_vmspace->vm_map == NULL)
		return EFAULT;

	map = &curproc->p_vmspace->vm_map;

	/* loop for each page */
	while (len > 0) {
		dst_p = l4x_run_uvm_fault(map, (vaddr_t)dst,
				VM_PROT_READ|VM_PROT_WRITE);
		if (dst_p == NULL)
			return EFAULT;

		upper_bound = round_page((paddr_t)dst_p + 1);

		if ((unsigned)dst_p+len <= upper_bound)
			copy_len = len;
		else
			copy_len = (unsigned)upper_bound - (unsigned)dst_p;

		debug_printf("bcopy(%p, %p, %d)\n", src, dst_p, copy_len);
		bcopy(src, dst_p, copy_len);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
	}

	return 0;
}

int
l4x_copyin(void *src, void *dst, size_t len)
{
	struct vm_map *map;
	paddr_t *src_p, upper_bound;
	size_t copy_len;

	debug_printf("l4x_copyin(%p, %p, %d)\n", src, dst, len);

	if (curproc->p_vmspace == NULL ||
	    &curproc->p_vmspace->vm_map == NULL)
		return EFAULT;

	map = &curproc->p_vmspace->vm_map;

	/* loop for each page */
	while (len > 0) {
		src_p = l4x_run_uvm_fault(map, (vaddr_t)src, VM_PROT_READ);
		if (src_p == NULL)
			return EFAULT;

		upper_bound = round_page((paddr_t)src_p + 1);

		if ((unsigned)src_p+len <= upper_bound)
			copy_len = len;
		else
			copy_len = (unsigned)upper_bound - (unsigned)src_p;

		debug_printf("bcopy(%p, %p, %d)\n", src_p, dst, copy_len);
		bcopy(src_p, dst, copy_len);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
	}

	return 0;
}

/*
 * Unless copyoutstr(9), we return the number of bytes left to copy
 * in *tocopy, so the caller's computings work right. [locore.s]
 */
int
l4x_copyoutstr(char *src, char *dst, size_t len, size_t *tocopy)
{
	struct vm_map *map;
	char *dst_p;
	paddr_t upper_bound;
	size_t copy_len, copied;

	debug_printf("l4x_copyoutstr(%p, %p, %d)\n", src, dst, len);

	if (curproc->p_vmspace == NULL ||
	    &curproc->p_vmspace->vm_map == NULL)
		return EFAULT;

	map = &curproc->p_vmspace->vm_map;
	*tocopy = len;

	/* loop for each page */
	while (len > 0) {
		dst_p = (char *)l4x_run_uvm_fault(map, (vaddr_t)dst,
				VM_PROT_READ | VM_PROT_WRITE);
		if (dst_p == NULL)
			return EFAULT;

		upper_bound = round_page((paddr_t)dst_p + 1);

		if ((unsigned)dst_p+len <= upper_bound)
			copy_len = len;
		else
			copy_len = (unsigned)upper_bound - (unsigned)dst_p;

		/* Copy byte by byte, we might hit the terminating '\0' */
		for (copied = 0; copied < copy_len; copied++) {
			dst_p[copied] = src[copied];
			if (dst_p[copied] == '\0') {
				debug_printf("Finally copied %dB from %p to %p\n",
						copied+1, src, dst_p);
				*tocopy -= (copied+1); /* add current iteration */
				return 0;
			}
		}

		debug_printf("Copied %dB from %p to %p\n", copied, src, dst_p);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
		*tocopy -= copy_len;
	}

	/* There really should be no mapping, but check anyway. */
	if ((vaddr_t)dst >= VM_MAXUSER_ADDRESS)
		return EFAULT;

	if (len == 0)
		return ENAMETOOLONG;

	return 0;
}

/*
 * Unless copyinstr(9), we return the number of bytes left to copy
 * in *tocopy, so the caller's computings work right. [locore.s]
 */
int
l4x_copyinstr(char *src, char *dst, size_t len, size_t *tocopy)
{
	struct vm_map *map;
	char *src_p;
	paddr_t upper_bound;
	size_t copy_len, copied;

	debug_printf("l4x_copyinstr(%p, %p, %d)\n", src, dst, len);

	if (curproc->p_vmspace == NULL ||
	    &curproc->p_vmspace->vm_map == NULL)
		return EFAULT;

	map = &curproc->p_vmspace->vm_map;
	*tocopy = len;

	/* loop for each page */
	while (len > 0) {
		src_p = (char *)l4x_run_uvm_fault(map, (vaddr_t)src, VM_PROT_READ);
		if (src_p == NULL)
			return EFAULT;

		upper_bound = round_page((paddr_t)src_p + 1);

		if ((unsigned)src_p+len <= upper_bound)
			copy_len = len;
		else
			copy_len = (unsigned)upper_bound - (unsigned)src_p;

		/* Copy byte by byte, we might hit the terminating '\0' */
		for (copied = 0; copied < copy_len; copied++) {
			dst[copied] = src_p[copied];
			if (dst[copied] == '\0') {
				debug_printf("Finally copied %dB from %p to %p\n",
						copied+1, src_p, dst);
				*tocopy -= (copied+1); /* add current iteration */
				return 0;
			}
		}

		debug_printf("Copied %dB from %p to %p\n", copied, src_p, dst);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
		*tocopy -= copy_len;
	}

	/* There really should be no mapping, but check anyway. */
	if ((vaddr_t)src >= VM_MAXUSER_ADDRESS)
		return EFAULT;

	if (len == 0)
		return ENAMETOOLONG;

	return 0;
}
