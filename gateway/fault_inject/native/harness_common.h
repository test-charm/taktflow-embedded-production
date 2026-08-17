/**
 * @file    harness_common.h
 * @brief   Shared helpers for native ASW test harnesses
 * @date    2026-08-17
 *
 * @details Header-only common code shared by all native test harnesses in
 *          this directory. Eliminates per-harness duplication of:
 *            - parse_uint / parse_int value parsing
 *            - the stdin phase-script reader (fgets + strtok_r + key=value
 *              token dispatch) with the "too many phases" guard
 *
 *          Each harness keeps its own Phase struct, defaults, key-dispatch
 *          table, run_phase and output formatting, but delegates the common
 *          line-splitting / tokenization to harness_read_phases().
 *
 * @copyright Taktflow Systems 2026
 */
#ifndef HARNESS_COMMON_H
#define HARNESS_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARNESS_MAX_PHASES      64u
#define HARNESS_LINE_LEN        2048u
#define HARNESS_MAX_PHASES_MSG  "too many phases (max 64)\n"

static inline uint32_t harness_parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 10);
}

static inline int32_t harness_parse_int(const char* s)
{
    return (int32_t)strtol(s, NULL, 10);
}

/* Callbacks for harness_read_phases(). */
typedef void (*harness_phase_reset_fn)(void* phase);             /* set defaults after memset */
typedef int  (*harness_field_setter_fn)(void* phase,             /* apply one key=value token;
                                                                  * return nonzero to abort   */
                                        const char* key,
                                        const char* value);
typedef int  (*harness_phase_finish_fn)(void* phase, size_t index); /* post-line hook;
                                                                      * return nonzero to abort */

/**
 * @brief Read a phase script from stdin into a caller-owned phase array.
 *
 * Each line is one phase: whitespace-separated `key=value` tokens. The
 * phase record is zeroed, reset() (if given) applies defaults, then setter()
 * is invoked once per token. finish() (if given) runs after all tokens of a
 * line, e.g. for cross-field defaults or required-field validation.
 *
 * @param phases    caller-owned array of phase records
 * @param phase_size sizeof(one phase record)
 * @param reset     optional default-initializer
 * @param setter    required key/value dispatcher
 * @param finish    optional post-line hook
 * @return number of phases read, or -1 on error (too many phases / abort)
 */
static inline int harness_read_phases(void* phases, size_t phase_size,
                                      harness_phase_reset_fn reset,
                                      harness_field_setter_fn setter,
                                      harness_phase_finish_fn finish)
{
    char line[HARNESS_LINE_LEN];
    size_t phase_count = 0u;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        char* token;
        char* saveptr = NULL;

        if (phase_count >= HARNESS_MAX_PHASES) {
            fprintf(stderr, HARNESS_MAX_PHASES_MSG);
            return -1;
        }

        {
            void* p = (char*)phases + phase_count * phase_size;
            memset(p, 0, phase_size);
            if (reset != NULL) {
                reset(p);
            }

            token = strtok_r(line, " \t\r\n", &saveptr);
            while (token != NULL) {
                char* eq = strchr(token, '=');
                if (eq != NULL) {
                    *eq = '\0';
                    if (setter(p, token, eq + 1) != 0) {
                        return -1;
                    }
                }
                token = strtok_r(NULL, " \t\r\n", &saveptr);
            }

            if (finish != NULL && finish(p, phase_count) != 0) {
                return -1;
            }
        }
        phase_count++;
    }

    return (int)phase_count;
}

#endif /* HARNESS_COMMON_H */
