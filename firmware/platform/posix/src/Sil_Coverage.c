/**
 * @file    Sil_Coverage.c
 * @brief   Periodic LLVM profile flush for instrumented SIL ECUs
 * @date    2026-08-24
 *
 * The CVC (and any LLVM_COV-instrumented) SIL ECU is a long-running daemon
 * that never exits, so the LLVM profile runtime would otherwise defer
 * writing .profraw until process termination — i.e. never. This module lets
 * the platform loop periodically flush the counters (via
 * __llvm_profile_write_file), so the true-E2E vcan0 tests naturally produce
 * coverage data for the gateway collector.
 *
 * Compiled only when the POSIX build is made with LLVM_COV=1 (which also
 * defines SIL_COVERAGE). SIL simulation only — never part of a physical
 * STM32/TMS570 build.
 */

#if defined(PLATFORM_POSIX) && defined(SIL_COVERAGE)

extern void __llvm_profile_write_file(void);

void Sil_Coverage_Periodic(void)
{
    /* Flush roughly once per 5s of virtual time (1ms tick assumed).
     * The LLVM runtime merges this write into the LLVM_PROFILE_FILE, so
     * repeated flushes accumulate the counters correctly. */
    static unsigned tick_count = 0u;

    tick_count++;
    if (tick_count >= 5000u) {
        tick_count = 0u;
        __llvm_profile_write_file();
    }
}

#endif /* PLATFORM_POSIX && SIL_COVERAGE */