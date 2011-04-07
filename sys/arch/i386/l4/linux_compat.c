/*
 * Linux compatibility library.
 * This code was taken from he Linux kernel.
 * Suppose it's licensed under the GPL then.
 */

/* XXX we should move this file to a more general location */

#include <machine/l4/linux_compat.h>
#include <machine/l4/bsd_compat.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

static __inline unsigned long __ffs(unsigned long word);

/*
 * ffz - find first zero in word.
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
#define ffz(x)  __ffs(~(x))


/*
 * Find the first set bit in a memory region.
 */
unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) & (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)         /* Are any bits set? */
		return result + size;   /* Nope. */
found:
	return result + __ffs(tmp);
}

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __inline unsigned long __ffs(unsigned long word)
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

inline void bitmap_zero(unsigned long *dst, int nbits)
{
	if (small_const_nbits(nbits))
		*dst = 0ul;
	else {
		size_t len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
		memset(dst, 0, len);
	}
}

inline void bitmap_fill(unsigned long *dst, int nbits)
{
	size_t nlongs = BITS_TO_LONGS(nbits);
	if (!small_const_nbits(nbits)) {
		size_t len = (nlongs - 1) * sizeof(unsigned long);
		memset(dst, 0xff,  len);
	}
	dst[nlongs - 1] = BITMAP_LAST_WORD_MASK(nbits);
}

/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
unsigned long find_next_zero_bit(const u_int32_t *addr, unsigned long size,
				 unsigned long offset)
{
	const u_int32_t *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (BITS_PER_LONG - offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found_middle:
	return result + ffz(tmp);
}

/*
 * Common code for bitmap_*_region() routines.
 *      bitmap: array of unsigned longs corresponding to the bitmap
 *      pos: the beginning of the region
 *      order: region size (log base 2 of number of bits)
 *      reg_op: operation(s) to perform on that region of bitmap
 *
 * Can set, verify and/or release a region of bits in a bitmap,
 * depending on which combination of REG_OP_* flag bits is set.
 *
 * A region of a bitmap is a sequence of bits in the bitmap, of
 * some size '1 << order' (a power of two), aligned to that same
 * '1 << order' power of two.
 *
 * Returns 1 if REG_OP_ISFREE succeeds (region is all zero bits).
 * Returns 0 in all other cases and reg_ops.
 */

enum {
        REG_OP_ISFREE,          /* true if region is all zero bits */
        REG_OP_ALLOC,           /* set all bits in region */
        REG_OP_RELEASE,         /* clear all bits in region */
};

static int __reg_op(unsigned long *bitmap, int pos, int order, int reg_op)
{
        int nbits_reg;          /* number of bits in region */
        int index;              /* index first long of region in bitmap */
        int offset;             /* bit offset region in bitmap[index] */
        int nlongs_reg;         /* num longs spanned by region in bitmap */
        int nbitsinlong;        /* num bits of region in each spanned long */
        unsigned long mask;     /* bitmask for one long of region */
        int i;                  /* scans bitmap by longs */
        int ret = 0;            /* return value */

        /*
         * Either nlongs_reg == 1 (for small orders that fit in one long)
         * or (offset == 0 && mask == ~0UL) (for larger multiword orders.)
         */
        nbits_reg = 1 << order;
        index = pos / BITS_PER_LONG;
        offset = pos - (index * BITS_PER_LONG);
        nlongs_reg = BITS_TO_LONGS(nbits_reg);
        nbitsinlong = min(nbits_reg,  BITS_PER_LONG);

        /*
         * Can't do "mask = (1UL << nbitsinlong) - 1", as that
         * overflows if nbitsinlong == BITS_PER_LONG.
         */
        mask = (1UL << (nbitsinlong - 1));
        mask += mask - 1;
        mask <<= offset;

        switch (reg_op) {
        case REG_OP_ISFREE:
                for (i = 0; i < nlongs_reg; i++) {
                        if (bitmap[index + i] & mask)
                                goto done;
                }
                ret = 1;        /* all bits in region free (zero) */
                break;

        case REG_OP_ALLOC:
                for (i = 0; i < nlongs_reg; i++)
                        bitmap[index + i] |= mask;
                break;

        case REG_OP_RELEASE:
                for (i = 0; i < nlongs_reg; i++)
                        bitmap[index + i] &= ~mask;
                break;
        }
done:
        return ret;
}

/**
 * bitmap_find_free_region - find a contiguous aligned mem region
 *      @bitmap: array of unsigned longs corresponding to the bitmap
 *      @bits: number of bits in the bitmap
 *      @order: region size (log base 2 of number of bits) to find
 *
 * Find a region of free (zero) bits in a @bitmap of @bits bits and
 * allocate them (set them to one).  Only consider regions of length
 * a power (@order) of two, aligned to that power of two, which
 * makes the search algorithm much faster.
 *
 * Return the bit offset in bitmap of the allocated region,
 * or -errno on failure.
 */
int bitmap_find_free_region(unsigned long *bitmap, int bits, int order)
{
        int pos, end;           /* scans bitmap by regions of size order */

        for (pos = 0 ; (end = pos + (1 << order)) <= bits; pos = end) {
                if (!__reg_op(bitmap, pos, order, REG_OP_ISFREE))
                        continue;
                __reg_op(bitmap, pos, order, REG_OP_ALLOC);
                return pos;
        }
        return -ENOMEM;
}

/**
 *      memparse - parse a string with mem suffixes into a number
 *      @ptr: Where parse begins
 *      @retptr: (output) Optional pointer to next char after parse completes
 *
 *      Parses a string into a number.  The number stored at @ptr is
 *      potentially suffixed with %K (for kilobytes, or 1024 bytes),
 *      %M (for megabytes, or 1048576 bytes), or %G (for gigabytes, or
 *      1073741824).  If the number is suffixed with K, M, or G, then
 *      the return value is the number multiplied by one kilobyte, one
 *      megabyte, or one gigabyte, respectively.
 */

unsigned long memparse(const char *ptr, char **retptr)
{
	char *endptr;   /* local pointer to end of parsed string */

	unsigned long ret = strtoul(ptr, &endptr, 0);

	switch (*endptr) {
		case 'G':
		case 'g':
			ret <<= 10;
		case 'M':
		case 'm':
			ret <<= 10;
		case 'K':
		case 'k':
			ret <<= 10;
			endptr++;
		default:
			break;
	}

	if (retptr)
		*retptr = endptr;

	return ret;
}
