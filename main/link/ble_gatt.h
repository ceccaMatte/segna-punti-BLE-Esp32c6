/**
 * @file ble_gatt.h
 * @brief Il servizio GATT della scheda: la parte che parla con la radio.
 *
 * Questo modulo sa di Bluetooth e non sa niente di padel: riceve pacchetti gia'
 * pronti e li mette dove la pagina web li va a leggere, oppure li spedisce come
 * notifiche. Chi decide *cosa* mandare sono gli altri moduli; qui c'e' solo il
 * come.
 *
 * Le cose che sa fare:
 *
 *   - preparare il servizio e cominciare ad annunciarsi;
 *   - tenere il conto della connessione e di chi si e' iscritto alle notifiche;
 *   - rispondere alle letture con l'ultimo pacchetto ricevuto dall'alto;
 *   - consegnare a chi di dovere i comandi scritti dalla pagina web.
 *
 * Il controllo di chi ha diritto a cosa non sta qui dentro ma in chi lo usa:
 * questo modulo si limita a chiedere se lo stato della partita si puo' leggere,
 * e a non spedire notifiche se nessuno le ha chieste.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ble_protocol.h"
#include "esp_err.h"

/**
 * Chi vuole sapere cosa succede sulla radio.
 *
 * Le funzioni vengono chiamate dal compito di NimBLE, non dal ciclo principale:
 * devono essere rapide e non devono toccare la memoria permanente ne' aspettare
 * niente. Chi le riceve si limita a prendere nota e a fare il lavoro dopo.
 */
typedef struct {
    void (*on_connected)(void *context);
    void (*on_disconnected)(void *context);
    /**
     * Un comando scritto dalla pagina web.
     * @param command il comando, oppure NULL se i byte non sono validi.
     */
    void (*on_control)(const padel_control_packet_t *command, void *context);
    void *context;
} ble_gatt_callbacks_t;

/**
 * @brief Prepara NimBLE, il servizio e l'annuncio.
 *
 * @param device_name nome con cui la scheda si presenta (PADEL_SCORE_XXXX).
 * @param callbacks   chi avvisare; puo' essere NULL.
 */
esp_err_t ble_gatt_init(const char *device_name, const ble_gatt_callbacks_t *callbacks);

/** Aggiorna il pacchetto che si risponde a chi legge DEVICE_INFO. */
void ble_gatt_set_device_info(const padel_device_info_packet_t *info);

/** Aggiorna il pacchetto che si risponde a chi legge COMMISSIONING_STATUS. */
void ble_gatt_set_status(const padel_status_packet_t *status);

/** Aggiorna il pacchetto che si risponde a chi legge SCORE_STATE. */
void ble_gatt_set_score(const uint8_t *packet, size_t size);

/**
 * @brief Decide se lo stato della partita si puo' leggere.
 *
 * Una scheda associata non racconta la partita a chi non si e' fatto
 * riconoscere: la lettura risponde con un rifiuto, come se il permesso non ci
 * fosse. Lo stato dell'associazione, invece, resta leggibile da tutti, perche'
 * senza quello la pagina web non saprebbe nemmeno cosa deve fare.
 */
void ble_gatt_set_score_readable(bool readable);

/**
 * Manda uno stato della partita come notifica.
 *
 * I byte da spedire sono quelli che si vogliono spedire *adesso*, e possono
 * non essere la copia che si risponde a chi legge: la notifica racconta il
 * gesto appena successo, la lettura racconta com'e' la partita, e le due cose
 * divergono proprio quando qualcuno legge subito dopo un gesto.
 *
 * Falso se non c'e' nessuno in ascolto.
 */
bool ble_gatt_notify_score(const uint8_t *packet, size_t size);

/** Manda lo stato dell'associazione come notifica. */
bool ble_gatt_notify_status(void);

/** Vero se c'e' una connessione aperta. */
bool ble_gatt_connected(void);

/** Riprende ad annunciarsi, se non c'e' gia' nessuno collegato. */
void ble_gatt_advertise(void);
