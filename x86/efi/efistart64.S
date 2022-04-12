/* Startup code and pre-defined data structures */

#include "apic-defs.h"
#include "asm-generic/page.h"
#include "crt0-efi-x86_64.S"

.data

max_cpus = MAX_TEST_CPUS

/* Reserve stack in .data */
	. = . + 4096 * max_cpus
	.align 16
.globl stacktop
stacktop:

.align PAGE_SIZE
.globl ptl2
ptl2:
	. = . + 4 * PAGE_SIZE
.align PAGE_SIZE

.globl ptl3
ptl3:
	. = . + PAGE_SIZE
.align PAGE_SIZE

.globl ptl4
ptl4:
	. = . + PAGE_SIZE
.align PAGE_SIZE

.section .init
.code64
.text

.globl setup_segments64
setup_segments64:
	/* Update data segments */
	mov $0x10, %ax /* 3rd entry in gdt64: 32/64-bit data segment */
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	/* Setup percpu base */
	MSR_GS_BASE = 0xc0000101
	mov %rdi, %rax
	mov $0, %edx
	mov $MSR_GS_BASE, %ecx
	wrmsr

	/*
	 * Update the code segment by putting it on the stack before the return
	 * address, then doing a far return: this will use the new code segment
	 * along with the address.
	 */
	popq %rdi
	pushq $0x08 /* 2nd entry in gdt64: 64-bit code segment */
	pushq %rdi
	lretq

.code16

.globl sipi_entry
sipi_entry:
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0

	/* Retrieve relocated gdt32_descr address at (PAGE_SIZE - 2). */
	mov (PAGE_SIZE - 2), %ebx
	lgdtl (%ebx)

	lcall $0x18, $0x0

.globl gdt32
gdt32:
	.quad 0
	.quad 0x00cf9b000000ffff // flat 32-bit code segment
	.quad 0x00cf93000000ffff // flat 32-bit data segment
	.quad 0                  // call gate to 32-bit AP entrypoint
.globl gdt32_end
gdt32_end:

.globl gdt32_descr
gdt32_descr:
	.word 0
	.long 0

.globl sipi_end
sipi_end:

.code32
.globl ap_start32
ap_start32:
	jmp ap_start32
