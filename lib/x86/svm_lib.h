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

void setup_svm(void);

u64 *npt_get_pte(u64 address);
u64 *npt_get_pde(u64 address);
u64 *npt_get_pdpe(void);
u64 *npt_get_pml4e(void);

u8* svm_get_msr_bitmap(void);
u8* svm_get_io_bitmap(void);

#define MSR_BITMAP_SIZE 8192


#endif /* SRC_LIB_X86_SVM_LIB_H_ */
