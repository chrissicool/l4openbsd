/*
 * Copyright (C) 2010, Christian Ludwig <cludwig@net.t-labs.tu-berlin.de>
 * Copyright (C) 2010, Technische Universitaet Berlin
 *
 * Licensed under the 2-clause BSD license.
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
L4_EXTERNAL_FUNC(LOG_flush);
L4_EXTERNAL_FUNC(l4x_external_exit);	/* jumps back into the wrapper */

L4_EXTERNAL_FUNC(l4util_kip_kernel_has_feature);
L4_EXTERNAL_FUNC(l4util_kip_kernel_abi_version);

#ifdef L4_EXTERNAL_RTC
L4_EXTERNAL_FUNC(l4rtc_get_seconds_since_1970);
#endif

L4_EXTERNAL_FUNC(l4re_debug_obj_debug);
L4_EXTERNAL_FUNC(l4re_ds_size);
L4_EXTERNAL_FUNC(l4re_ds_phys);
L4_EXTERNAL_FUNC(l4re_ma_alloc_srv);
L4_EXTERNAL_FUNC(l4re_rm_attach_srv);
L4_EXTERNAL_FUNC(l4re_rm_detach_srv);
L4_EXTERNAL_FUNC(l4re_rm_find_srv);
L4_EXTERNAL_FUNC(l4re_rm_reserve_area_srv);
L4_EXTERNAL_FUNC(l4re_rm_free_area_srv);
L4_EXTERNAL_FUNC(l4re_rm_show_lists_srv);
L4_EXTERNAL_FUNC(l4re_util_cap_alloc);
L4_EXTERNAL_FUNC(l4re_util_cap_free);

L4_EXTERNAL_FUNC(l4sys_errtostr);
