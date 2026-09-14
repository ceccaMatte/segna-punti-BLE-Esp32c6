/**
 * @file commissioning_pin.c
 * @brief Il piedino del commissioning. Vedi commissioning_pin.h per il perche'.
 *
 * SPDX-License-Identifier: MIT
 */

#include "commissioning_pin.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#include "commissioning_manager.h"

static const char *TAG = "commissioning";

/** Ultimo livello visto: serve a parlare solo quando cambia. */
static bool s_low_seen;

void commissioning_pin_init(void)
{
    /*
     * Come il pulsante di gioco: ingresso con la resistenza di salita accesa,
     * cosi' quando nessuno lo tocca legge alto e non si prende disturbi.
     */
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << CONFIG_PADEL_COMMISSIONING_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));

    /*
     * Come si legge il piedino appena configurato.
     *
     * Con la resistenza di salita accesa un piedino libero si legge alto: se
     * all'avvio si legge gia' basso, vuol dire che qualcosa lo tiene a massa.
     * E' la prima cosa da guardare quando il commissioning non si apre.
     */
    s_low_seen = (gpio_get_level((gpio_num_t)CONFIG_PADEL_COMMISSIONING_GPIO) == 0);

    ESP_LOGI(TAG, "GPIO%d letto %s all'avvio",
             CONFIG_PADEL_COMMISSIONING_GPIO,
             s_low_seen ? "BASSO (qualcosa lo tiene a massa)" : "alto");
}

void commissioning_pin_update(uint32_t dt_ms)
{
    const bool low = (gpio_get_level((gpio_num_t)CONFIG_PADEL_COMMISSIONING_GPIO) == 0);

    /* Una riga quando cambia, e solo allora: e' il modo piu' diretto per capire
       se il filo fa contatto, senza dedurlo dal fatto che il gesto sia scattato
       o meno. */
    if (low != s_low_seen) {
        ESP_LOGI(TAG, "GPIO%d %s", CONFIG_PADEL_COMMISSIONING_GPIO,
                 low ? "verso massa" : "rilasciato");
        s_low_seen = low;
    }

    commissioning_manager_update(low, dt_ms);
}
