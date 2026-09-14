/**
 * @file indicators.c
 * @brief Il LED di bordo. Vedi indicators.h per il perche'.
 *
 * SPDX-License-Identifier: MIT
 */

#include "indicators.h"

#include "esp_err.h"
#include "esp_log.h"

#include "board.h"
#include "controller.h"
#include "led_anim.h"
#include "rgb_led.h"

static const char *TAG = "led";

void indicators_init(void)
{
    /*
     * Il LED si prepara dopo lo schermo, a chip gia' avviato.
     *
     * Il suo piedino e' uno di quelli che il chip legge all'accensione per
     * decidere come partire, quindi non va toccato prima. Se non parte si
     * prosegue come per lo schermo: il segnapunti funziona anche senza LED.
     */
    const esp_err_t error = rgb_led_init(BOARD_RGB_GPIO);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "LED non disponibile (%s): si prosegue senza",
                 esp_err_to_name(error));
    }

    led_anim_init();
}

void indicators_update(uint32_t now_ms)
{
    switch (controller_take_action()) {
    case CTRL_ACTION_POINT_NOI:
        led_anim_point(TEAM_US, now_ms);
        break;
    case CTRL_ACTION_POINT_LORO:
        led_anim_point(TEAM_THEM, now_ms);
        break;
    case CTRL_ACTION_UNDO:
        led_anim_undo();
        break;
    case CTRL_ACTION_RESET:
        led_anim_reset();
        break;
    case CTRL_ACTION_NONE:
    default:
        break;
    }

    if (!rgb_led_ready()) {
        return;
    }

    /* Ci si aspetta che il colore sia cambiato: durante lo spettacolo cambia a
       ogni giro, quando e' fermo non si manda niente al LED. */
    led_rgb_t colour;
    if (led_anim_update(now_ms, &colour)) {
        rgb_led_set(&colour);
    }
}
