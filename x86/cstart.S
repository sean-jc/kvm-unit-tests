
#include "apic-defs.h"

.global online_cpus

ipi_vector = 0x20

max_cpus = MAX_TEST_CPUS

.bss

	. = . + 4096 * max_cpus
	.align 16
stacktop:

.data

.align 4096
pt:
i = 0
        .rept 1024
        .long 0x1e7 | (i << 22)
        i = i + 1
        .endr

.section .init

.code32

mb_magic = 0x1BADB002
mb_flags = 0x0

	# multiboot header
	.long mb_magic, mb_flags, 0 - (mb_magic + mb_flags)
mb_cmdline = 16

.macro setup_tr_and_percpu
	lidt idt_descr
	push %esp
	call setup_tss
	addl $4, %esp
	ltr %ax
	add $(max_cpus * 8), %ax
	mov %ax, %gs
.endm

.macro setup_segments
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
.endm

.globl start
start:
        lgdtl gdt_descr
        setup_segments
        mov $stacktop, %esp

        push %ebx
        call setup_multiboot
        addl $4, %esp
        call setup_libcflat
        mov mb_cmdline(%ebx), %eax
        mov %eax, __args
        call __setup_args
        call prepare_32
        jmpl $8, $start32

prepare_32:
	mov $(1 << 4), %eax // pse
	mov %eax, %cr4

	mov $pt, %eax
	mov %eax, %cr3

	mov %cr0, %eax
	bts $0, %eax
	bts $31, %eax
	mov %eax, %cr0
	ret

smp_stacktop:	.long stacktop - 4096

save_id:
	movl $(APIC_DEFAULT_PHYS_BASE + APIC_ID), %eax
	movl (%eax), %eax
	shrl $24, %eax
	lock btsl %eax, online_cpus
	retl

ap_start32:
	setup_segments
	mov $-4096, %esp
	lock xaddl %esp, smp_stacktop
	setup_tr_and_percpu
	call prepare_32
	call reset_apic
	call save_id
	call enable_apic
	call enable_x2apic
	sti
	nop
	lock incw cpu_online_count

1:	hlt
	jmp 1b

start32:
	setup_tr_and_percpu
	call reset_apic
	call save_id
	call mask_pic_interrupts
	call enable_apic
	call ap_init
	call enable_x2apic
	call smp_init
        push $__environ
        push $__argv
        push __argc
        call main
	push %eax
	call exit

ap_init:
	cld
	sgdtl ap_gdt_descr // must be close to sipi_entry for real mode access to work
	lea sipi_entry, %esi
	xor %edi, %edi
	mov $(sipi_end - sipi_entry), %ecx
	rep movsb
	mov $APIC_DEFAULT_PHYS_BASE, %eax
	movl $(APIC_DEST_ALLBUT | APIC_DEST_PHYSICAL | APIC_DM_INIT | APIC_INT_ASSERT), APIC_ICR(%eax)
	movl $(APIC_DEST_ALLBUT | APIC_DEST_PHYSICAL | APIC_DM_STARTUP), APIC_ICR(%eax)
	call fwcfg_get_nb_cpus
1:	pause
	cmpw %ax, cpu_online_count
	jne 1b
	ret

online_cpus:
	.fill (max_cpus + 7) / 8, 1, 0

.align 2
cpu_online_count:	.word 1

.code16
sipi_entry:
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0
	lgdtl ap_gdt_descr - sipi_entry
	ljmpl $8, $ap_start32

ap_gdt_descr:
	.word 0
	.long 0

sipi_end:
