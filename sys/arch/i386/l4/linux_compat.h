#ifndef __LINUX_COMPAT_H__
#define __LINUX_COMPAT_H__

/* XXX we should make this more generic */
#define BITS_PER_LONG 32

unsigned long find_first_bit(const unsigned long *addr, unsigned long size);

#endif /* __LINUX_COMPAT_H__ */
