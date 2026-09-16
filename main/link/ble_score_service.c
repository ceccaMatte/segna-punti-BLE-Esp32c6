/**
 * @file ble_score_service.c
 * @brief Pubblicazione dello stato della partita.
 *
 * Le pubblicazioni sono di due tipi, e si riconoscono dalla funzione che le
 * manda:
 *
 *   - ble_score_service_update(): il giro normale. Guarda se lo stato e'
 *     cambiato e, se non lo e', si limita a un battito ogni tanto. E' il
 *     battito che fa accorgere la pagina di un riavvio in pochi secondi.
 *   - ble_score_service_publish_event(): il gesto. Parte subito, anche se lo
 *     stato non e' cambiato, e porta il nome di quello che e' successo.
 *
 * Le due copie che escono da deliver() hanno lo stesso stato e due eventi
 * diversi: la notifica porta il gesto, la risposta a chi *legge* porta
 * STATE_SYNC. Chi legge non ha perso nessuna notizia — sta chiedendo com'e' la
 * partita — e l'ultimo gesto di mezz'ora prima non lo riguarda.
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

/** I byte della notifica e quelli che si risponde a chi legge. Vedi deliver(). */
static uint8_t s_notify_bytes[PADEL_SCORE_PACKET_SIZE];
static uint8_t s_read_bytes[PADEL_SCORE_PACKET_SIZE];

void ble_score_service_init(void)
{
    s_have_current = false;
    s_sequence = 0u;
    s_auth_marker = commissioning_manager_auth_marker();
    s_since_publish_ms = 0u;
}

/**
 * Consegna un pacchetto alla radio, nelle sue due copie.
 *
 * La copia da notificare porta l'evento: dice a chi era collegato *perche'* e'
 * arrivato il pacchetto. La copia che si risponde a chi legge la
 * characteristic porta STATE_SYNC: chi legge sta chiedendo com'e' la partita,
 * e non deve vedere un gesto vecchio come se fosse appena successo.
 */
static void deliver(const padel_score_packet_t *state, padel_event_t event)
{
    padel_score_packet_t outgoing = *state;
    outgoing.event = (uint8_t)event;

    padel_score_packet_t readable = *state;
    readable.event = (uint8_t)PADEL_EVT_STATE_SYNC;

    (void)padel_score_encode(&outgoing, s_notify_bytes, sizeof(s_notify_bytes));
    (void)padel_score_encode(&readable, s_read_bytes, sizeof(s_read_bytes));

    ble_gatt_set_score(s_read_bytes, sizeof(s_read_bytes));

    /* A nessuno che non si sia fatto riconoscere si racconta la partita. */
    if (!commissioning_manager_authenticated()) {
        return;
    }

    if (ble_gatt_notify_score(s_notify_bytes, sizeof(s_notify_bytes))) {
        ESP_LOGD(TAG, "pubblicato lo snapshot %u, evento %u",
                 (unsigned)outgoing.sequence, (unsigned)event);
    }
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
    score_adapter_build(match, (uint16_t)(s_sequence + 1u), PADEL_EVT_NONE, &next);

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

    /* Lo stato salvato e' quello vero, senza il bit del battito e senza
       l'evento: altrimenti il confronto successivo vedrebbe un cambiamento
       che non c'e'. */
    s_current = next;
    s_have_current = true;

    if (heartbeat) {
        /* Il battito non porta un numero nuovo: chi lo riceve sa che non e'
           cambiato niente, e non lo conta come un secondo pacchetto. */
        next.sequence = s_sequence;
        next.flags |= PADEL_FLAG_HEARTBEAT;
        deliver(&next, PADEL_EVT_NONE);
        return;
    }

    s_sequence = next.sequence;

    /* Chi si e' appena fatto riconoscere riceve lo stato marcato come
       sincronizzazione; un cambiamento che non viene da un gesto — per esempio
       l'azzeramento automatico dopo la schermata del vincitore — viaggia senza
       evento. */
    deliver(&next, newcomer ? PADEL_EVT_STATE_SYNC : PADEL_EVT_NONE);
}

void ble_score_service_publish_event(const MatchState *match, padel_event_t event)
{
    padel_score_packet_t packet;
    score_adapter_build(match, (uint16_t)(s_sequence + 1u), event, &packet);

    /* La pubblicazione forzata e' una notizia: parte adesso, con un numero
       nuovo, e sposta anche il battito — non serve ripetere fra un secondo lo
       stato che si e' appena mandato. */
    s_sequence = packet.sequence;
    s_since_publish_ms = 0u;
    s_have_current = true;

    /* Lo stato salvato non porta l'evento: serve al confronto, e il confronto
       guarda la partita. */
    s_current = packet;
    s_current.event = (uint8_t)PADEL_EVT_NONE;

    deliver(&packet, event);
}

uint16_t ble_score_service_sequence(void)
{
    return s_sequence;
}
