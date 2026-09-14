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

/**
 * Ogni quanto la scheda si fa sentire, anche se il punteggio non cambia.
 *
 * E' un battito, e ha un destinatario preciso: la pagina web. Senza, una pagina
 * collegata non ha modo di sapere se il silenzio vuol dire "partita ferma" o
 * "scheda sparita": per accorgersene dovrebbe aspettare che il sistema dichiari
 * caduto il collegamento, e dopo un riavvio ci mette piu' di dieci secondi.
 *
 * Il valore e' legato a SILENCE_TIMEOUT_MS in `web/src/ble/liveness.ts`: la
 * pagina aspetta due battiti mancati prima di concludere che la scheda non c'e'
 * piu'. Cambiando uno dei due va cambiato anche l'altro.
 */
#define HEARTBEAT_MS 1200u

/** Ultimo stato pubblicato, e se e' mai stato pubblicato. */
static padel_score_packet_t s_current;
static bool                 s_have_current;

/** Numero dello snapshot. Cresce di uno a ogni pubblicazione.
 *
 * Il battito non lo fa crescere: e' lo stesso stato di prima, e chi lo riceve
 * non deve credere che sia successo qualcosa.
 */
static uint16_t s_sequence;

/** Ultimo riconoscimento visto, per accorgersi di chi entra. */
static uint32_t s_auth_marker;

/** Millisecondi dall'ultima pubblicazione, battiti compresi. */
static uint32_t s_since_publish_ms;

static uint8_t s_encoded[PADEL_SCORE_PACKET_SIZE];

void ble_score_service_init(void)
{
    s_have_current = false;
    s_sequence = 0u;
    s_auth_marker = commissioning_manager_auth_marker();
    s_since_publish_ms = 0u;
}

void ble_score_service_update(const MatchState *match, uint32_t dt_ms)
{
    s_since_publish_ms += dt_ms;

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

    /* Niente di nuovo e niente da annunciare: si manda lo stesso stato di
       prima, marcato come battito. */
    const bool heartbeat = !changed && !newcomer && s_since_publish_ms >= HEARTBEAT_MS;

    if (!changed && !newcomer && !heartbeat) {
        return;
    }

    s_since_publish_ms = 0u;
    s_auth_marker = commissioning_manager_auth_marker();

    padel_score_packet_t outgoing = next;

    if (heartbeat) {
        /* Il battito non porta un numero nuovo: chi lo riceve sa che non e'
           cambiato niente, e non lo conta come un secondo pacchetto. */
        outgoing.flags |= PADEL_FLAG_HEARTBEAT;
        outgoing.sequence = s_current.sequence;
    } else {
        s_sequence = next.sequence;
    }

    /* Lo stato salvato e' sempre quello vero, senza il bit del battito:
       altrimenti il confronto successivo vedrebbe un cambiamento che non c'e'. */
    s_current = next;
    s_have_current = true;

    (void)padel_score_encode(&outgoing, s_encoded, sizeof(s_encoded));
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
