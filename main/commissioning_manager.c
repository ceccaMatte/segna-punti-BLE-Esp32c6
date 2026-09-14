/**
 * @file commissioning_manager.c
 * @brief La regia del commissioning.
 *
 * SPDX-License-Identifier: MIT
 */

#include "commissioning_manager.h"

#include <string.h>

#include "ble_gatt.h"
#include "device_identity.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "hold_gesture.h"
#include "nvs_store.h"

static const char *TAG = "commissioning";

/* -------------------------------------------------------------------------- */
/* Quello che arriva dalla radio, messo da parte                              */
/* -------------------------------------------------------------------------- */

/**
 * Gli avvisi che il compito di NimBLE lascia al ciclo principale.
 *
 * E' una casella sola, non una coda: i comandi arrivano da un essere umano che
 * preme un pulsante in una pagina web, quindi al massimo se ne perde uno se due
 * arrivano nello stesso giro da cinque millisecondi. Il comando resta valido
 * finche' non viene ritirato, e chi lo scrive e' uno solo.
 */
typedef enum {
    PENDING_NONE = 0,
    PENDING_CONNECTED,
    PENDING_DISCONNECTED,
    PENDING_COMMAND
} pending_event_t;

static volatile pending_event_t s_pending = PENDING_NONE;
static padel_control_packet_t   s_command;
static bool                     s_command_valid;

/* -------------------------------------------------------------------------- */
/* Stato                                                                      */
/* -------------------------------------------------------------------------- */

static commissioning_state_t s_state;
static hold_gesture_t        s_gesture;

static char     s_device_name[32];
static char     s_short_id[IDENTITY_SHORT_ID_LEN + 1];
static uint16_t s_short_id_value;
static uint32_t s_auth_marker;

/* -------------------------------------------------------------------------- */
/* Avvisi dalla radio                                                         */
/* -------------------------------------------------------------------------- */

static void on_connected(void *context)
{
    (void)context;
    s_pending = PENDING_CONNECTED;
}

static void on_disconnected(void *context)
{
    (void)context;
    s_pending = PENDING_DISCONNECTED;
}

static void on_control(const padel_control_packet_t *command, void *context)
{
    (void)context;

    s_command_valid = (command != NULL);
    if (command != NULL) {
        s_command = *command;
    }
    s_pending = PENDING_COMMAND;
}

/* -------------------------------------------------------------------------- */
/* Quello che la radio deve poter leggere                                     */
/* -------------------------------------------------------------------------- */

static void refresh_outputs(void)
{
    const padel_device_info_packet_t info = {
        .state = commissioning_state_protocol_state(&s_state),
        .authenticated = s_state.authenticated ? 1u : 0u,
        .firmware = PADEL_FIRMWARE_VERSION,
        .short_id = s_short_id_value,
    };
    ble_gatt_set_device_info(&info);

    const padel_status_packet_t status = {
        .state = commissioning_state_protocol_state(&s_state),
        .authenticated = s_state.authenticated ? 1u : 0u,
        .result = s_state.result,
        .remaining_s = (uint8_t)commissioning_state_seconds_left(&s_state),
    };
    ble_gatt_set_status(&status);

    /* Da qui in avanti si decide anche chi puo' leggere il punteggio. */
    ble_gatt_set_score_readable(s_state.authenticated);
}

/* -------------------------------------------------------------------------- */
/* Aprire la finestra                                                         */
/* -------------------------------------------------------------------------- */

/**
 * Cancella l'associazione e apre la finestra.
 *
 * La cancellazione riguarda solo il commissioning — nella memoria di questa
 * scheda non c'e' nient'altro che il token — ed e' voluta: chi apre la finestra
 * sta revocando l'associazione che c'era.
 */
static void open_window(void)
{
    commissioning_state_open(&s_state);
    (void)nvs_store_erase_token();
}

/* -------------------------------------------------------------------------- */
/* Comandi                                                                    */
/* -------------------------------------------------------------------------- */

static void apply_command(void)
{
    if (!s_command_valid) {
        ESP_LOGW(TAG, "comando illeggibile: si risponde con un errore di protocollo");
        commissioning_state_protocol_error(&s_state);
        return;
    }

    if (s_command.opcode == (uint8_t)PADEL_OP_CLAIM) {
        const uint8_t result = commissioning_state_claim(&s_state, s_command.token);

        if (result == PADEL_RESULT_CLAIM_SUCCESS) {
            if (nvs_store_save_token(s_command.token) != ESP_OK) {
                ESP_LOGE(TAG, "associazione non salvata: vale solo fino al riavvio");
            }
            ESP_LOGI(TAG, "associazione creata");
            s_auth_marker++;
        } else {
            ESP_LOGW(TAG, "associazione rifiutata: la finestra non e' aperta");
        }
        return;
    }

    const uint8_t result = commissioning_state_auth(&s_state, s_command.token);

    if (result == PADEL_RESULT_AUTH_SUCCESS) {
        ESP_LOGI(TAG, "connessione riconosciuta");
        s_auth_marker++;
    } else {
        ESP_LOGW(TAG, "riconoscimento fallito: il token non e' quello salvato");
    }
}

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

