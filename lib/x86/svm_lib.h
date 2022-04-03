#ifndef SRC_LIB_X86_SVM_LIB_H_
#define SRC_LIB_X86_SVM_LIB_H_

#include <x86/svm.h>
#include "processor.h"

static inline bool svm_supported(void)
{
    return this_cpu_has(X86_FEATURE_SVM);
}

static inline bool npt_supported(void)
{
    return this_cpu_has(X86_FEATURE_NPT);
}

static inline bool vgif_supported(void)
{
    return this_cpu_has(X86_FEATURE_VGIF);
}

static inline bool lbrv_supported(void)
{
    return this_cpu_has(X86_FEATURE_LBRV);
}

static inline bool tsc_scale_supported(void)
{
    return this_cpu_has(X86_FEATURE_TSCRATEMSR);
}

static inline bool pause_filter_supported(void)
{
    return this_cpu_has(X86_FEATURE_PAUSEFILTER);
}

static inline bool pause_threshold_supported(void)
{
    return this_cpu_has(X86_FEATURE_PFTHRESHOLD);
}

static inline void vmmcall(void)
{
    asm volatile ("vmmcall" : : : "memory");
}

static inline void stgi(void)
{
    asm volatile ("stgi");
}

static inline void clgi(void)
{
    asm volatile ("clgi");
}

void vmcb_set_seg(struct vmcb_seg *seg, u16 selector,
                         u64 base, u32 limit, u32 attr);

void setup_svm(void);
void vmcb_ident(struct vmcb *vmcb);

u64 *npt_get_pte(u64 address);
u64 *npt_get_pde(u64 address);
u64 *npt_get_pdpe(void);
u64 *npt_get_pml4e(void);

u8* svm_get_msr_bitmap(void);
u8* svm_get_io_bitmap(void);

#define MSR_BITMAP_SIZE 8192


struct x86_gpr_regs
{
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 cr2;
    u64 rbp;
    u64 rsi;
    u64 rdi;

    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rflags;
};

#define SAVE_GPR_C(regs) \
		"xchg %%rbx, %p[" #regs "]+0x8\n\t"              \
		"xchg %%rcx, %p[" #regs "]+0x10\n\t"             \
		"xchg %%rdx, %p[" #regs "]+0x18\n\t"             \
		"xchg %%rbp, %p[" #regs "]+0x28\n\t"             \
		"xchg %%rsi, %p[" #regs "]+0x30\n\t"             \
		"xchg %%rdi, %p[" #regs "]+0x38\n\t"             \
		"xchg %%r8,  %p[" #regs "]+0x40\n\t"             \
		"xchg %%r9,  %p[" #regs "]+0x48\n\t"             \
		"xchg %%r10, %p[" #regs "]+0x50\n\t"             \
		"xchg %%r11, %p[" #regs "]+0x58\n\t"             \
		"xchg %%r12, %p[" #regs "]+0x60\n\t"             \
		"xchg %%r13, %p[" #regs "]+0x68\n\t"             \
		"xchg %%r14, %p[" #regs "]+0x70\n\t"             \
		"xchg %%r15, %p[" #regs "]+0x78\n\t"             \

#define LOAD_GPR_C(regs)      SAVE_GPR_C(regs)

#define ASM_PRE_VMRUN_CMD(regs)             \
        "vmload %%rax\n\t"                  \
        "mov %p[" #regs "]+0x80, %%r15\n\t" \
        "mov %%r15, 0x170(%%rax)\n\t"       \
        "mov %p[" #regs "], %%r15\n\t"      \
        "mov %%r15, 0x1f8(%%rax)\n\t"       \
        LOAD_GPR_C(regs)                    \

#define ASM_POST_VMRUN_CMD(regs)            \
        SAVE_GPR_C(regs)                    \
        "mov 0x170(%%rax), %%r15\n\t"       \
        "mov %%r15, %p[regs]+0x80\n\t"      \
        "mov 0x1f8(%%rax), %%r15\n\t"       \
        "mov %%r15, %p[regs]\n\t"           \
        "vmsave %%rax\n\t"                  \


#define SVM_BARE_VMRUN(vmcb, regs) \
	asm volatile (                    \
		ASM_PRE_VMRUN_CMD(regs)       \
                "vmrun %%rax\n\t"     \
		ASM_POST_VMRUN_CMD(regs)      \
		:                             \
		: "a" (virt_to_phys(vmcb)),    \
		  [regs] "i" (&regs) \
		: "memory", "r15")


#endif /* SRC_LIB_X86_SVM_LIB_H_ */
