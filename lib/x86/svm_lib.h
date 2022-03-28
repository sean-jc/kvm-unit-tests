#ifndef SRC_LIB_X86_SVM_LIB_H_
#define SRC_LIB_X86_SVM_LIB_H_

#include <x86/svm.h>
#include "processor.h"

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


#endif /* SRC_LIB_X86_SVM_LIB_H_ */