void commissioning_manager_init(void)
{
    /*
     * L'identita' viene prima di tutto il resto: serve al nome con cui la
     * scheda si annuncia. Si legge dall'indirizzo di rete del Bluetooth, che e'
     * di fabbrica e diverso per ogni esemplare.
     */
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
        ESP_LOGW(TAG, "indirizzo della scheda non leggibile: nome provvisorio");
        memset(mac, 0, sizeof(mac));
    }

    identity_device_name(mac, s_device_name, sizeof(s_device_name));
    identity_short_id(mac, s_short_id);
    s_short_id_value = identity_short_id_value(mac);

    commissioning_state_init(&s_state, COMMISSIONING_WINDOW_MS, COMMISSIONING_NOTICE_MS);
    hold_gesture_init(&s_gesture);

    /* La memoria va preparata prima della radio: NimBLE la usa per conto suo. */
    if (nvs_store_init() == ESP_OK) {
        uint8_t token[PADEL_TOKEN_LEN];
        const bool present = nvs_store_load_token(token);
        commissioning_state_restore(&s_state, token, present);

        if (present) {
            ESP_LOGI(TAG, "associazione trovata in memoria");
        }
    } else {
        ESP_LOGE(TAG, "memoria non disponibile: l'associazione non sopravvive al riavvio");
    }

    const ble_gatt_callbacks_t callbacks = {
        .on_connected = on_connected,
        .on_disconnected = on_disconnected,
        .on_control = on_control,
        .context = NULL,
    };

    const esp_err_t err = ble_gatt_init(s_device_name, &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth non disponibile (%s): si prosegue senza",
                 esp_err_to_name(err));
    }

    refresh_outputs();
}

void commissioning_manager_update(bool gpio_low, uint32_t dt_ms)
{
    bool something_changed = false;

    /* Il piedino tenuto basso: si cancella l'associazione precedente e si apre
       la finestra, come farebbe il pulsante di gioco tenuto premuto a lungo. */
    if (hold_gesture_update(&s_gesture, gpio_low, dt_ms, COMMISSIONING_HOLD_MS)) {
        ESP_LOGW(TAG, "commissioning richiesto: associazione cancellata, finestra aperta");
        open_window();
        something_changed = true;
    }

    /* Quello che la radio ha lasciato in sospeso. */
    const pending_event_t pending = s_pending;
    s_pending = PENDING_NONE;

    switch (pending) {
    case PENDING_CONNECTED:
        ESP_LOGI(TAG, "pagina web collegata");
        commissioning_state_connected(&s_state);
        something_changed = true;
        break;

    case PENDING_DISCONNECTED:
        commissioning_state_disconnected(&s_state);
        something_changed = true;
        break;

    case PENDING_COMMAND:
        apply_command();
        something_changed = true;
        break;

    case PENDING_NONE:
    default:
        break;
    }

    /* E infine il tempo che passa: la finestra, l'avviso a video. */
    if (commissioning_state_tick(&s_state, dt_ms)) {
        something_changed = true;
    }

    if (something_changed) {
        refresh_outputs();
        (void)ble_gatt_notify_status();
    }
}

void commissioning_manager_request(void)
{
    ESP_LOGW(TAG, "commissioning richiesto: associazione cancellata, finestra aperta");
    open_window();

    /* Fuori dal giro di commissioning_manager_update non c'e' nessuno che
       rinfreschi radio e schermo: si fa qui, che e' l'unica cosa da fare. */
    refresh_outputs();
    (void)ble_gatt_notify_status();
}

const commissioning_state_t *commissioning_manager_state(void)
{
    return &s_state;
}

bool commissioning_manager_authenticated(void)
{
    return s_state.authenticated;
}

uint32_t commissioning_manager_auth_marker(void)
{
    return s_auth_marker;
}

bool commissioning_manager_screen_active(void)
{
    return commissioning_state_screen_active(&s_state);
}

const char *commissioning_manager_device_name(void)
{
    return s_device_name;
}

const char *commissioning_manager_short_id(void)
{
    return s_short_id;
}
