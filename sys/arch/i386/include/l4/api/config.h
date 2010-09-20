#ifndef __ASM_L4__API_L4ENV__CONFIG_H__
#define __ASM_L4__API_L4ENV__CONFIG_H__

#include <machine/vmparam.h>

/* L4 task number limits */
#define TASK_NO_MAX			255

/* must be 4mb aligned, arm could probably go to bff00000 */
#define L4LX_USER_END_ADDRESS		(VM_MAXUSER_ADDRESS - USPACE)	/* L4Linux: (0xBFC00000UL) */
#define L4LX_USER_KERN_AREA_START	L4LX_USER_END_ADDRESS
#define L4LX_USER_KERN_AREA_END		(0xD0000000UL)

#define UPAGE_USER_ADDRESS		(L4LX_USER_KERN_AREA_END - USPACE)
#define UPAGE_USER_ADDRESS_END		(UPAGE_USER_ADDRESS + USPACE)

#ifdef CONFIG_ARM
#define UPAGE_USER_TLS_OFFSET		0x0ff0
#endif

#define PAGE0_PAGE_ADDRESS		0x2000

#endif /* ! __ASM_L4__API_L4ENV__CONFIG_H__ */
