
#include <libcflat.h>
#include "processor.h"
#include "atomic.h"
#include "smp.h"
#include "apic.h"
#include "fwcfg.h"
#include "desc.h"
#include "asm/page.h"

#define IPI_VECTOR 0x20

typedef void (*ipi_function_type)(void *data);

static struct spinlock ipi_lock;
static volatile ipi_function_type ipi_function;
static void *volatile ipi_data;
static volatile int ipi_done;
static volatile bool ipi_wait;
static int _cpu_count;
static atomic_t active_cpus;
extern u8 sipi_entry;
extern u8 sipi_end;
volatile unsigned cpu_online_count = 1;

static __attribute__((used)) void ipi(void)
{
	void (*function)(void *data) = ipi_function;
	void *data = ipi_data;
	bool wait = ipi_wait;

	if (!wait) {
		ipi_done = 1;
		apic_write(APIC_EOI, 0);
	}
	function(data);
	atomic_dec(&active_cpus);
	if (wait) {
		ipi_done = 1;
		apic_write(APIC_EOI, 0);
	}
}

asm (
	 "ipi_entry: \n"
	 "   call ipi \n"
#ifndef __x86_64__
	 "   iret"
#else
	 "   iretq"
#endif
	 );

int cpu_count(void)
{
	return _cpu_count;
}

int smp_id(void)
{
	return this_cpu_read_smp_id();
}

static void setup_smp_id(void *data)
{
	this_cpu_write_smp_id(apic_id());
}

static void __on_cpu(int cpu, void (*function)(void *data), void *data, int wait)
{
	const u32 ipi_icr = APIC_INT_ASSERT | APIC_DEST_PHYSICAL | APIC_DM_FIXED | IPI_VECTOR;
	unsigned int target = id_map[cpu];

	spin_lock(&ipi_lock);
	if (target == smp_id()) {
		function(data);
	} else {
		atomic_inc(&active_cpus);
		ipi_done = 0;
		ipi_function = function;
		ipi_data = data;
		ipi_wait = wait;
		apic_icr_write(ipi_icr, target);
		while (!ipi_done)
			;
	}
	spin_unlock(&ipi_lock);
}

void on_cpu(int cpu, void (*function)(void *data), void *data)
{
	__on_cpu(cpu, function, data, 1);
}

void on_cpu_async(int cpu, void (*function)(void *data), void *data)
{
	__on_cpu(cpu, function, data, 0);
}

void on_cpus(void (*function)(void *data), void *data)
{
	int cpu;

	for (cpu = cpu_count() - 1; cpu >= 0; --cpu)
		on_cpu_async(cpu, function, data);

	while (cpus_active() > 1)
		pause();
}

int cpus_active(void)
{
	return atomic_read(&active_cpus);
}

void smp_init(void)
{
	int i;
	void ipi_entry(void);

	setup_idt();
	init_apic_map();
	set_idt_entry(IPI_VECTOR, ipi_entry, 0);

	setup_smp_id(0);
	for (i = 1; i < cpu_count(); ++i)
		on_cpu(i, setup_smp_id, 0);

	atomic_inc(&active_cpus);
}

static void do_reset_apic(void *data)
{
	reset_apic();
}

void smp_reset_apic(void)
{
	int i;

	reset_apic();
	for (i = 1; i < cpu_count(); ++i)
		on_cpu(i, do_reset_apic, 0);

	atomic_inc(&active_cpus);
}

#ifdef CONFIG_EFI
extern u8 gdt32_descr, gdt32, gdt32_end;
extern u8 ap_start32;
extern u32 smp_stacktop;
extern u8 stacktop;
#endif

void ap_init(void)
{
	u8 *dst_addr = 0;
	size_t sipi_sz = (&sipi_end - &sipi_entry) + 1;

	assert(sipi_sz < PAGE_SIZE);

	asm volatile("cld");

	/* Relocate SIPI vector to dst_addr so it can run in 16-bit mode. */
	memset(dst_addr, 0, PAGE_SIZE);
	memcpy(dst_addr, &sipi_entry, sipi_sz);

#ifdef CONFIG_EFI
	volatile struct descriptor_table_ptr *gdt32_descr_rel;
	idt_entry_t *gate_descr;
	u16 *gdt32_descr_reladdr = (u16 *) (PAGE_SIZE - sizeof(u16));

	smp_stacktop = ((u64) (&stacktop)) - 4096;
	/*
	 * gdt32_descr for CONFIG_EFI needs to be filled here dynamically
	 * since compile time calculation of offsets is not allowed when
	 * building with -shared, and rip-relative addressing is not supported
	 * in 16-bit mode.
	 *
	 * Use the last two bytes of SIPI page to store relocated gdt32_descr
	 * addr.
	 */
	*gdt32_descr_reladdr = (&gdt32_descr - &sipi_entry);

	gdt32_descr_rel = (struct descriptor_table_ptr *) ((u64) *gdt32_descr_reladdr);
	gdt32_descr_rel->limit = (u16) (&gdt32_end - &gdt32 - 1);
	gdt32_descr_rel->base = (ulong) ((u32) (&gdt32 - &sipi_entry));

	/*
	 * EFI may not load the 32-bit AP entrypoint (ap_start32) low enough
	 * to be reachable from the SIPI vector. Since we build with -shared, this
	 * location needs to be fetched at runtime, and rip-relative addressing is
	 * not supported in 16-bit mode.
	 * To perform 16-bit -> 32-bit far jump, our options are:
	 * - ljmpl $cs, $label : unusable since $label is not known at build time.
	 * - push $cs; push $label; lret : requires an intermediate trampoline since
	 *	 $label must still be within 0 - 0xFFFF for 16-bit far return to work.
	 * - lcall into a call-gate : best suited.
	 *
	 * Set up call gate to ap_start32 within GDT.
	 *
	 * gdt32 layout:
	 *
	 * Entry | Segment
	 * 0	 | NULL descr
	 * 1	 | Code segment descr
	 * 2	 | Data segment descr
	 * 3	 | Call gate descr
	 */
	gate_descr = (idt_entry_t *) ((u8 *)(&gdt32 - &sipi_entry)
		+ 3 * sizeof(gdt_entry_t));
	set_idt_entry_t(gate_descr, sizeof(gdt_entry_t), (void *) &ap_start32,
		0x8 /* sel */, 0xc /* type */, 0 /* dpl */);
#endif

	/* INIT */
	apic_icr_write(APIC_DEST_ALLBUT | APIC_DEST_PHYSICAL | APIC_DM_INIT | APIC_INT_ASSERT, 0);

	/* SIPI */
	apic_icr_write(APIC_DEST_ALLBUT | APIC_DEST_PHYSICAL | APIC_DM_STARTUP, 0);

	_cpu_count = fwcfg_get_nb_cpus();

	while (_cpu_count != cpu_online_count) {
		;
	}
}
