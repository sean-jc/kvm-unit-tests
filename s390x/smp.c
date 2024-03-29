/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests sigp emulation
 *
 * Copyright 2019 IBM Corp.
 *
 * Authors:
 *    Janosch Frank <frankja@linux.ibm.com>
 */
#include <libcflat.h>
#include <asm/asm-offsets.h>
#include <asm/interrupt.h>
#include <asm/page.h>
#include <asm/facility.h>
#include <asm-generic/barrier.h>
#include <asm/sigp.h>

#include <smp.h>
#include <uv.h>
#include <alloc_page.h>

static int testflag = 0;
#define INVALID_CPU_ADDRESS -4711
#define INVALID_ORDER_CODE 0xFF
struct sigp_invalid_cases {
	int order;
	char message[100];
};
static const struct sigp_invalid_cases cases_invalid_cpu_addr[] = {
	{ SIGP_STOP,                  "stop with invalid CPU address" },
	{ SIGP_START,                 "start with invalid CPU address" },
	{ SIGP_CPU_RESET,             "reset with invalid CPU address" },
	{ INVALID_ORDER_CODE,         "invalid order code and CPU address" },
	{ SIGP_SENSE,                 "sense with invalid CPU address" },
	{ SIGP_STOP_AND_STORE_STATUS, "stop and store status with invalid CPU address" },
};
static const struct sigp_invalid_cases cases_valid_cpu_addr[] = {
	{ INVALID_ORDER_CODE,         "invalid order code" },
};

static uint32_t cpu1_prefix;

static void test_invalid(void)
{
	const struct sigp_invalid_cases *c;
	uint32_t status;
	int cc;
	int i;

	report_prefix_push("invalid parameters");

	for (i = 0; i < ARRAY_SIZE(cases_invalid_cpu_addr); i++) {
		c = &cases_invalid_cpu_addr[i];
		cc = sigp(INVALID_CPU_ADDRESS, c->order, 0, &status);
		report(cc == 3, "%s", c->message);
	}

	for (i = 0; i < ARRAY_SIZE(cases_valid_cpu_addr); i++) {
		c = &cases_valid_cpu_addr[i];
		cc = smp_sigp(1, c->order, 0, &status);
		report(cc == 1, "%s", c->message);
	}

	report_prefix_pop();
}

static void wait_for_flag(void)
{
	while (!testflag)
		mb();
}

static void set_flag(int val)
{
	mb();
	testflag = val;
	mb();
}

static void test_func(void)
{
	set_flag(1);
}

static void test_start(void)
{
	struct psw psw;
	psw.mask = extract_psw_mask();
	psw.addr = (unsigned long)test_func;

	set_flag(0);
	smp_cpu_start(1, psw);
	wait_for_flag();
	report_pass("start");
}

static void test_restart(void)
{
	struct cpu *cpu = smp_cpu_from_idx(1);
	struct lowcore *lc = cpu->lowcore;
	int rc;

	report_prefix_push("restart");
	report_prefix_push("stopped");

	lc->restart_new_psw.mask = extract_psw_mask();
	lc->restart_new_psw.addr = (unsigned long)test_func;

	/* Make sure cpu is stopped */
	smp_cpu_stop(1);
	set_flag(0);
	rc = smp_cpu_restart_nowait(1);
	report(!rc, "return code");
	report(!smp_cpu_stopped(1), "cpu started");
	wait_for_flag();
	report_pass("test flag");

	report_prefix_pop();
	report_prefix_push("running");

	/*
	 * Wait until cpu 1 has set the flag because it executed the
	 * restart function.
	 */
	set_flag(0);
	rc = smp_cpu_restart_nowait(1);
	report(!rc, "return code");
	report(!smp_cpu_stopped(1), "cpu started");
	wait_for_flag();
	report_pass("test flag");

	report_prefix_pop();
	report_prefix_pop();
}

