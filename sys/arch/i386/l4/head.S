/*
 *  This file is questionable as we also could define these memory areas
 *  in C files, on the other hand this "asm" code looks very portable...
 */

#include <machine/asm.h>

#ifdef ARCH_x86
.section ".text"
.globl _start
_start:
	mov 0(%esi), %edi
	mov %edi, __l4_external_resolver

	mov $l4_utcb_wrap, %edi
	mov %edi, 4(%esi)

	mov 8(%esi), %edi
	mov %edi, l4re_global_env

	mov 12(%esi), %edi
	mov %edi, l4lx_kinfo

	jmp l4start
.previous
.section ".bss"
ENTRY(swapper_pg_dir)
	.fill 1024,4,0
ENTRY(empty_zero_page)
	.fill 4096,1,0
.previous
#endif

#ifdef ARCH_arm
.section ".text"
.globl _start
_start:
	ldr r4, [r2, #0]
	ldr r5, __address_res
	str r4, [r5]

	ldr r4, __address_l4_utcb_wrap
	str r4, [r2, #4]

	ldr r4, [r2, #8]
	ldr r5, __address_l4re_global_env
	str r4, [r5]

	ldr r4, [r2, #12]
	ldr r5, __address_l4lx_kinfo
	str r4, [r5]

	b start
__address_res:
	.long __l4_external_resolver
__address_l4_utcb_wrap:
	.long l4_utcb_wrap
__address_l4re_global_env:
	.long l4re_global_env
__address_l4lx_kinfo:
	.long l4lx_kinfo
.previous
.section .data, "wa"
.p2align 12
ENTRY(swapper_pg_dir)
	.fill 4096,4,0
#endif
