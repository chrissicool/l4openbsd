#ifndef __ASM_L4__GENERIC__UTIL_H__
#define __ASM_L4__GENERIC__UTIL_H__

#include <l4/re/c/dataspace.h>

/*
int l4x_query_and_get_ds(const char *name, const char *logprefix,
                         l4_cap_idx_t *dscap, void **addr,
                         l4re_ds_stats_t *dsstat);

int l4x_query_and_get_cow_ds(const char *name, const char *logprefix,
                             l4_cap_idx_t *memcap,
                             l4_cap_idx_t *dscap, void **addr,
                             l4re_ds_stats_t *stat,
                             int pass_through_if_rw_src_ds);

int l4x_detach_and_free_ds(l4_cap_idx_t dscap, void *addr);
int l4x_detach_and_free_cow_ds(l4_cap_idx_t memcap,
                               l4_cap_idx_t dscap, void *addr);
*/

int l4x_re_resolve_name(const char *name, l4_cap_idx_t *cap);

#endif /* ! __ASM_L4__GENERIC__UTIL_H__ */