static void test_stop(void)
{
	int rc;

	report_prefix_push("stop");

	rc = smp_cpu_stop_nowait(1);
	report(!rc, "return code");
	report(smp_cpu_stopped(1), "cpu stopped");

	report_prefix_push("stop stopped CPU");
	report(!smp_cpu_stop(1), "STOP succeeds");
	report(smp_cpu_stopped(1), "CPU is stopped");
	report_prefix_pop();

	report_prefix_pop();
}

static void test_stop_store_status(void)
{
	struct cpu *cpu = smp_cpu_from_idx(1);
	struct lowcore *lc = (void *)0x0;

	report_prefix_push("stop store status");
	report_prefix_push("running");
	smp_cpu_restart(1);
	lc->prefix_sa = 0;
	lc->grs_sa[15] = 0;
	smp_cpu_stop_store_status(1);
	mb();
	report(smp_cpu_stopped(1), "cpu stopped");
	report(lc->prefix_sa == (uint32_t)(uintptr_t)cpu->lowcore, "prefix");
	report(lc->grs_sa[15], "stack");
	report_prefix_pop();

	report_prefix_push("stopped");
	lc->prefix_sa = 0;
	lc->grs_sa[15] = 0;
	smp_cpu_stop_store_status(1);
	mb();
	report(smp_cpu_stopped(1), "cpu stopped");
	report(lc->prefix_sa == (uint32_t)(uintptr_t)cpu->lowcore, "prefix");
	report(lc->grs_sa[15], "stack");
	report_prefix_pop();

	report_prefix_pop();
}

static void test_store_status(void)
{
	struct cpu_status *status = alloc_pages_flags(1, AREA_DMA31);
	uint32_t r;
	int cc;

	report_prefix_push("store status at address");
	memset(status, 0, PAGE_SIZE * 2);

	report_prefix_push("invalid CPU address");
	cc = sigp(INVALID_CPU_ADDRESS, SIGP_STORE_STATUS_AT_ADDRESS, (uintptr_t)status, &r);
	report(cc == 3, "returned with CC = 3");
	report_prefix_pop();

	report_prefix_push("running");
	smp_cpu_restart(1);
	smp_sigp(1, SIGP_STORE_STATUS_AT_ADDRESS, (uintptr_t)status, &r);
	report(r == SIGP_STATUS_INCORRECT_STATE, "incorrect state");
	report(!memcmp(status, (void *)status + PAGE_SIZE, PAGE_SIZE),
	       "status not written");
	report_prefix_pop();

	memset(status, 0, PAGE_SIZE);
	report_prefix_push("stopped");
	smp_cpu_stop(1);
	smp_sigp(1, SIGP_STORE_STATUS_AT_ADDRESS, (uintptr_t)status, NULL);
	while (!status->prefix) { mb(); }
	report_pass("status written");
	free_pages(status);
	report_prefix_pop();
	smp_cpu_stop(1);

	report_prefix_pop();
}

static void loop(void)
{
	while (1)
		;
}

static void stpx_and_set_flag(void)
{
	asm volatile (
		"	stpx %[prefix]\n"
		: [prefix] "=Q" (cpu1_prefix)
		:
		:
	);

	set_flag(1);
}

static void test_set_prefix(void)
{
	struct lowcore *new_lc = alloc_pages_flags(1, AREA_DMA31);
	struct cpu *cpu1 = smp_cpu_from_idx(1);
	uint32_t status = 0;
	struct psw new_psw;
	int cc;

	report_prefix_push("set prefix");

	assert(new_lc);

	memcpy(new_lc, cpu1->lowcore, sizeof(struct lowcore));
	new_lc->restart_new_psw.addr = (unsigned long)loop;

	report_prefix_push("running");
	set_flag(0);
	new_psw.addr = (unsigned long)stpx_and_set_flag;
	new_psw.mask = extract_psw_mask();
	smp_cpu_start(1, new_psw);
	wait_for_flag();
	cpu1_prefix = 0xFFFFFFFF;

	cc = smp_sigp(1, SIGP_SET_PREFIX, (unsigned long)new_lc, &status);
	report(cc == 1, "CC = 1");
	report(status == SIGP_STATUS_INCORRECT_STATE, "status = INCORRECT_STATE");

	/*
	 * If the prefix of the other CPU was changed it will enter an endless
	 * loop. Otherwise, it should eventually set the flag.
	 */
	smp_cpu_stop(1);
	set_flag(0);
	smp_cpu_restart(1);
	wait_for_flag();
	report(cpu1_prefix == (uint64_t)cpu1->lowcore, "prefix unchanged");

	report_prefix_pop();

	report_prefix_push("invalid CPU address");

	cc = sigp(INVALID_CPU_ADDRESS, SIGP_SET_PREFIX, (unsigned long)new_lc, &status);
	report(cc == 3, "CC = 3");

	report_prefix_pop();

	free_pages(new_lc);

	report_prefix_pop();

}

