/*
 * Copyright (c) 2010 Christian Ludwig <cludwig@net.t-labs.tu-berlin.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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
#include <machine/cpufunc.h>

//#define COPY_DEBUG
#ifdef COPY_DEBUG
#define debug_printf(...)				\
	do {						\
		L4XV_V(n);				\
		L4XV_L(n);				\
		printf("copy(9): " __VA_ARGS__); 	\
		L4XV_U(n);				\
	} while(0)
#else
#define debug_printf(...)
#endif

int	l4x_copyout(void *, void *, size_t);
int	l4x_copyin(void *, void *, size_t);
int	l4x_copyoutstr(char *, char *, size_t, size_t *);
int	l4x_copyinstr(char *, char *, size_t, size_t *);

int
l4x_copyout(void *src, void *dst, size_t len)
{
	struct proc *p = curproc;
	paddr_t dst_p, upper_bound;
	size_t copy_len;
	L4XV_V(n);

	debug_printf("l4x_copyout(%p, %p, %d)\n", src, dst, len);

	/* loop for each page */
	while (len > 0) {
		dst_p = l4x_pmap_walk_pd(p, (vaddr_t)dst,
				VM_PROT_READ|VM_PROT_WRITE);
		if (dst_p == NULL)
			return EFAULT;

		upper_bound = round_page(dst_p + 1);

		if (dst_p + len < upper_bound)
			copy_len = len;
		else
			copy_len = upper_bound - dst_p;

		debug_printf("bcopy(0x%08lx, 0x%08lx, %d)\n", src, dst_p,
		    copy_len);
		L4XV_L(n);
		bcopy(src, (void *)dst_p, copy_len);
		L4XV_U(n);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
	}

	return 0;
}

int
l4x_copyin(void *src, void *dst, size_t len)
{
	struct proc *p = curproc;
	paddr_t src_p, upper_bound;
	size_t copy_len;
	L4XV_V(n);

	debug_printf("l4x_copyin(%p, %p, %d)\n", src, dst, len);

	/* loop for each page */
	while (len > 0) {
		src_p = l4x_pmap_walk_pd(p, (vaddr_t)src, VM_PROT_READ);
		if (src_p == NULL)
			return EFAULT;

		upper_bound = round_page(src_p + 1);

		if (src_p+len < upper_bound)
			copy_len = len;
		else
			copy_len = upper_bound - src_p;

		debug_printf("bcopy(0x%08lx, 0x%08lx, %d)\n", src_p, dst,
		    copy_len);
		L4XV_L(n);
		bcopy((void *)src_p, dst, copy_len);
		L4XV_U(n);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
	}

	return 0;
}

/*
 * Unlike copyoutstr(9), we return the number of bytes left to copy
 * in *tocopy, so the caller's computings work right. [locore.s]
 */
int
l4x_copyoutstr(char *src, char *dst, size_t len, size_t *tocopy)
{
	struct proc *p = curproc;
	char *dst_p;
	paddr_t upper_bound;
	size_t copy_len, copied;
	L4XV_V(n);

	debug_printf("l4x_copyoutstr(%p, %p, %d)\n", src, dst, len);

	if (tocopy)
		*tocopy = len;

	/* loop for each page */
	while (len > 0) {
		dst_p = (char *)l4x_pmap_walk_pd(p, (vaddr_t)dst,
				VM_PROT_READ | VM_PROT_WRITE);
		if (dst_p == NULL)
			return EFAULT;

		upper_bound = round_page((paddr_t)dst_p + 1);

		if ((unsigned)dst_p+len < upper_bound)
			copy_len = len;
		else
			copy_len = (unsigned)upper_bound - (unsigned)dst_p;

		/* Copy byte by byte, we might hit the terminating '\0' */
		L4XV_L(n);
		for (copied = 0; copied < copy_len; copied++) {
			dst_p[copied] = src[copied];
			if (dst_p[copied] == '\0') {
				L4XV_U(n);
				debug_printf("Finally copied %dB from %p to %p\n",
						copied+1, src, dst_p);
				if (tocopy)
					*tocopy -= (copied+1); /* add current iteration */
				return 0;
			}
		}
		L4XV_U(n);

		debug_printf("Copied %dB from %p to %p\n", copied, src, dst_p);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
		if (tocopy)
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
 * Unlike copyinstr(9), we return the number of bytes left to copy
 * in *tocopy, so the caller's computings work right. [locore.s]
 */
int
l4x_copyinstr(char *src, char *dst, size_t len, size_t *tocopy)
{
	struct proc *p = curproc;
	char *src_p;
	paddr_t upper_bound;
	size_t copy_len, copied;
	L4XV_V(n);

	debug_printf("l4x_copyinstr(%p, %p, %d)\n", src, dst, len);

	if (tocopy)
		*tocopy = len;

	/* loop for each page */
	while (len > 0) {
		src_p = (char *)l4x_pmap_walk_pd(p, (vaddr_t)src, VM_PROT_READ);
		if (src_p == NULL)
			return EFAULT;

		upper_bound = round_page((paddr_t)src_p + 1);

		if ((unsigned)src_p+len < upper_bound)
			copy_len = len;
		else
			copy_len = (unsigned)upper_bound - (unsigned)src_p;

		/* Copy byte by byte, we might hit the terminating '\0' */
		L4XV_L(n);
		for (copied = 0; copied < copy_len; copied++) {
			dst[copied] = src_p[copied];
			if (dst[copied] == '\0') {
				L4XV_U(n);
				debug_printf("Finally copied %dB from %p to %p\n",
						copied+1, src_p, dst);
				if (tocopy)
					*tocopy -= (copied+1); /* add current iteration */
				return 0;
			}
		}
		L4XV_U(n);

		debug_printf("Copied %dB from %p to %p\n", copied, src_p, dst);

		src += copy_len;
		dst += copy_len;
		len -= copy_len;
		if (tocopy)
			*tocopy -= copy_len;
	}

	/* There really should be no mapping, but check anyway. */
	if ((vaddr_t)src >= VM_MAXUSER_ADDRESS)
		return EFAULT;

	if (len == 0)
		return ENAMETOOLONG;

	return 0;
}
