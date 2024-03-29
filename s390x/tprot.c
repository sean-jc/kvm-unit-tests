/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TEST PROTECTION tests
 *
 * Copyright IBM Corp. 2022
 *
 * Authors:
 *  Nico Boehr <nrb@linux.ibm.com>
 */

#include <libcflat.h>
#include <bitops.h>
#include <asm/pgtable.h>
#include <asm/interrupt.h>
#include "mmu.h"
#include <vmalloc.h>
#include <sclp.h>

static uint8_t pagebuf[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static void test_tprot_rw(void)
{
	int cc;

	report_prefix_push("Page read/writeable");

	cc = tprot((unsigned long)pagebuf, 0);
	report(cc == 0, "CC = 0");

	report_prefix_pop();
}

static void test_tprot_ro(void)
{
	int cc;

	report_prefix_push("Page readonly");

	protect_dat_entry(pagebuf, PAGE_ENTRY_P, 5);

	cc = tprot((unsigned long)pagebuf, 0);
	report(cc == 1, "CC = 1");

	unprotect_dat_entry(pagebuf, PAGE_ENTRY_P, 5);

	report_prefix_pop();
}

static void test_tprot_low_addr_prot(void)
{
	int cc;

	report_prefix_push("low-address protection");

	low_prot_enable();
	cc = tprot(0, 0);
	low_prot_disable();
	report(cc == 1, "CC = 1");

	report_prefix_pop();
}

static void test_tprot_transl_unavail(void)
{
	int cc;

	report_prefix_push("Page translation unavailable");

	protect_dat_entry(pagebuf, PAGE_ENTRY_I, 5);

	cc = tprot((unsigned long)pagebuf, 0);
	report(cc == 3, "CC = 3");

	unprotect_dat_entry(pagebuf, PAGE_ENTRY_I, 5);

	report_prefix_pop();
}

static void test_tprot_transl_pte_bit52_set(void)
{
	report_prefix_push("PTE Bit 52 set");

	protect_dat_entry(pagebuf, BIT(63 - 52), 5);

	expect_pgm_int();
	tprot((unsigned long)pagebuf, 0);
	check_pgm_int_code(PGM_INT_CODE_TRANSLATION_SPEC);

	unprotect_dat_entry(pagebuf, BIT(63 - 52), 5);

	report_prefix_pop();
}

int main(void)
{
	report_prefix_push("tprot");

	setup_vm();

	test_tprot_rw();
	test_tprot_ro();
	test_tprot_low_addr_prot();
	test_tprot_transl_unavail();
	test_tprot_transl_pte_bit52_set();

	report_prefix_pop();
	return report_summary();
}