static void ecall(void)
{
	unsigned long mask;
	struct lowcore *lc = (void *)0x0;

	expect_ext_int();
	ctl_set_bit(0, CTL0_EXTERNAL_CALL);
	mask = extract_psw_mask();
	mask |= PSW_MASK_EXT;
	load_psw_mask(mask);
	set_flag(1);
	while (lc->ext_int_code != 0x1202) { mb(); }
	report_pass("received");
	set_flag(1);
}

static void test_ecall(void)
{
	struct psw psw;
	psw.mask = extract_psw_mask();
	psw.addr = (unsigned long)ecall;

	report_prefix_push("ecall");
	set_flag(0);

	smp_cpu_start(1, psw);
	wait_for_flag();
	set_flag(0);
	smp_sigp(1, SIGP_EXTERNAL_CALL, 0, NULL);
	wait_for_flag();
	smp_cpu_stop(1);
	report_prefix_pop();
}

static void emcall(void)
{
	unsigned long mask;
	struct lowcore *lc = (void *)0x0;

	expect_ext_int();
	ctl_set_bit(0, CTL0_EMERGENCY_SIGNAL);
	mask = extract_psw_mask();
	mask |= PSW_MASK_EXT;
	load_psw_mask(mask);
	set_flag(1);
	while (lc->ext_int_code != 0x1201) { mb(); }
	report_pass("received");
	set_flag(1);
}

static void test_emcall(void)
{
	struct psw psw;
	int cc;
	psw.mask = extract_psw_mask();
	psw.addr = (unsigned long)emcall;

	report_prefix_push("emcall");
	set_flag(0);

	smp_cpu_start(1, psw);
	wait_for_flag();
	set_flag(0);
	smp_sigp(1, SIGP_EMERGENCY_SIGNAL, 0, NULL);
	wait_for_flag();
	smp_cpu_stop(1);

	report_prefix_push("invalid CPU address");

	cc = sigp(INVALID_CPU_ADDRESS, SIGP_EMERGENCY_SIGNAL, 0, NULL);
	report(cc == 3, "CC = 3");

	report_prefix_pop();

	report_prefix_pop();
}

static void test_cond_emcall(void)
{
	uint32_t status = 0;
	struct psw psw;
	int cc;
	psw.mask = extract_psw_mask() & ~PSW_MASK_IO;
	psw.addr = (unsigned long)emcall;

	report_prefix_push("conditional emergency call");

	if (uv_os_is_guest()) {
		report_skip("unsupported under PV");
		goto out;
	}

	report_prefix_push("invalid CPU address");

	cc = sigp(INVALID_CPU_ADDRESS, SIGP_COND_EMERGENCY_SIGNAL, 0, NULL);
	report(cc == 3, "CC = 3");

	report_prefix_pop();

	report_prefix_push("success");
	set_flag(0);

	smp_cpu_start(1, psw);
	wait_for_flag();
	set_flag(0);
	cc = smp_sigp(1, SIGP_COND_EMERGENCY_SIGNAL, 0, &status);
	report(!cc, "CC = 0");

	wait_for_flag();
	smp_cpu_stop(1);

	report_prefix_pop();

out:
	report_prefix_pop();

}

