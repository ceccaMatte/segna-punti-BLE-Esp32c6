/**
 * @file ble_gatt.c
 * @brief Servizio GATT, annuncio e connessioni, con NimBLE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_gatt.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble";

/* -------------------------------------------------------------------------- */
/* Quanto deve essere reattivo il collegamento                                */
/* -------------------------------------------------------------------------- */

/**
 * Quanto il collegamento puo' restare in silenzio prima di essere dichiarato
 * morto.
 *
 * E' il numero che decide quanto ci si mette ad accorgersi che la scheda e'
 * sparita: se la scheda si riavvia il collegamento muore senza avvisare, e il
 * computer se ne accorge solo quando questo tempo e' scaduto. Quello che
 * propone lui e' lungo — misurato, una decina di secondi — e per una scheda che
 * si riavvia e' tempo perso: qui gliene si chiede uno corto.
 *
 * Due secondi con un intervallo di trenta millisecondi vogliono dire
 * trentatre' pacchetti persi di fila prima di dichiarare morto un collegamento
 * che sta benissimo: nessun disturbo passeggero arriva a tanto.
 */
#define LINK_SUPERVISION_TIMEOUT_MS 2000u

/** Ogni quanto due apparecchi si danno il buongiorno quando non c'e' niente da dire. */
#define LINK_INTERVAL_MS 30u

/**
 * Chiede al computer di tenere il collegamento pronto a morire presto.
 *
 * La pagina web fa la sua parte — conta i battiti della scheda e chiude da sola
 * il collegamento quando smettono di arrivare — ma c'e' un pezzo che dipende
 * solo da questa parte: la velocita' con cui il sistema libera la scheda
 * vecchia. Finche' non l'ha liberata, ogni tentativo di ricollegarsi fallisce,
 * e non c'e' niente che la pagina possa fare.
 *
 * Chiedere parametri piu' stretti si puo' fare anche da qui: lo standard prevede
 * che il periferico proponga i suoi, e il computer decide. Se dice di no non si
 * perde niente: resta il tempo lungo di prima.
 */
static void request_fast_link(uint16_t conn_handle)
{
    const struct ble_gap_upd_params params = {
        .itvl_min = BLE_GAP_CONN_ITVL_MS(LINK_INTERVAL_MS),
        .itvl_max = BLE_GAP_CONN_ITVL_MS(LINK_INTERVAL_MS),
        .latency = 0u,
        .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(LINK_SUPERVISION_TIMEOUT_MS),
        .min_ce_len = 0u,
        .max_ce_len = 0u,
    };

    const int rc = ble_gap_update_params(conn_handle, &params);

    if (rc == 0) {
        ESP_LOGI(TAG, "chiesto un collegamento reattivo (morte dichiarata dopo %u ms)",
                 (unsigned)LINK_SUPERVISION_TIMEOUT_MS);
    } else {
        ESP_LOGW(TAG, "parametri del collegamento non richiesti (%d)", rc);
    }
}

/* -------------------------------------------------------------------------- */
/* Gli identificativi, nella forma che vuole NimBLE                           */
/* -------------------------------------------------------------------------- */

/*
 * NimBLE tiene gli UUID a 128 bit al contrario di come si scrivono: il primo
 * byte del vettore e' l'ultimo della forma leggibile. I vettori qui sotto sono
 * quindi le stringhe di ble_protocol.h lette da destra a sinistra, e il
 * commento dice quale stringa e', perche' a occhio non si riconosce.
 *
 * La forma leggibile e' l'unica sorgente di verita': se cambia li', va cambiata
 * anche qui, e il gemello in web/src/ble/protocol.ts.
 */

/* 6b8d0001-9c4f-4e21-b7a3-0d5e1f2a3b40 */
static const ble_uuid128_t UUID_SERVICE =
    BLE_UUID128_INIT(0x40, 0x3b, 0x2a, 0x1f, 0x5e, 0x0d, 0xa3, 0xb7,
                     0x21, 0x4e, 0x4f, 0x9c, 0x01, 0x00, 0x8d, 0x6b);

/* 6b8d0002-9c4f-4e21-b7a3-0d5e1f2a3b40 */
static const ble_uuid128_t UUID_DEVICE_INFO =
    BLE_UUID128_INIT(0x40, 0x3b, 0x2a, 0x1f, 0x5e, 0x0d, 0xa3, 0xb7,
                     0x21, 0x4e, 0x4f, 0x9c, 0x02, 0x00, 0x8d, 0x6b);

