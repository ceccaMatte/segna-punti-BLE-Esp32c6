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

#include "ble_score_service.h"
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
static const char *gesture_name(btn_event_t event)
{
    switch (event) {
    case BTN_EVT_SINGLE:    return "1 click -> NOI";
    case BTN_EVT_DOUBLE:    return "2 click -> LORO";
    case BTN_EVT_TRIPLE:    return "3 click -> annulla";
    case BTN_EVT_QUADRUPLE: return "4 click -> azzera";
    case BTN_EVT_MOMENT:    return "pressione lunga -> MOMENT";
    case BTN_EVT_PAIRING:   return "pressione prolungata -> commissioning";
    default:                return "nessuno";
    }
}
#endif

/**
 * Il nome che il gesto prende nel protocollo.
 *
 * La macchina del pulsante non conosce il protocollo e il protocollo non
 * conosce i gesti: la traduzione sta qui, in un posto solo, e non va scritta
 * ne' di la' ne' di qua.
 */
static padel_event_t protocol_event(btn_event_t event)
{
    switch (event) {
    case BTN_EVT_SINGLE:    return PADEL_EVT_OUR_POINT;
    case BTN_EVT_DOUBLE:    return PADEL_EVT_THEIR_POINT;
    case BTN_EVT_TRIPLE:    return PADEL_EVT_UNDO;
    case BTN_EVT_QUADRUPLE: return PADEL_EVT_RESET;
    default:                return PADEL_EVT_NONE;
    }
}

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

uint32_t gestures_hold_ms(void)
{
    /* hold_ms sopravvive al rilascio come ultimo valore misurato: e' la
       pressione *in corso* che conta, e a pulsante su non c'e' nessuna
       pressione in corso. */
    return s_button.level ? s_button.hold_ms : 0u;
}

uint32_t gestures_pairing_hold_ms(void)
{
    return BTN_PAIRING_HOLD_MS;
}

void gestures_update(uint32_t dt_ms)
{
    const bool pressed = (gpio_get_level(BOARD_BUTTON_GPIO) == BUTTON_PRESSED_LEVEL);
    const btn_event_t event = button_update(&s_button, pressed, dt_ms);

    if (event == BTN_EVT_NONE) {
        return;
    }

    switch (event) {
    case BTN_EVT_PAIRING:
        /*
         * La pressione piu' lunga non e' un gesto di gioco: apre la finestra
         * di commissioning, come farebbe il piedino tenuto verso massa.
         *
         * Prima si annuncia l'evento alla pagina collegata — se c'e' e si e'
         * fatta riconoscere — e solo dopo si cancella l'associazione: in questo
         * istante il vecchio committente e' ancora valido, ed e' l'ultima
         * occasione per dirgli perche' sta per perdere le notifiche.
         */
        ble_score_service_publish_event(controller_state(), PADEL_EVT_START_PAIRING);
        commissioning_manager_request();
        break;

    case BTN_EVT_MOMENT:
        /* Un segno nel tempo, non un punto: il punteggio resta com'e', quindi
           non passa dal controller. Lo stato che accompagna l'evento dice in
           che punto della partita il segno e' stato chiesto. */
        ble_score_service_publish_event(controller_state(), PADEL_EVT_MOMENT);
        break;

    default:
        /*
         * I gesti di gioco passano dal controller, e lo stato si pubblica solo
         * se ha davvero applicato qualcosa: un click a partita finita non deve
         * arrivare alla pagina web come un evento che non c'e' stato.
         */
        if (controller_handle_event(event)) {
            ble_score_service_publish_event(controller_state(), protocol_event(event));
        }

#if CONFIG_PADEL_DEBUG_INVARIANTS
        if (!match_invariants_ok(controller_state())) {
            ESP_LOGE(TAG, "stato della partita incoerente dopo: %s", gesture_name(event));
        }
#endif
        break;
    }

#if CONFIG_PADEL_LOG_EVENTS
    ESP_LOGI(TAG, "%s", gesture_name(event));
#endif
}