static void test_sense_running(void)
{
	report_prefix_push("sense_running");
	/* we (CPU0) are running */
	report(smp_sense_running_status(0), "CPU0 sense claims running");
	/* stop the target CPU (CPU1) to speed up the not running case */
	smp_cpu_stop(1);
	/* Make sure to have at least one time with a not running indication */
	while(smp_sense_running_status(1));
	report_pass("CPU1 sense claims not running");
	report_prefix_pop();
}

/* Used to dirty registers of cpu #1 before it is reset */
static void test_func_initial(void)
{
	asm volatile("sfpc %0" :: "d" (0x11));
	lctlg(1, 0x42000UL);
	lctlg(7, 0x43000UL);
	lctlg(13, 0x44000UL);
	set_flag(1);
}

static void test_reset_initial(void)
{
	struct cpu_status *status = alloc_pages_flags(0, AREA_DMA31);
	struct psw psw;
	int i;

	psw.mask = extract_psw_mask();
	psw.addr = (unsigned long)test_func_initial;

	report_prefix_push("reset initial");
	set_flag(0);
	smp_cpu_start(1, psw);
	wait_for_flag();

	smp_sigp(1, SIGP_INITIAL_CPU_RESET, 0, NULL);
	smp_sigp(1, SIGP_STORE_STATUS_AT_ADDRESS, (uintptr_t)status, NULL);

	report_prefix_push("clear");
	report(!status->psw.mask && !status->psw.addr, "psw");
	report(!status->prefix, "prefix");
	report(!status->fpc, "fpc");
	report(!status->cputm, "cpu timer");
	report(!status->todpr, "todpr");
	for (i = 1; i <= 13; i++) {
		report(status->crs[i] == 0, "cr%d == 0", i);
	}
	report(status->crs[15] == 0, "cr15 == 0");
	report_prefix_pop();

	report_prefix_push("initialized");
	report(status->crs[0] == 0xE0UL, "cr0 == 0xE0");
	report(status->crs[14] == 0xC2000000UL, "cr14 == 0xC2000000");
	report_prefix_pop();

	report(smp_cpu_stopped(1), "cpu stopped");
	free_pages(status);
	report_prefix_pop();
}

static void test_local_ints(void)
{
	unsigned long mask;

	/* Open masks for ecall and emcall */
	ctl_set_bit(0, CTL0_EXTERNAL_CALL);
	ctl_set_bit(0, CTL0_EMERGENCY_SIGNAL);
	mask = extract_psw_mask();
	mask |= PSW_MASK_EXT;
	load_psw_mask(mask);
	set_flag(1);
}

static void test_reset(void)
{
	struct psw psw;

	psw.mask = extract_psw_mask();
	psw.addr = (unsigned long)test_func;

	report_prefix_push("cpu reset");
	smp_sigp(1, SIGP_EMERGENCY_SIGNAL, 0, NULL);
	smp_sigp(1, SIGP_EXTERNAL_CALL, 0, NULL);
	smp_cpu_start(1, psw);

	smp_sigp(1, SIGP_CPU_RESET, 0, NULL);
	report(smp_cpu_stopped(1), "cpu stopped");

	set_flag(0);
	psw.addr = (unsigned long)test_local_ints;
	smp_cpu_start(1, psw);
	wait_for_flag();
	report_pass("local interrupts cleared");
	report_prefix_pop();
}

int main(void)
{
	struct psw psw;
	report_prefix_push("smp");

	if (smp_query_num_cpus() == 1) {
		report_skip("need at least 2 cpus for this test");
		goto done;
	}

	/* Setting up the cpu to give it a stack and lowcore */
	psw.mask = extract_psw_mask();
	psw.addr = (unsigned long)test_func;
	smp_cpu_setup(1, psw);
	smp_cpu_stop(1);

	test_start();
	test_invalid();
	test_restart();
	test_stop();
	test_stop_store_status();
	test_store_status();
	test_set_prefix();
	test_ecall();
	test_emcall();
	test_cond_emcall();
	test_sense_running();
	test_reset();
	test_reset_initial();
	smp_cpu_destroy(1);

done:
	report_prefix_pop();
	return report_summary();
}