/* 6b8d0003-9c4f-4e21-b7a3-0d5e1f2a3b40 */
static const ble_uuid128_t UUID_SCORE_STATE =
    BLE_UUID128_INIT(0x40, 0x3b, 0x2a, 0x1f, 0x5e, 0x0d, 0xa3, 0xb7,
                     0x21, 0x4e, 0x4f, 0x9c, 0x03, 0x00, 0x8d, 0x6b);

/* 6b8d0004-9c4f-4e21-b7a3-0d5e1f2a3b40 */
static const ble_uuid128_t UUID_COMMISSION_CONTROL =
    BLE_UUID128_INIT(0x40, 0x3b, 0x2a, 0x1f, 0x5e, 0x0d, 0xa3, 0xb7,
                     0x21, 0x4e, 0x4f, 0x9c, 0x04, 0x00, 0x8d, 0x6b);

/* 6b8d0005-9c4f-4e21-b7a3-0d5e1f2a3b40 */
static const ble_uuid128_t UUID_COMMISSION_STATUS =
    BLE_UUID128_INIT(0x40, 0x3b, 0x2a, 0x1f, 0x5e, 0x0d, 0xa3, 0xb7,
                     0x21, 0x4e, 0x4f, 0x9c, 0x05, 0x00, 0x8d, 0x6b);

/* -------------------------------------------------------------------------- */
/* Stato interno                                                              */
/* -------------------------------------------------------------------------- */

static ble_gatt_callbacks_t s_callbacks;

static char     s_device_name[32];
static uint8_t  s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool     s_connected;
static bool     s_score_subscribed;
static bool     s_status_subscribed;
static bool     s_score_readable;
static bool     s_advertising;

static uint16_t s_handle_device_info;
static uint16_t s_handle_score;
static uint16_t s_handle_control;
static uint16_t s_handle_status;

/** Gli ultimi pacchetti pronti da leggere. Vedi ble_gatt_set_*(). */
static uint8_t s_device_info[PADEL_DEVICE_INFO_SIZE];
static uint8_t s_status[PADEL_STATUS_PACKET_SIZE];
static uint8_t s_score[PADEL_SCORE_PACKET_SIZE];
static size_t  s_score_size;

static int gap_event(struct ble_gap_event *event, void *arg);

/* -------------------------------------------------------------------------- */
/* Accesso alle characteristic                                                */
/* -------------------------------------------------------------------------- */

