/*
 * This code is taken from Linux all over the place, thus it is GPLed.
 */

#ifndef __LINUX_COMPAT_H__
#define __LINUX_COMPAT_H__

/* XXX we should make this more generic */
#define BITS_PER_LONG		32
#define BITS_PER_BYTE           8
#define DIV_ROUND_UP(n,d)	(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

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

#endif /* __LINUX_COMPAT_H__ */
