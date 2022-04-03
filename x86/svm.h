#ifndef X86_SVM_H
#define X86_SVM_H

#include "libcflat.h"
#include <x86/svm.h>


#define LBR_CTL_ENABLE_MASK BIT_ULL(0)

struct svm_test {
	const char *name;
	bool (*supported)(void);
	void (*prepare)(struct svm_test *test);
	void (*prepare_gif_clear)(struct svm_test *test);
	void (*guest_func)(struct svm_test *test);
	bool (*finished)(struct svm_test *test);
	bool (*succeeded)(struct svm_test *test);
	int exits;
	ulong scratch;
	/* Alternative test interface. */
	void (*v2)(void);
	int on_vcpu;
	bool on_vcpu_done;
};

typedef void (*test_guest_func)(struct svm_test *);

extern struct x86_gpr_regs regs;

bool smp_supported(void);
bool default_supported(void);
void default_prepare(struct svm_test *test);
void default_prepare_gif_clear(struct svm_test *test);
bool default_finished(struct svm_test *test);
int get_test_stage(struct svm_test *test);
void set_test_stage(struct svm_test *test, int s);
void inc_test_stage(struct svm_test *test);
struct x86_gpr_regs get_regs(void);
int __svm_vmrun(u64 rip);
void __svm_bare_vmrun(void);
int svm_vmrun(void);
void test_set_guest(test_guest_func func);

extern struct vmcb *vmcb;
extern struct svm_test svm_tests[];
#endif
