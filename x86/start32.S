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
#ifdef CONFIG_EFI
	call prepare_64_1
prepare_64_1:
	pop %edx
	add $gdt_descr - prepare_64_1, %edx
#else
	mov $gdt_descr, %edx
#endif
	lgdtl (%edx)

	setup_segments

	xor %eax, %eax
	mov %eax, %cr4

enter_long_mode:
	mov %cr4, %eax
	bts $5, %eax  // pae
	mov %eax, %cr4

#ifdef CONFIG_EFI
	call prepare_64_2
prepare_64_2:
	pop %eax
	add $ptl4 - prepare_64_2, %eax
#else
	mov pt_root, %eax
#endif
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

.globl ap_start32
ap_start32:
	setup_segments

#ifdef CONFIG_EFI
	call ap_start32_1
ap_start32_1:
	pop %edx
	add $smp_stacktop - ap_start32_1, %edx
#else
	mov $smp_stacktop, %edx
#endif
	mov $-4096, %esp
	lock xaddl %esp, (%edx)

	setup_percpu_area
	call prepare_64

#ifdef CONFIG_EFI
	call ap_start32_2
ap_start32_2:
	pop %edx
	add $ap_start64 - ap_start32_2, %edx
#else
	mov $ap_start64, %edx
#endif

	pushl $0x08
	pushl %edx

	lretl
