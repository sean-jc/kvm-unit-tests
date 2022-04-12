/* Common 32-bit code between EFI and non-EFI bootstrapping. */

.code32

MSR_GS_BASE = 0xc0000101

.macro setup_percpu_area
	lea -4096(%esp), %eax
	mov $0, %edx
	mov $MSR_GS_BASE, %ecx
	wrmsr
.endm

.macro setup_segments
	mov $MSR_GS_BASE, %ecx
	rdmsr

	mov $0x10, %bx
	mov %bx, %ds
	mov %bx, %es
	mov %bx, %fs
	mov %bx, %gs
	mov %bx, %ss

	/* restore MSR_GS_BASE */
	wrmsr
.endm

prepare_64:
	lgdt gdt_descr
	setup_segments

	xor %eax, %eax
	mov %eax, %cr4

enter_long_mode:
	mov %cr4, %eax
	bts $5, %eax  // pae
	mov %eax, %cr4

	mov pt_root, %eax
	mov %eax, %cr3

efer = 0xc0000080
	mov $efer, %ecx
	rdmsr
	bts $8, %eax
	wrmsr

	mov %cr0, %eax
	bts $0, %eax
	bts $31, %eax
	mov %eax, %cr0
	ret

ap_start32:
	setup_segments
	mov $-4096, %esp
	lock xaddl %esp, smp_stacktop
	setup_percpu_area
	call prepare_64
	ljmpl $8, $ap_start64
