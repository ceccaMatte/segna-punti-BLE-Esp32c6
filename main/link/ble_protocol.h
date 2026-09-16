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
/* Eventi                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * Che cosa ha provocato la pubblicazione di uno snapshot.
 *
 * Viaggia nel primo byte libero del pacchetto della partita, in coda ai campi
 * storici, e non tocca nessuno di quelli: chi sa leggere un pacchetto di sedici
 * byte trova gli stessi numeri nello stesso posto, e in piu' il motivo per cui
 * e' arrivato.
 *
 * La regola, e vale la pena di scriverla perche' e' tutta qui la semantica del
 * campo: ``event`` dice *perche'* e' partito il pacchetto, tutto il resto dice
 * com'e' la partita *dopo* quell'evento. Chi riceve non applica niente: lo
 * stato che legge e' gia' quello giusto.
 *
 * I nomi sono in inglese perche' sono nomi di protocollo, non parole
 * dell'interfaccia: gli stessi valori sono definiti anche in
 * ``web/src/ble/protocol.ts``, e i test dei due lati usano gli stessi esempi.
 */
typedef enum {
    /**
     * Nessun gesto.
     *
     * Accompagna il battito e i cambiamenti che non vengono da un gesto (per
     * esempio l'azzeramento automatico dopo la schermata del vincitore): chi
     * riceve non deve mostrare nessun evento, solo lo stato.
     */
    PADEL_EVT_NONE = 0,

    PADEL_EVT_OUR_POINT   = 1, /**< un click: punto a NOI                  */
    PADEL_EVT_THEIR_POINT = 2, /**< due click: punto a LORO                */
    PADEL_EVT_UNDO        = 3, /**< tre click: annullata l'ultima azione   */

    /**
     * Pressione lunga, rilasciata oltre la soglia breve.
     *
     * Non e' un gesto di gioco e non tocca il punteggio: serve a chi, fuori
     * dalla scheda, vorra' mettere un segno nel tempo (un marker sul video).
     * Lo stato che accompagna il pacchetto e' quello in cui il marker e'
     * stato chiesto.
     */
    PADEL_EVT_MOMENT = 4,

    /**
     * La pressione ha raggiunto la soglia del pairing.
     *
     * Esce, se la connessione e' viva e riconosciuta, subito *prima* che la
     * scheda cancelli l'associazione e apra la finestra di commissioning: la
     * pagina collegata sa cosi' perche' sta per perdere le notifiche.
     */
    PADEL_EVT_START_PAIRING = 5,

    PADEL_EVT_RESET = 6, /**< quattro click: partita azzerata              */

    /**
     * Non e' un gesto: e' la sincronizzazione.
     *
     * Accompagna i pacchetti che servono a mettersi in pari — appena una
     * pagina si e' fatta riconoscere, o quando legge lo stato — e dice a chi
     * riceve che quello e' lo stato corrente, non la notizia di qualcosa.
     */
    PADEL_EVT_STATE_SYNC = 7
} padel_event_t;

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
 * Diciassette byte, non uno di piu'.
 *
 * La misura non e' casuale: la notifica BLE piu' piccola che esista porta venti
 * byte di dati (ATT MTU 23 meno i tre di intestazione). Standoci dentro, lo
 * stato della partita arriva anche senza negoziare l'MTU, che con Web Bluetooth
 * non e' garantito chiedere.
 *
 * I sedici byte originali sono rimasti dove erano; il diciassettesimo e' in
 * coda e dice che cosa ha provocato la pubblicazione (vedi ::padel_event_t).
 * Aggiungere in coda invece di infilarsi in mezzo e' quello che permette a un
 * lettore di una versione precedente di continuare a leggere i campi che
 * conosce, ed e' la ragione per cui la versione del protocollo non cambia.
 */
#define PADEL_SCORE_PACKET_SIZE  17
#define PADEL_STATUS_PACKET_SIZE 6
#define PADEL_DEVICE_INFO_SIZE   8
#define PADEL_CONTROL_PACKET_SIZE (1 + PADEL_TOKEN_LEN)

/* -------------------------------------------------------------------------- */
/* Bit di stato del pacchetto punteggio                                       */
/* -------------------------------------------------------------------------- */

#define PADEL_FLAG_TIE_BREAK   0x01u /**< il set corrente e' deciso al tie-break */
#define PADEL_FLAG_FINISHED    0x02u /**< partita conclusa                      */
#define PADEL_FLAG_SERVING_NOI 0x04u /**< serve NOI (altrimenti LORO)           */

/**
 * Il pacchetto non porta niente di nuovo: e' un battito.
 *
 * La scheda manda lo stesso stato di prima a intervalli regolari, anche quando
 * il punteggio non cambia, e il numero di sequenza resta quello.
 *
 * Serve alla pagina web, e a una cosa sola: distinguere "non sta succedendo
 * niente" da "la scheda non c'e' piu'". Senza il battito l'unico modo di
 * accorgersi di un riavvio sarebbe aspettare che il sistema dichiari caduto il
 * collegamento, e da solo ci mette piu' di dieci secondi — il tempo di
 * supervisione del Bluetooth. Col battito la pagina lo capisce in tre secondi,
 * chiude il collegamento morto e riprende.
 *
 * Chi lo riceve sa che e' identico a quello di prima: non e' un aggiornamento,
 * e non va contato come un pacchetto arrivato due volte.
 */
#define PADEL_FLAG_HEARTBEAT   0x08u

/** Segna che il campo "vincitore" non significa niente. */
#define PADEL_WINNER_NONE 0xFFu

/* -------------------------------------------------------------------------- */
/* Pacchetti                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Snapshot completo e autorevole della partita.
 *
 * E' sempre lo stato intero: una pagina che si collega a partita iniziata lo
 * legge e sa subito tutto, e se una notifica va persa la successiva rimette
 * comunque le cose a posto.
 *
 * Insieme allo stato viaggia il motivo per cui e' partito (::padel_event_t):
 * il campo ``event`` racconta il gesto, tutto il resto racconta la partita
 * dopo quel gesto. Chi riceve non applica niente: disegna quello che legge.
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
    uint8_t  event;       /**< padel_event_t: che cosa ha provocato l'invio     */
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
