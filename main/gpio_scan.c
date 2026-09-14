/**
 * @file gpio_scan.c
 * @brief Sorveglianza diagnostica dei piedini esposti.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gpio_scan.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio-scan";

/*
 * I piedini che questa scheda porta fuori, in ordine di numero.
 *
 * Si mettono tutti come ingressi con la resistenza di salita accesa: cosi'
 * nessuno di loro prende disturbi, e un piedino lasciato libero si legge alto.
 * Da li' in poi basta guardarli.
 *
 * Restano fuori:
 *
 *   6, 7, 14, 15, 21, 22   il display (SPI e comandi)
 *   8                      il LED di bordo
 *   12, 13                 le due linee del USB, che sono anche la porta da cui
 *                          si legge questo log: metterle come ingressi
 *                          spegnerebbe il collegamento con il computer, e con
 *                          esso il monitor. Si lasciano in pace.
 */
static const int SCAN_PINS[] = { 0, 1, 2, 3, 4, 5, 9, 16, 17, 18, 19, 20, 23 };

#define SCAN_COUNT ((int)(sizeof(SCAN_PINS) / sizeof(SCAN_PINS[0])))

/** Ultimo livello visto, per accorgersi dei cambiamenti. */
static bool s_level[SCAN_COUNT];

/** Quando e' stata scritta l'ultima fotografia completa. */
static uint32_t s_last_report;

/** Legge il livello di un piedino sorvegliato. */
static bool read_pin(int index)
{
    return gpio_get_level((gpio_num_t)SCAN_PINS[index]) != 0;
}

/** Scrive la fotografia completa: numero del piedino e livello. */
static void report_all(void)
{
    char line[160];
    int used = 0;
    int remaining = (int)sizeof(line);

    for (int i = 0; i < SCAN_COUNT; ++i) {
        const int written = snprintf(&line[used], (size_t)remaining, "%s%d:%d",
                                     (i == 0) ? "" : " ", SCAN_PINS[i],
                                     s_level[i] ? 1 : 0);

        if (written <= 0 || written >= remaining) {
            /* Riga piena: meglio tagliarla che scrivere fuori dal buffer. */
            break;
        }
        used += written;
        remaining -= written;
    }

    ESP_LOGI(TAG, "%s", line);
}

void gpio_scan_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < SCAN_COUNT; ++i) {
        mask |= 1ULL << SCAN_PINS[i];
    }

    const gpio_config_t io_config = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));

    for (int i = 0; i < SCAN_COUNT; ++i) {
        s_level[i] = read_pin(i);
    }

    ESP_LOGW(TAG, "sorveglianza su %d piedini, una riga al secondo: %d, %d, %d, %d, %d, %d,"
                  " %d, %d, %d, %d, %d, %d, %d",
             SCAN_COUNT,
             SCAN_PINS[0], SCAN_PINS[1], SCAN_PINS[2], SCAN_PINS[3], SCAN_PINS[4],
             SCAN_PINS[5], SCAN_PINS[6], SCAN_PINS[7], SCAN_PINS[8], SCAN_PINS[9],
             SCAN_PINS[10], SCAN_PINS[11], SCAN_PINS[12]);
    ESP_LOGW(TAG, "non sorvegliati: 6, 7, 14, 15, 21, 22 (display), 8 (LED),"
                  " 12 e 13 (USB: riconfigurarli spegnerebbe questo monitor)");

    report_all();
}

void gpio_scan_tick(uint32_t now_ms)
{
    for (int i = 0; i < SCAN_COUNT; ++i) {
        const bool level = read_pin(i);

        if (level != s_level[i]) {
            s_level[i] = level;
            ESP_LOGW(TAG, "GPIO%d %s", SCAN_PINS[i],
                     level ? "e' tornato alto" : "e' andato verso massa");
        }
    }

    if ((uint32_t)(now_ms - s_last_report) >= GPIO_SCAN_PERIOD_MS) {
        s_last_report = now_ms;
        report_all();
    }
}
