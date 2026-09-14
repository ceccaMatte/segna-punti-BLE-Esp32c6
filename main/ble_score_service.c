/**
 * @file ble_score_service.c
 * @brief Pubblicazione dello stato della partita.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_score_service.h"

#include "ble_gatt.h"
#include "commissioning_manager.h"
#include "esp_log.h"
#include "score_state_adapter.h"

static const char *TAG = "score";

/** Ultimo stato pubblicato, e se e' mai stato pubblicato. */
static padel_score_packet_t s_current;
static bool                 s_have_current;

/** Numero dello snapshot. Cresce di uno a ogni pubblicazione. */
static uint16_t s_sequence;

/** Ultimo riconoscimento visto, per accorgersi di chi entra. */
static uint32_t s_auth_marker;

static uint8_t s_encoded[PADEL_SCORE_PACKET_SIZE];

void ble_score_service_init(void)
{
    s_have_current = false;
    s_sequence = 0u;
    s_auth_marker = commissioning_manager_auth_marker();
}

void ble_score_service_update(const MatchState *match)
{
    /*
     * Il pacchetto si costruisce sempre, anche a nessuno collegato: e' il modo
     * piu' semplice per sapere se qualcosa e' cambiato, ed e' anche quello che
     * tiene aggiornato lo stato che si risponde a chi legge.
     */
    padel_score_packet_t next;
    score_adapter_build(match, (uint16_t)(s_sequence + 1u), &next);

    const bool changed = !s_have_current || !score_adapter_same(&s_current, &next);

    /* Chi si e' appena fatto riconoscere deve ricevere subito com'e' la
       partita, senza aspettare che cambi: la pagina web puo' collegarsi a
       partita gia' avviata, ed e' anzi il caso piu' comune. */
    const bool newcomer = commissioning_manager_auth_marker() != s_auth_marker;

    if (!changed && !newcomer) {
        return;
    }

    s_auth_marker = commissioning_manager_auth_marker();
    s_sequence = next.sequence;
    s_current = next;
    s_have_current = true;

    (void)padel_score_encode(&s_current, s_encoded, sizeof(s_encoded));
    ble_gatt_set_score(s_encoded, sizeof(s_encoded));

    /* A nessuno che non si sia fatto riconoscere si racconta la partita. */
    if (commissioning_manager_authenticated()) {
        if (ble_gatt_notify_score()) {
            ESP_LOGD(TAG, "pubblicato lo snapshot %u", (unsigned)s_sequence);
        }
    }
}

uint16_t ble_score_service_sequence(void)
{
    return s_sequence;
}