/** Copia un pacchetto nella risposta a una lettura. */
static int answer_read(struct ble_gatt_access_ctxt *ctxt, const uint8_t *data, size_t size)
{
    if (os_mbuf_append(ctxt->om, data, size) != 0) {
        /* Il buffer di risposta e' pieno: succede solo se il client chiede piu'
           di quanto la connessione possa portare in un pacchetto. */
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static int handle_read(uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt)
{
    if (attr_handle == s_handle_device_info) {
        return answer_read(ctxt, s_device_info, sizeof(s_device_info));
    }

    if (attr_handle == s_handle_status) {
        return answer_read(ctxt, s_status, sizeof(s_status));
    }

    if (attr_handle == s_handle_score) {
        /* Lo stato della partita non e' di tutti: se la scheda e' associata a
           qualcuno, chi non si e' fatto riconoscere riceve un rifiuto. */
        if (!s_score_readable) {
            ESP_LOGW(TAG, "lettura dello stato rifiutata: connessione non autenticata");
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        return answer_read(ctxt, s_score, s_score_size);
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int handle_write(uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt)
{
    if (attr_handle != s_handle_control) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t payload[PADEL_CONTROL_PACKET_SIZE];
    const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);

    if (length > sizeof(payload)) {
        ESP_LOGW(TAG, "comando di %u byte: troppo lungo", (unsigned)length);
        if (s_callbacks.on_control != NULL) {
            s_callbacks.on_control(NULL, s_callbacks.context);
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, payload, sizeof(payload), &copied) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    padel_control_packet_t command;
    const bool valid = padel_control_decode(payload, copied, &command);

    if (!valid) {
        ESP_LOGW(TAG, "comando non valido: %u byte", (unsigned)copied);
    }

    /*
     * Si consegna e si torna subito: chi riceve prende nota e il lavoro vero
     * (memoria permanente, notifiche) lo fa il ciclo principale. Qui dentro
     * gira il compito di NimBLE, che non deve fermarsi a scrivere sulla flash.
     *
     * Un comando non valido viene consegnato lo stesso, con NULL: serve a chi
     * riceve per dire alla pagina web che ha sbagliato qualcosa, invece di
     * lasciarla aspettare una risposta che non arrivera' mai.
     */
    if (s_callbacks.on_control != NULL) {
        s_callbacks.on_control(valid ? &command : NULL, s_callbacks.context);
    }

    return 0;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return handle_read(attr_handle, ctxt);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return handle_write(attr_handle, ctxt);
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* -------------------------------------------------------------------------- */
/* Il servizio                                                                */
/* -------------------------------------------------------------------------- */

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Chi e' la scheda e in che stato si trova. Si legge sempre:
                   senza queste informazioni la pagina web non saprebbe nemmeno
                   se deve associarsi o riconoscersi. */
                .uuid = &UUID_DEVICE_INFO.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_handle_device_info,
            },
            {
                /* Lo stato della partita: si legge, e si puo' ricevere senza
                   doverlo chiedere ogni volta. */
                .uuid = &UUID_SCORE_STATE.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_handle_score,
            },
            {
                /* I comandi, uno solo verso la scheda. */
                .uuid = &UUID_COMMISSION_CONTROL.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_handle_control,
            },
            {
                /* Come sta andando l'associazione. Si legge sempre, e si puo'
                   ricevere: e' cosi' che la pagina web scopre l'esito del
                   comando che ha appena mandato. */
                .uuid = &UUID_COMMISSION_STATUS.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_handle_status,
            },
            {
                0, /* fine delle characteristic */
            }
        },
    },
    {
        0, /* fine dei servizi */
    },
};

/* -------------------------------------------------------------------------- */
/* Annuncio                                                                   */
/* -------------------------------------------------------------------------- */

void ble_gatt_advertise(void)
{
    if (s_connected || s_advertising) {
        return;
    }

    struct ble_hs_adv_fields fields;

    /*
     * L'annuncio e la risposta allo scan si dividono il lavoro, e non e' una
     * scelta estetica: un pacchetto di annuncio porta trentuno byte, e il nome
     * (diciassette con l'intestazione) piu' l'UUID a 128 bit (diciotto) non ci
     * starebbero insieme. Nell'annuncio va l'UUID, perche' e' quello che il
     * browser usa per trovare la scheda; il nome va nella risposta allo scan,
     * che il browser legge comunque.
     */
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &UUID_SERVICE;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "annuncio non impostato: %d", rc);
        return;
    }

    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)s_device_name;
    fields.name_len = (uint8_t)strlen(s_device_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "risposta allo scan non impostata: %d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "annuncio non avviato: %d", rc);
        return;
    }

    s_advertising = true;
    ESP_LOGI(TAG, "in annuncio come %s", s_device_name);
}

/* -------------------------------------------------------------------------- */
/* Eventi della connessione                                                   */
/* -------------------------------------------------------------------------- */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            /* Il tentativo non e' andato a buon fine: si riprova. */
            s_advertising = false;
            ble_gatt_advertise();
            return 0;
        }

        s_conn_handle = event->connect.conn_handle;
        s_connected = true;
        s_advertising = false;
        s_score_subscribed = false;
        s_status_subscribed = false;

        ESP_LOGI(TAG, "connesso");

        /* Si chiede subito un collegamento pronto a morire presto: e' quello
           che permette al computer di accorgersi in due secondi di un riavvio,
           invece di dieci. Vedi request_fast_link(). */
        request_fast_link(event->connect.conn_handle);

        if (s_callbacks.on_connected != NULL) {
            s_callbacks.on_connected(s_callbacks.context);
        }
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* L'esito della richiesta fatta appena collegati. */
        if (event->conn_update.status == 0) {
            ESP_LOGI(TAG, "collegamento aggiornato come richiesto");
        } else {
            ESP_LOGW(TAG, "il computer non ha accettato i parametri proposti (%d)",
                     event->conn_update.status);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnesso (motivo %d)", event->disconnect.reason);

        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected = false;
        s_score_subscribed = false;
        s_status_subscribed = false;

        if (s_callbacks.on_disconnected != NULL) {
            s_callbacks.on_disconnected(s_callbacks.context);
        }

        /* Si torna subito a farsi trovare: la pagina web deve poter tornare
           quando vuole, anche a partita in corso. */
        ble_gatt_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_handle_score) {
            s_score_subscribed = event->subscribe.cur_notify;
        } else if (event->subscribe.attr_handle == s_handle_status) {
            s_status_subscribed = event->subscribe.cur_notify;
        }
        ESP_LOGI(TAG, "iscrizioni: partita %d, associazione %d",
                 (int)s_score_subscribed, (int)s_status_subscribed);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = false;
        ble_gatt_advertise();
        return 0;

    default:
        return 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Avvio                                                                      */
