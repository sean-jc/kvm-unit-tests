
#include "svm_lib.h"
#include "libcflat.h"
#include "processor.h"
#include "desc.h"
#include "msr.h"
#include "vm.h"
#include "smp.h"
#include "alloc_page.h"

/* for the nested page table*/
static u64 *pte[2048];
static u64 *pde[4];
static u64 *pdpe;
static u64 *pml4e;

static u8 *io_bitmap;
static u8 io_bitmap_area[16384];

static u8 *msr_bitmap;
static u8 msr_bitmap_area[MSR_BITMAP_SIZE + PAGE_SIZE];


u64 *npt_get_pte(u64 address)
{
    int i1, i2;

    address >>= 12;
    i1 = (address >> 9) & 0x7ff;
    i2 = address & 0x1ff;

    return &pte[i1][i2];
}

u64 *npt_get_pde(u64 address)
{
    int i1, i2;

    address >>= 21;
    i1 = (address >> 9) & 0x3;
    i2 = address & 0x1ff;

    return &pde[i1][i2];
}

u64 *npt_get_pdpe(void)
{
    return pdpe;
}

u64 *npt_get_pml4e(void)
{
    return pml4e;
}

u8* svm_get_msr_bitmap(void)
{
    return msr_bitmap;
}

u8* svm_get_io_bitmap(void)
{
    return io_bitmap;
}

static void set_additional_vcpu_msr(void *msr_efer)
{
    void *hsave = alloc_page();

    wrmsr(MSR_VM_HSAVE_PA, virt_to_phys(hsave));
    wrmsr(MSR_EFER, (ulong)msr_efer | EFER_SVME);
}

void setup_svm(void)
{
    void *hsave = alloc_page();
    u64 *page, address;
    int i,j;

    wrmsr(MSR_VM_HSAVE_PA, virt_to_phys(hsave));
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SVME);

    io_bitmap = (void *) ALIGN((ulong)io_bitmap_area, PAGE_SIZE);

    msr_bitmap = (void *) ALIGN((ulong)msr_bitmap_area, PAGE_SIZE);

    if (!npt_supported())
        return;

    for (i = 1; i < cpu_count(); i++)
        on_cpu(i, (void *)set_additional_vcpu_msr, (void *)rdmsr(MSR_EFER));

    printf("NPT detected - running all tests with NPT enabled\n");

    /*
    * Nested paging supported - Build a nested page table
    * Build the page-table bottom-up and map everything with 4k
    * pages to get enough granularity for the NPT unit-tests.
    */

    address = 0;

    /* PTE level */
    for (i = 0; i < 2048; ++i) {
        page = alloc_page();

        for (j = 0; j < 512; ++j, address += 4096)
                page[j] = address | 0x067ULL;

        pte[i] = page;
    }

    /* PDE level */
    for (i = 0; i < 4; ++i) {
        page = alloc_page();

        for (j = 0; j < 512; ++j)
            page[j] = (u64)pte[(i * 512) + j] | 0x027ULL;

        pde[i] = page;
    }

    /* PDPe level */
    pdpe   = alloc_page();
    for (i = 0; i < 4; ++i)
        pdpe[i] = ((u64)(pde[i])) | 0x27;

    /* PML4e level */
    pml4e    = alloc_page();
    pml4e[0] = ((u64)pdpe) | 0x27;
}
