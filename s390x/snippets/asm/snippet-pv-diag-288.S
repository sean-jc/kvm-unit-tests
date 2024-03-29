#include <asm/asm-offsets.h>
.section .text

/* Clean and pre-load registers that are used for diag 288 */
xgr	%r0, %r0
xgr	%r1, %r1
xgr	%r3, %r3
lghi	%r0, 1
lghi	%r1, 2
lghi	%r2, 3

/* Let's jump to the pgm exit label on a PGM */
larl	%r4, exit_pgm
stg     %r4, GEN_LC_PGM_NEW_PSW + 8

/* Execute the diag288 */
diag	%r0, %r2, 0x288

/* Force exit if we don't get a PGM */
diag	0, 0, 0x44

/* Communicate the PGM code via diag9c(easiest) */
exit_pgm:
lh	%r1, GEN_LC_PGM_INT_CODE
diag	%r1, 0, 0x9c
