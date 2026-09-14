/**
 * @file gpio_scan.h
 * @brief Sorveglianza dei piedini esposti: serve a capire se un piedino arriva.
 *
 * E' uno strumento diagnostico, non fa parte del segnapunti. Ogni secondo scrive
 * sul monitor seriale come si legge ciascun piedino, e una riga a parte ogni
 * volta che uno cambia.
 *
 * Serve a rispondere a una domanda sola: "questo filo tocca davvero il piedino
 * che credo?". Si mette a massa un piedino e si guarda se il log se ne accorge.
 * Se il log non se ne accorge non e' il firmware che non legge: e' il filo che
 * non arriva, o il piedino che non e' quello.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Ogni quanto si scrive la fotografia completa dei piedini. */
#ifndef GPIO_SCAN_PERIOD_MS
#define GPIO_SCAN_PERIOD_MS 1000u
#endif

/** Prepara i piedini come ingressi con la resistenza di salita accesa. */
void gpio_scan_init(void);

/**
 * @brief Guarda i piedini e scrive quello che vede.
 * @param now_ms istante corrente, in millisecondi.
 */
void gpio_scan_tick(uint32_t now_ms);
