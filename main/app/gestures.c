/**
 * @file gestures.c
 * @brief Il pulsante di gioco. Vedi gestures.h per il perché.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gestures.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#include "board.h"
#include "button.h"
#include "commissioning_manager.h"
#include "controller.h"
#include "match.h"

/** Il pulsante porta il piedino a massa quando e' premuto. */
#define BUTTON_PRESSED_LEVEL 0

/** La macchina a stati del pulsante: una sola, per l'unico pulsante che c'e'. */
static button_t s_button;

#if CONFIG_PADEL_LOG_EVENTS || CONFIG_PADEL_DEBUG_INVARIANTS
/* Il prefisso dei log serve solo dove si stampa qualcosa: definirlo sempre
   lascerebbe un avviso di compilazione in ogni configurazione che tace. */
static const char *TAG = "segnapunti";

/** Nome del gesto riconosciuto, per i log. */
static const char *event_name(btn_event_t event)
{
    switch (event) {
    case BTN_EVT_SINGLE:    return "1 click -> NOI";
    case BTN_EVT_DOUBLE:    return "2 click -> LORO";
    case BTN_EVT_TRIPLE:    return "3 click -> annulla";
    case BTN_EVT_LONG:      return "pressione lunga -> azzera";
    case BTN_EVT_VERY_LONG: return "pressione prolungata -> commissioning";
    default:                return "nessuno";
    }
}
#endif

/**
 * Prepara il piedino del pulsante come ingresso.
 *
 * Niente interrupt: il pulsante viene letto dal ciclo principale, che e' anche
 * l'unico posto dove si puo' stampare sul monitor seriale senza rischi.
 */
static void button_gpio_init(void)
{
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));
}

void gestures_init(void)
{
    button_gpio_init();
    button_init(&s_button);
}

void gestures_update(uint32_t dt_ms)
{
    const bool pressed = (gpio_get_level(BOARD_BUTTON_GPIO) == BUTTON_PRESSED_LEVEL);
    const btn_event_t event = button_update(&s_button, pressed, dt_ms);

    if (event == BTN_EVT_NONE) {
        return;
    }

    /*
     * La pressione piu' lunga non e' un gesto di gioco: non passa dal
     * controller, apre la finestra di commissioning come farebbe il piedino
     * tenuto verso massa. Il controller non sa nemmeno che esista.
     */
    if (event == BTN_EVT_VERY_LONG) {
        commissioning_manager_request();
    } else {
        controller_handle_event(event);

#if CONFIG_PADEL_DEBUG_INVARIANTS
        if (!match_invariants_ok(controller_state())) {
            ESP_LOGE(TAG, "stato della partita incoerente dopo: %s", event_name(event));
        }
#endif
    }

#if CONFIG_PADEL_LOG_EVENTS
    ESP_LOGI(TAG, "%s", event_name(event));
#endif
}