/* -------------------------------------------------------------------------- */

static void host_task(void *param)
{
    (void)param;

    /* Non torna: e' il compito che fa girare il protocollo. */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGE(TAG, "nessun indirizzo Bluetooth disponibile");
        return;
    }

    /* L'indirizzo si prende da solo: la scheda ne ha uno di fabbrica. */
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(TAG, "indirizzo Bluetooth non determinabile");
        return;
    }

    ble_gatt_advertise();
}

static void on_reset(int reason)
{
    /* Succede solo se il controller si riavvia da solo: si annota e si aspetta
       che torni, perche' da qui non si puo' fare altro. */
    ESP_LOGW(TAG, "controller Bluetooth riavviato (motivo %d)", reason);
}

esp_err_t ble_gatt_init(const char *device_name, const ble_gatt_callbacks_t *callbacks)
{
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_callbacks, 0, sizeof(s_callbacks));
    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    }

    strlcpy(s_device_name, device_name, sizeof(s_device_name));

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE non partito: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "nome non impostato: %d", rc);
    }

    rc = ble_gatts_count_cfg(SERVICES);
    if (rc != 0) {
        ESP_LOGE(TAG, "servizio non contato: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(SERVICES);
    if (rc != 0) {
        ESP_LOGE(TAG, "servizio non aggiunto: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "servizio pronto, nome %s", s_device_name);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Dati e notifiche                                                           */
/* -------------------------------------------------------------------------- */

void ble_gatt_set_device_info(const padel_device_info_packet_t *info)
{
    if (info == NULL) {
        return;
    }
    (void)padel_device_info_encode(info, s_device_info, sizeof(s_device_info));
}

void ble_gatt_set_status(const padel_status_packet_t *status)
{
    if (status == NULL) {
        return;
    }
    (void)padel_status_encode(status, s_status, sizeof(s_status));
}

void ble_gatt_set_score(const uint8_t *packet, size_t size)
{
    if (packet == NULL || size > sizeof(s_score)) {
        return;
    }

    memcpy(s_score, packet, size);
    s_score_size = size;
}

void ble_gatt_set_score_readable(bool readable)
{
    s_score_readable = readable;
}

/** Spedisce un pacchetto come notifica, se c'e' qualcuno che lo aspetta. */
static bool notify(uint16_t handle, bool subscribed, const uint8_t *data, size_t size)
{
    if (!s_connected || !subscribed || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }

    /*
     * La copia del pacchetto diventa di proprieta' dello stack, che la libera
     * da solo appena ha finito di spedirla: non va toccata dopo.
     */
    struct os_mbuf *buffer = ble_hs_mbuf_from_flat(data, size);
    if (buffer == NULL) {
        ESP_LOGW(TAG, "memoria esaurita per una notifica");
        return false;
    }

    const int rc = ble_gatts_notify_custom(s_conn_handle, handle, buffer);
    if (rc != 0) {
        ESP_LOGW(TAG, "notifica non spedita: %d", rc);
        return false;
    }

    return true;
}

bool ble_gatt_notify_score(void)
{
    if (s_score_size == 0u) {
        return false;
    }
    return notify(s_handle_score, s_score_subscribed, s_score, s_score_size);
}

bool ble_gatt_notify_status(void)
{
    return notify(s_handle_status, s_status_subscribed, s_status, sizeof(s_status));
}

bool ble_gatt_connected(void)
{
    return s_connected;
}
