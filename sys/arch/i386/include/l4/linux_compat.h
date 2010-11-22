/*
 * This code is taken from Linux all over the place, thus it is GPLed.
 */

#ifndef __LINUX_COMPAT_H__
#define __LINUX_COMPAT_H__

#include <machine/param.h>
#include <sys/types.h>

/* XXX we should make this more generic */
#define BITS_PER_LONG		32
#define BITS_PER_BYTE           8
#define DIV_ROUND_UP(n,d)	(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

#define BITMAP_LAST_WORD_MASK(nbits)                                    \
(	                                                                \
	((nbits) % BITS_PER_LONG) ?                                     \
	        (1UL<<((nbits) % BITS_PER_LONG))-1 : ~0UL               \
)

#define small_const_nbits(nbits) \
	(__builtin_constant_p(nbits) && (nbits) <= BITS_PER_LONG)


unsigned long find_first_bit(const unsigned long *addr, unsigned long size);
inline void bitmap_zero(unsigned long *dst, int nbits);
inline void bitmap_fill(unsigned long *dst, int nbits);
unsigned long find_next_zero_bit(const u_int32_t *addr, unsigned long size,
				 unsigned long offset);
#define find_first_zero_bit(addr, size) find_next_zero_bit((addr), (size), 0)
int bitmap_find_free_region(unsigned long *bitmap, int bits, int order);

/* XXX This is most probably wrong */
#define THREAD_SIZE		(2 * PAGE_SIZE)

unsigned long long memparse(const char *ptr, char **retptr);

/* form include/kernel.h */

/* Force a compilation error if condition is true */
#define BUILD_BUG_ON(condition) ((void)BUILD_BUG_ON_ZERO(condition))

/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))

#endif /* __LINUX_COMPAT_H__ */
