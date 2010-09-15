#ifndef __ASM_L4__API_L4ENV__MACROS_H__
#define __ASM_L4__API_L4ENV__MACROS_H__

/* place holder for thread id in printf */
#define PRINTF_L4TASK_FORM	"%lx"
#define PRINTF_L4TASK_ARG(t)	(t >> L4_CAP_SHIFT)

#endif /* ! __ASM_L4__API_L4ENV__MACROS_H__ */
