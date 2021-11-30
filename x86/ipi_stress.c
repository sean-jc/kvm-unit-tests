#include "libcflat.h"
#include "smp.h"
#include "alloc.h"
#include "apic.h"
#include "processor.h"
#include "isr.h"
#include "asm/barrier.h"
#include "delay.h"
#include "svm.h"
#include "desc.h"
#include "msr.h"
#include "vm.h"
#include "types.h"
#include "alloc_page.h"
#include "vmalloc.h"
#include "svm_lib.h"

u64 num_iterations = -1;
struct x86_gpr_regs regs;
u64 guest_stack[10000];
struct vmcb *vmcb;

volatile u64 *isr_counts;
bool use_svm;
int hlt_allowed = -1;


static int get_random(int min, int max)
{
	/* TODO : use rdrand to seed an PRNG instead */
	u64 random_value = rdtsc() >> 4;

	return min + random_value % (max - min + 1);
}

static void ipi_interrupt_handler(isr_regs_t *r)
{
	isr_counts[smp_id()]++;
	eoi();
}

static void wait_for_ipi(volatile u64 *count)
{
	u64 old_count = *count;
	bool use_halt;

	switch (hlt_allowed) {
	case -1:
		use_halt = get_random(0,10000) == 0;
		break;
	case 0:
		use_halt = false;
		break;
	case 1:
		use_halt = true;
		break;
	default:
		use_halt = false;
		break;
	}

	do {
		if (use_halt)
			asm volatile ("sti;hlt;cli\n");
		else
			asm volatile ("sti;nop;cli");

	} while (old_count == *count);
}

/******************************************************************************************************/

#ifdef __x86_64__
static void l2_guest_wait_for_ipi(volatile u64 *count)
{
	wait_for_ipi(count);
	asm volatile("vmmcall");
}

static void l2_guest_dummy(void)
{
	asm volatile("vmmcall");
}

static void wait_for_ipi_in_l2(volatile u64 *count, struct vmcb *vmcb)
{
	u64 old_count = *count;
	bool irq_on_vmentry = get_random(0,1) == 0;

	vmcb->save.rip = (ulong)l2_guest_wait_for_ipi;
	vmcb->save.rsp = (ulong)(guest_stack + ARRAY_SIZE(guest_stack));
	regs.rdi = (u64)count;

	vmcb->save.rip = irq_on_vmentry ? (ulong)l2_guest_dummy : (ulong)l2_guest_wait_for_ipi;

	do {
		if (irq_on_vmentry)
			vmcb->save.rflags |= X86_EFLAGS_IF;
		else
			vmcb->save.rflags &= ~X86_EFLAGS_IF;

		asm volatile("clgi;nop;sti");
		// GIF is set by VMRUN
		SVM_BARE_VMRUN(vmcb, regs);
		// GIF is cleared by VMEXIT
		asm volatile("cli;nop;stgi");

		assert(vmcb->control.exit_code == SVM_EXIT_VMMCALL);

	} while (old_count == *count);
}
#endif

/******************************************************************************************************/

#define FIRST_TEST_VCPU 1

static void vcpu_init(void *data)
{
	/* To make it easier to see iteration number in the trace */
	handle_irq(0x40, ipi_interrupt_handler);
	handle_irq(0x50, ipi_interrupt_handler);
}

static void vcpu_code(void *data)
{
	int ncpus = cpu_count();
	int cpu = (long)data;

	u64 i;

#ifdef __x86_64__
	if (cpu == 2 && use_svm)
	{
		vmcb = alloc_page();
		vmcb_ident(vmcb);

		// when set, intercept physical interrupts
		//vmcb->control.intercept |= (1 << INTERCEPT_INTR);

		// when set, host IF controls the masking of interrupts while the guest runs
		// guest IF only might allow a virtual interrupt to be injected (if set in int_ctl)
		//vmcb->control.int_ctl |= V_INTR_MASKING_MASK;
	}
#endif

	assert(cpu != 0);

	if (cpu != FIRST_TEST_VCPU)
		wait_for_ipi(&isr_counts[cpu]);

	for (i = 0; i < num_iterations; i++)
	{
		u8 physical_dst = cpu == ncpus -1 ? 1 : cpu + 1;

		// send IPI to a next vCPU in a circular fashion
		apic_icr_write(APIC_INT_ASSERT |
				APIC_DEST_PHYSICAL |
				APIC_DM_FIXED |
				(i % 2 ? 0x40 : 0x50),
				physical_dst);

		if (i == (num_iterations - 1) && cpu != FIRST_TEST_VCPU)
			break;

#ifdef __x86_64__
		// wait for the IPI interrupt chain to come back to us
		if (cpu == 2 && use_svm) {
				wait_for_ipi_in_l2(&isr_counts[cpu], vmcb);
				continue;
		}
#endif

		wait_for_ipi(&isr_counts[cpu]);
	}
}

int main(int argc, void** argv)
{
	int cpu, ncpus = cpu_count();

	assert(ncpus > 2);

	if (argc > 1)
		hlt_allowed = atol(argv[1]);

	if (argc > 2)
		num_iterations = atol(argv[2]);

	setup_vm();

#ifdef __x86_64__
	if (svm_supported()) {
		use_svm = true;
		setup_svm();
	}
#endif

	isr_counts = (volatile u64 *)calloc(ncpus, sizeof(u64));

	printf("found %d cpus\n", ncpus);
	printf("running for %lld iterations - test\n",
		(long long unsigned int)num_iterations);

	/*
	 * Ensure that we don't have interrupt window pending
	 * from PIT timer which inhibits the AVIC.
	 */

	asm volatile("sti;nop;cli\n");

	for (cpu = 0; cpu < ncpus; ++cpu)
		on_cpu_async(cpu, vcpu_init, (void *)(long)cpu);

	/* now let all the vCPUs end the IPI function*/
	while (cpus_active() > 1)
		  pause();

	printf("starting test on all cpus but 0...\n");

	for (cpu = ncpus-1; cpu >= FIRST_TEST_VCPU; cpu--)
		on_cpu_async(cpu, vcpu_code, (void *)(long)cpu);

	printf("test started, waiting to end...\n");

	while (cpus_active() > 1) {

		unsigned long isr_count1, isr_count2;

		isr_count1 = isr_counts[1];
		delay(5ULL*1000*1000*1000);
		isr_count2 = isr_counts[1];

		if (isr_count1 == isr_count2) {
			printf("\n");
			printf("hang detected!!\n");
			//break;
		} else {
			printf("made %ld IPIs \n", (isr_count2 - isr_count1)*(ncpus-1));
		}
	}

	printf("\n");

	for (cpu = 1; cpu < ncpus; ++cpu)
		report(isr_counts[cpu] == num_iterations,
				"Number of IPIs match (%lld)",
				(long long unsigned int)isr_counts[cpu]);

	free((void*)isr_counts);
	return report_summary();
}
