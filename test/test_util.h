/**
 * @file test_util.h
 * @brief Microroutine di supporto per i test host della logica pura.
 *
 * Nessuna dipendenza esterna: solo stdio. I test girano su PC e non toccano
 * l'hardware, cosi' i bug del padel non si mescolano a quelli del display.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>

/** Contatori globali, definiti in test_util.c. */
extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        g_checks++;                                                     \
        if (!(cond)) {                                                  \
            g_failures++;                                               \
            printf("    FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        const long a_ = (long)(actual);                                        \
        const long e_ = (long)(expected);                                      \
        g_checks++;                                                            \
        if (a_ != e_) {                                                        \
            g_failures++;                                                      \
            printf("    FAIL  %s:%d  %s == %s  (atteso %ld, ottenuto %ld)\n",  \
                   __FILE__, __LINE__, #actual, #expected, e_, a_);            \
        }                                                                      \
    } while (0)

#define FAIL(msg)                                                              \
    do {                                                                       \
        g_checks++;                                                            \
        g_failures++;                                                          \
        printf("    FAIL  %s:%d  %s\n", __FILE__, __LINE__, (msg));            \
    } while (0)

/** Segnala l'inizio di un caso di test. */
void test_begin(const char *name);

/** Chiude il caso di test e riporta quanti controlli sono falliti. */
void test_end(const char *name);

/** Stampa il riepilogo. Ritorna 0 se tutto e' passato, 1 altrimenti. */
int test_summary(void);

/* Suite, implementate nei rispettivi file. */
void test_timing_all(void);
void test_match_all(void);
void test_button_all(void);
void test_controller_all(void);
void test_font_all(void);
void test_gfx_all(void);
void test_dirty_all(void);
void test_ui_view_all(void);
void test_layout_all(void);
void test_led_anim_all(void);
void test_ble_protocol_all(void);
void test_device_identity_all(void);
void test_hold_gesture_all(void);
void test_score_state_adapter_all(void);
void test_commissioning_state_all(void);
