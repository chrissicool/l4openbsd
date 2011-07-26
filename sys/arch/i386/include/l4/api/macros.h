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

#ifndef __ASM_L4__API_L4ENV__MACROS_H__
#define __ASM_L4__API_L4ENV__MACROS_H__

/* place holder for thread id in printf */
#define PRINTF_L4TASK_FORM	"%lx"
#define PRINTF_L4TASK_ARG(t)	(t >> L4_CAP_SHIFT)

#endif /* ! __ASM_L4__API_L4ENV__MACROS_H__ */
