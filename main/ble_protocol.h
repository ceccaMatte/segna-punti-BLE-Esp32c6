/**
 * @file ble_protocol.h
 * @brief Il protocollo fra la scheda e la pagina web: UUID, opcode, pacchetti.
 *
 * Tutto quello che riguarda il formato dei dati sta qui e da nessun'altra parte:
 * gli UUID, la versione, gli opcode e la disposizione dei byte. Chi spedisce e
 * chi riceve usano le funzioni di codifica e decodifica e non hanno bisogno di
 * conoscere i numeri, che altrimenti finirebbero sparsi in due linguaggi diversi
 * e prima o poi divergerebbero.
 *
 * Il gemello di questo file e' ``web/src/ble/protocol.ts``: le due copie devono
 * restare d'accordo, e il modo per accorgersene e' che gli stessi casi sono
 * provati da entrambe le parti (``test/test_ble_protocol.c`` e
 * ``web/test/protocol.test.ts``).
 *
 * Modulo di pura logica: nessuna dipendenza da ESP-IDF, si compila e si prova
 * sul PC.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Identificativi                                                             */
/* -------------------------------------------------------------------------- */

/*
 * Una famiglia di UUID sola: cambiano solo le ultime quattro cifre del terzo
 * gruppo, cosi' si riconosce a colpo d'occhio che appartengono allo stesso
 * servizio. Sono valori fissi scelti per questo progetto: non esistono altrove
 * e non devono cambiare, perche' sono scritti anche nella pagina web e nel
 * filtro che il browser usa per cercare la scheda.
 */
#define PADEL_UUID_SERVICE            "6b8d0001-9c4f-4e21-b7a3-0d5e1f2a3b40"
#define PADEL_UUID_DEVICE_INFO        "6b8d0002-9c4f-4e21-b7a3-0d5e1f2a3b40"
#define PADEL_UUID_SCORE_STATE        "6b8d0003-9c4f-4e21-b7a3-0d5e1f2a3b40"
#define PADEL_UUID_COMMISSION_CONTROL "6b8d0004-9c4f-4e21-b7a3-0d5e1f2a3b40"
#define PADEL_UUID_COMMISSION_STATUS  "6b8d0005-9c4f-4e21-b7a3-0d5e1f2a3b40"

/** Prefisso del nome con cui la scheda si annuncia. */
#define PADEL_NAME_PREFIX "PADEL_SCORE_"

/**
 * Versione del protocollo.
 *
 * Va alzata quando cambia la disposizione dei byte. Chi riceve un pacchetto con
 * una versione diversa lo scarta invece di interpretarlo male: un pacchetto
 * capito a meta' e' peggio di un pacchetto perso.
 */
#define PADEL_PROTOCOL_VERSION 1

/** Versione del firmware, due byte: byte alto = maggiore, byte basso = minore. */
#define PADEL_FIRMWARE_VERSION 0x0100u

/** Lunghezza del token di associazione. */
#define PADEL_TOKEN_LEN 16

/* -------------------------------------------------------------------------- */
/* Tipi di messaggio                                                          */
/* -------------------------------------------------------------------------- */

typedef enum {
    PADEL_MSG_SCORE_STATE          = 1, /**< snapshot completo della partita    */
    PADEL_MSG_COMMISSIONING_STATUS = 2, /**< stato dell'associazione            */
    PADEL_MSG_DEVICE_INFO          = 3  /**< chi e' la scheda e come sta        */
} padel_message_type_t;

/* -------------------------------------------------------------------------- */
/* Committente e autenticazione                                               */
/* -------------------------------------------------------------------------- */

/** Stato dell'associazione, come lo vede la pagina web. */
typedef enum {
    PADEL_COMM_UNCOMMISSIONED = 0, /**< nessuna associazione salvata           */
    PADEL_COMM_WINDOW_OPEN    = 1, /**< in attesa di un nuovo committente      */
    PADEL_COMM_COMMISSIONED   = 2  /**< associazione presente                   */
} padel_commissioning_state_t;

/**
 * Esito dell'ultima operazione.
 *
 * E' un valore di stato, non un evento: resta quello che era finche' non
 * succede qualcosa d'altro. La pagina web puo' cosi' leggerlo in qualsiasi
 * momento e capire come e' andata, anche se ha perso la notifica del momento.
 */
typedef enum {
    PADEL_RESULT_IDLE           = 0, /**< niente da segnalare                 */
    PADEL_RESULT_CLAIM_SUCCESS  = 1, /**< associazione appena creata          */
    PADEL_RESULT_AUTH_SUCCESS   = 2, /**< token riconosciuto                  */
    PADEL_RESULT_AUTH_FAILED    = 3, /**< token diverso da quello salvato     */
    PADEL_RESULT_CLAIM_REJECTED = 4, /**< CLAIM fuori dalla finestra          */
    PADEL_RESULT_TIMEOUT        = 5, /**< finestra scaduta senza committente  */
    PADEL_RESULT_PROTOCOL_ERROR = 6  /**< pacchetto ricevuto non valido       */
} padel_result_t;

