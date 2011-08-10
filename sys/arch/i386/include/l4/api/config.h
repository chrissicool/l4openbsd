/*
 * License:
 * This file is largely based on code from the L4Linux project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */

#ifndef __ASM_L4__API_L4ENV__CONFIG_H__
#define __ASM_L4__API_L4ENV__CONFIG_H__

#include <machine/param.h>

/* L4 task number limits */
#define TASK_NO_MAX			255

/* must be 4mb aligned, arm could probably go to bff00000 */
#define L4LX_USER_END_ADDRESS		0xBFC00000
#define L4LX_USER_KERN_AREA_START	0x10000000
#define L4LX_USER_KERN_AREA_END		0x40000000	/* which is where physical
							   memory starts */

#define UPAGE_USER_ADDRESS		(L4LX_USER_KERN_AREA_START - USPACE)
#define UPAGE_USER_ADDRESS_END		(UPAGE_USER_ADDRESS + USPACE)

#ifdef CONFIG_ARM
#define UPAGE_USER_TLS_OFFSET		0x0ff0
#endif

#define PAGE0_PAGE_ADDRESS		0x2000

#endif /* ! __ASM_L4__API_L4ENV__CONFIG_H__ */
