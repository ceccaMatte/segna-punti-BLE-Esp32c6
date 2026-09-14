/**
 * @file test_util.c
 * @brief Supporto ai test host: contatori e riepilogo.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_util.h"

int g_checks = 0;
int g_failures = 0;

/** Fallimenti registrati all'inizio del caso in corso. */
static int s_case_start_failures;

void test_begin(const char *name)
{
    s_case_start_failures = g_failures;
    printf("  - %s\n", name);
}

void test_end(const char *name)
{
    (void)name;

    if (g_failures == s_case_start_failures) {
        printf("      ok\n");
    } else {
        printf("      ^^ %d controlli falliti\n", g_failures - s_case_start_failures);
    }
}

int test_summary(void)
{
    printf("\n==================================================\n");

    if (g_failures == 0) {
        printf("  TUTTI I TEST PASSATI  (%d controlli)\n", g_checks);
    } else {
        printf("  %d CONTROLLI FALLITI su %d\n", g_failures, g_checks);
    }

    printf("==================================================\n");

    return (g_failures == 0) ? 0 : 1;
}
