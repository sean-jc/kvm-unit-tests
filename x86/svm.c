/*
 * Framework for testing nested virtualization
 */

#include "svm.h"
#include "libcflat.h"
#include "processor.h"
#include "desc.h"
#include "msr.h"
#include "vm.h"
#include "smp.h"
#include "types.h"
#include "alloc_page.h"
#include "isr.h"
#include "apic.h"
#include "vmalloc.h"
#include "svm_lib.h"

struct vmcb *vmcb;

bool smp_supported(void)
{
	return cpu_count() > 1;
}

bool default_supported(void)
{
    return true;
}


void default_prepare(struct svm_test *test)
{
	vmcb_ident(vmcb);
}

void default_prepare_gif_clear(struct svm_test *test)
{
}

bool default_finished(struct svm_test *test)
{
	return true; /* one vmexit */
}


int get_test_stage(struct svm_test *test)
{
	barrier();
	return test->scratch;
}

void set_test_stage(struct svm_test *test, int s)
{
	barrier();
	test->scratch = s;
	barrier();
}

void inc_test_stage(struct svm_test *test)
{
	barrier();
	test->scratch++;
	barrier();
}

static test_guest_func guest_main;

void test_set_guest(test_guest_func func)
{
	guest_main = func;
}

static void test_thunk(struct svm_test *test)
{
	guest_main(test);
	vmmcall();
}

struct x86_gpr_regs regs;

struct x86_gpr_regs get_regs(void)
{
	return regs;
}

// rax handled specially below


struct svm_test *v2_test;


u64 guest_stack[10000];

int __svm_vmrun(u64 rip)
{
	vmcb->save.rip = (ulong)rip;
	vmcb->save.rsp = (ulong)(guest_stack + ARRAY_SIZE(guest_stack));
	regs.rdi = (ulong)v2_test;

	SVM_BARE_VMRUN(vmcb, regs);

	return (vmcb->control.exit_code);
}

int svm_vmrun(void)
{
	return __svm_vmrun((u64)test_thunk);
}

extern u8 vmrun_rip;

static noinline void test_run(struct svm_test *test)
{

	u64 vmcb_phys = virt_to_phys(vmcb);

	irq_disable();
	vmcb_ident(vmcb);

	test->prepare(test);
	guest_main = test->guest_func;
	vmcb->save.rip = (ulong)test_thunk;
	vmcb->save.rsp = (ulong)(guest_stack + ARRAY_SIZE(guest_stack));
	regs.rdi = (ulong)test;
	do {
		struct svm_test *the_test = test;
		u64 the_vmcb = vmcb_phys;
		asm volatile (
			"clgi;\n\t" // semi-colon needed for LLVM compatibility
			"sti \n\t"
			"call *%c[PREPARE_GIF_CLEAR](%[test]) \n \t"
			"mov %[vmcb_phys], %%rax \n\t"
			ASM_PRE_VMRUN_CMD(regs)
			".global vmrun_rip\n\t"		\
			"vmrun_rip: vmrun %%rax\n\t"    \
			ASM_POST_VMRUN_CMD(regs)
			"cli \n\t"
			"stgi"
			: // inputs clobbered by the guest:
			"=D" (the_test),            // first argument register
			"=b" (the_vmcb)             // callee save register!
			: [test] "0" (the_test),
			  [vmcb_phys] "1"(the_vmcb),
			  [PREPARE_GIF_CLEAR] "i" (offsetof(struct svm_test, prepare_gif_clear)),
			  [regs] "i"(&regs)
			: "rax", "rcx", "rdx", "rsi",
			"r8", "r9", "r10", "r11" , "r12", "r13", "r14", "r15",
			"memory");
		++test->exits;
	} while (!test->finished(test));
	irq_enable();

	report(test->succeeded(test), "%s", test->name);

        if (test->on_vcpu)
	    test->on_vcpu_done = true;
}



int matched;

static bool
test_wanted(const char *name, char *filters[], int filter_count)
{
        int i;
        bool positive = false;
        bool match = false;
        char clean_name[strlen(name) + 1];
        char *c;
        const char *n;

        /* Replace spaces with underscores. */
        n = name;
        c = &clean_name[0];
        do *c++ = (*n == ' ') ? '_' : *n;
        while (*n++);

        for (i = 0; i < filter_count; i++) {
                const char *filter = filters[i];

                if (filter[0] == '-') {
                        if (simple_glob(clean_name, filter + 1))
                                return false;
                } else {
                        positive = true;
                        match |= simple_glob(clean_name, filter);
                }
        }

        if (!positive || match) {
                matched++;
                return true;
        } else {
                return false;
        }
}

int main(int ac, char **av)
{
	/* Omit PT_USER_MASK to allow tested host.CR4.SMEP=1. */
	pteval_t opt_mask = 0;
	int i = 0;

	ac--;
	av++;

	__setup_vm(&opt_mask);

	if (!svm_supported()) {
		printf("SVM not availble\n");
		return report_summary();
	}

	setup_svm();

	vmcb = alloc_page();

	for (; svm_tests[i].name != NULL; i++) {
		if (!test_wanted(svm_tests[i].name, av, ac))
			continue;
		if (svm_tests[i].supported && !svm_tests[i].supported())
			continue;
		if (svm_tests[i].v2 == NULL) {
			if (svm_tests[i].on_vcpu) {
				if (cpu_count() <= svm_tests[i].on_vcpu)
					continue;
				on_cpu_async(svm_tests[i].on_vcpu, (void *)test_run, &svm_tests[i]);
				while (!svm_tests[i].on_vcpu_done)
					cpu_relax();
			}
			else
				test_run(&svm_tests[i]);
		} else {
			vmcb_ident(vmcb);
			v2_test = &(svm_tests[i]);
			svm_tests[i].v2();
		}
	}

	if (!matched)
		report(matched, "command line didn't match any tests!");

	return report_summary();
}
