/**
 * @file timing.c
 * @brief Il conteggio del tempo. Vedi timing.h per il perche'.
 *
 * SPDX-License-Identifier: MIT
 */

#include "timing.h"

void timing_init(timing_t *t, int64_t now_us)
{
    if (t == NULL) {
        return;
    }

    t->last_us = now_us;
    t->dt_ms = (uint32_t)(TIMING_MIN_STEP_US / 1000);
    t->now_ms = (uint32_t)(now_us / 1000);
}

void timing_step(timing_t *t, int64_t now_us)
{
    if (t == NULL) {
        return;
    }

    int64_t elapsed_us = now_us - t->last_us;
    t->last_us = now_us;

    if (elapsed_us < (int64_t)TIMING_MIN_STEP_US) {
        elapsed_us = (int64_t)TIMING_MIN_STEP_US;
    }
    if (elapsed_us > (int64_t)TIMING_MAX_STEP_US) {
        elapsed_us = (int64_t)TIMING_MAX_STEP_US;
    }

    t->dt_ms = (uint32_t)(elapsed_us / 1000);
    t->now_ms = (uint32_t)(now_us / 1000);
}
