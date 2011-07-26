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

#include <machine/l4/l4lib.h>

/*
 * Define all externally used L4re library functions.
 * They will be declared as weak symbols in the final image.
 * An external resolver wraps the image and hooks in to
 * resolve these functions.
 *
 * You just need to make sure this files gets compiled, the
 * declaration can be found in the respective L4 header files.
 */

L4_EXTERNAL_FUNC(LOG_printf);
L4_EXTERNAL_FUNC(LOG_vprintf);
L4_EXTERNAL_FUNC(LOG_flush);
L4_EXTERNAL_FUNC(l4x_external_exit);	/* jumps back into the wrapper */

L4_EXTERNAL_FUNC(l4util_kip_kernel_has_feature);
L4_EXTERNAL_FUNC(l4util_kip_kernel_abi_version);
L4_EXTERNAL_FUNC(l4util_micros2l4to);

L4_EXTERNAL_FUNC(l4io_request_irq);

#ifdef L4_EXTERNAL_RTC
L4_EXTERNAL_FUNC(l4rtc_get_seconds_since_1970);
#endif

#ifdef RAMDISK_HOOKS
L4_EXTERNAL_FUNC(l4_sleep);
L4_EXTERNAL_FUNC(l4re_ds_info);
L4_EXTERNAL_FUNC(l4re_ds_copy_in);
L4_EXTERNAL_FUNC(l4re_ma_free_srv);
#endif

L4_EXTERNAL_FUNC(l4re_debug_obj_debug);
L4_EXTERNAL_FUNC(l4re_ds_size);
L4_EXTERNAL_FUNC(l4re_ds_phys);
L4_EXTERNAL_FUNC(l4re_ma_alloc_srv);
L4_EXTERNAL_FUNC(l4re_ns_query_to_srv);
L4_EXTERNAL_FUNC(l4re_rm_attach_srv);
L4_EXTERNAL_FUNC(l4re_rm_detach_srv);
L4_EXTERNAL_FUNC(l4re_rm_find_srv);
L4_EXTERNAL_FUNC(l4re_rm_reserve_area_srv);
L4_EXTERNAL_FUNC(l4re_rm_free_area_srv);
L4_EXTERNAL_FUNC(l4re_rm_show_lists_srv);
L4_EXTERNAL_FUNC(l4re_util_cap_alloc);
L4_EXTERNAL_FUNC(l4re_util_cap_free);
L4_EXTERNAL_FUNC(l4re_util_kumem_alloc);

L4_EXTERNAL_FUNC(l4sys_errtostr);