/** Comandi che la pagina web puo' scrivere sulla characteristic di controllo. */
typedef enum {
    PADEL_OP_CLAIM = 0x01, /**< associa questa installazione alla scheda */
    PADEL_OP_AUTH  = 0x02  /**< riconosciti sulle connessioni successive */
} padel_control_op_t;

/* -------------------------------------------------------------------------- */
/* Dimensioni dei pacchetti                                                   */
/* -------------------------------------------------------------------------- */

/**
 * Sedici byte, non uno di piu'.
 *
 * La misura non e' casuale: la notifica BLE piu' piccola che esista porta venti
 * byte di dati (ATT MTU 23 meno i tre di intestazione). Standoci dentro, lo
 * stato della partita arriva anche senza negoziare l'MTU, che con Web Bluetooth
 * non e' garantito chiedere.
 */
#define PADEL_SCORE_PACKET_SIZE  16
#define PADEL_STATUS_PACKET_SIZE 6
#define PADEL_DEVICE_INFO_SIZE   8
#define PADEL_CONTROL_PACKET_SIZE (1 + PADEL_TOKEN_LEN)

/* -------------------------------------------------------------------------- */
/* Bit di stato del pacchetto punteggio                                       */
/* -------------------------------------------------------------------------- */

#define PADEL_FLAG_TIE_BREAK   0x01u /**< il set corrente e' deciso al tie-break */
#define PADEL_FLAG_FINISHED    0x02u /**< partita conclusa                      */
#define PADEL_FLAG_SERVING_NOI 0x04u /**< serve NOI (altrimenti LORO)           */

/** Segna che il campo "vincitore" non significa niente. */
#define PADEL_WINNER_NONE 0xFFu

/* -------------------------------------------------------------------------- */
/* Pacchetti                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Snapshot completo e autorevole della partita.
 *
 * E' sempre lo stato intero, mai un evento: una pagina che si collega a partita
 * iniziata lo legge e sa subito tutto, e se una notifica va persa la successiva
 * rimette comunque le cose a posto.
 *
 * Gli indici delle squadre sono quelli del motore: 0 = LORO (sinistra),
 * 1 = NOI (destra).
 */
typedef struct {
    uint8_t  flags;       /**< combinazione di PADEL_FLAG_*                    */
    uint8_t  winner;      /**< indice squadra oppure PADEL_WINNER_NONE        */
    uint16_t sequence;    /**< numero di snapshot, cresce a ogni pubblicazione */
    uint8_t  points[2];   /**< valore di point_t: 0, 15, 30, 40, vantaggio     */
    uint8_t  games[2];    /**< game vinti nel set corrente                     */
    uint8_t  sets[2];     /**< set vinti                                       */
    uint16_t tb_points[2];/**< punti del tie-break, contati uno per uno        */
} padel_score_packet_t;

/** Stato dell'associazione e della connessione in corso. */
typedef struct {
    uint8_t state;         /**< padel_commissioning_state_t                  */
    uint8_t authenticated; /**< 1 se questa connessione ha superato AUTH     */
    uint8_t result;        /**< padel_result_t                               */
    uint8_t remaining_s;   /**< secondi che restano alla finestra, 0 se chiusa */
} padel_status_packet_t;

/** Chi e' la scheda. */
typedef struct {
    uint8_t  state;         /**< padel_commissioning_state_t                 */
    uint8_t  authenticated; /**< 1 se questa connessione ha superato AUTH    */
    uint16_t firmware;      /**< PADEL_FIRMWARE_VERSION                      */
    uint16_t short_id;      /**< due byte dell'indirizzo BLE, per riconoscerla */
} padel_device_info_packet_t;

/** Comando ricevuto dalla pagina web. */
typedef struct {
    uint8_t opcode;                /**< padel_control_op_t                   */
    uint8_t token[PADEL_TOKEN_LEN];/**< i sedici byte dell'associazione      */
} padel_control_packet_t;

/* -------------------------------------------------------------------------- */
/* Codifica e decodifica                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Scrive un pacchetto di stato della partita.
 * @return i byte scritti, oppure 0 se il buffer e' troppo piccolo.
 */
size_t padel_score_encode(const padel_score_packet_t *packet, uint8_t *out, size_t out_size);

/**
 * @brief Legge un pacchetto di stato della partita.
 * @return false se la versione non e' quella attesa, se il tipo non e' quello
 *         giusto o se i byte non bastano.
 */
bool padel_score_decode(const uint8_t *in, size_t in_size, padel_score_packet_t *out);

size_t padel_status_encode(const padel_status_packet_t *packet, uint8_t *out, size_t out_size);
bool padel_status_decode(const uint8_t *in, size_t in_size, padel_status_packet_t *out);

size_t padel_device_info_encode(const padel_device_info_packet_t *packet, uint8_t *out, size_t out_size);
bool padel_device_info_decode(const uint8_t *in, size_t in_size, padel_device_info_packet_t *out);

size_t padel_control_encode(const padel_control_packet_t *packet, uint8_t *out, size_t out_size);

/**
 * @brief Legge un comando ricevuto.
 * @return false se la lunghezza non e' quella giusta o se l'opcode e' sconosciuto.
 */
bool padel_control_decode(const uint8_t *in, size_t in_size, padel_control_packet_t *out);
