/**
 * @file commissioning_state.h
 * @brief La macchina a stati dell'associazione, senza hardware intorno.
 *
 * Tiene il conto di tre cose che non hanno niente a che vedere con il padel:
 * se la scheda e' associata a una pagina web, se in questo momento e' aperta la
 * finestra per associarne una nuova, e se la connessione in corso si e' fatta
 * riconoscere.
 *
 * Sta da sola, senza NimBLE e senza NVS, per una ragione precisa: e' la parte
 * che si puo' sbagliare in modo invisibile (una finestra che non si chiude, un
 * token che resta valido dopo essere stato cancellato, un'autenticazione che
 * sopravvive alla disconnessione) ed e' quindi quella che vale la pena provare
 * sul computer, dove si puo' far passare un minuto in un microsecondo.
 *
 * Chi la usa le porta il tempo e i fatti: il piedino tenuto basso, un comando
 * arrivato dalla radio, una connessione aperta o chiusa. Lei li trasforma in
 * stato e in esiti, e dice se c'e' qualcosa di nuovo da mostrare sullo schermo.
 *
 * La persistenza non e' affar suo: memorizzare il token su NVS lo fa chi la
 * usa, quando le dice che l'associazione e' cambiata.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ble_protocol.h"

/** Cosa deve mostrare lo schermo in questo momento. */
typedef enum {
    COMMISSIONING_PHASE_IDLE = 0,  /**< niente: resta la schermata di gioco     */
    COMMISSIONING_PHASE_WAITING,   /**< finestra aperta, nessuno collegato      */
    COMMISSIONING_PHASE_CONNECTED, /**< finestra aperta, pagina web collegata   */
    COMMISSIONING_PHASE_DONE,      /**< associazione riuscita, a video per poco */
    COMMISSIONING_PHASE_EXPIRED    /**< finestra scaduta, a video per poco      */
} commissioning_phase_t;

typedef struct {
    uint8_t  token[PADEL_TOKEN_LEN]; /**< associazione salvata                */
    bool     commissioned;           /**< esiste un'associazione              */
    bool     window_open;            /**< si accettano nuove associazioni     */
    bool     connected;              /**< c'e' una connessione aperta         */
    bool     authenticated;          /**< quella connessione si e' riconosciuta */
    uint8_t  result;                 /**< padel_result_t: l'ultimo esito      */
    commissioning_phase_t phase;     /**< cosa mostrare                       */

    uint32_t window_left_ms;         /**< tempo che resta alla finestra       */
    uint32_t notice_left_ms;         /**< tempo che resta all'avviso a video  */
    uint32_t window_ms;              /**< durata della finestra               */
    uint32_t notice_ms;              /**< durata dell'avviso                  */

    /**
     * Cambia ogni volta che qualcosa che si vede e' cambiato.
     *
     * Serve a chi disegna per non riscrivere lo schermo a ogni giro del ciclo
     * principale: si confronta il numero e si ridisegna solo quando e' diverso.
     * Il conto alla rovescia lo fa cambiare una volta al secondo, che e' quanto
     * basta e non di piu'.
     */
    uint32_t revision;
} commissioning_state_t;

/** Prepara lo stato: non associata, finestra chiusa. */
void commissioning_state_init(commissioning_state_t *state, uint32_t window_ms, uint32_t notice_ms);

/**
 * @brief Riferisce l'associazione trovata in memoria all'avvio.
 *
 * Non cambia la fase: all'accensione si mostra la schermata di gioco anche se
 * la scheda e' gia' associata.
 */
void commissioning_state_restore(commissioning_state_t *st, const uint8_t *token, bool present);

/**
 * @brief Apre la finestra per una nuova associazione.
 *
 * Cancella l'associazione precedente, se c'era: chi apre la finestra vuole
 * associare una pagina web nuova, e tenere in giro il token vecchio vorrebbe
 * dire lasciare una chiave che non serve piu'. La connessione in corso smette
 * di essere autenticata, perche' l'associazione su cui si basava non esiste piu'.
 */
void commissioning_state_open(commissioning_state_t *st);

/**
 * @brief Fa passare il tempo.
 * @return true se qualcosa che si vede e' cambiato.
 */
bool commissioning_state_tick(commissioning_state_t *st, uint32_t dt_ms);

/**
 * @brief Registra un comando di associazione arrivato dalla radio.
 * @return l'esito: PADEL_RESULT_CLAIM_SUCCESS oppure PADEL_RESULT_CLAIM_REJECTED.
 */
uint8_t commissioning_state_claim(commissioning_state_t *st, const uint8_t *token);

/**
 * @brief Registra un tentativo di riconoscersi.
 * @return PADEL_RESULT_AUTH_SUCCESS oppure PADEL_RESULT_AUTH_FAILED.
 */
uint8_t commissioning_state_auth(commissioning_state_t *st, const uint8_t *token);

/** Nuova connessione aperta. */
void commissioning_state_connected(commissioning_state_t *st);

/**
 * @brief Registra un comando arrivato male.
 *
 * Non cambia lo stato dell'associazione: serve solo a dire alla pagina web che
 * ha mandato qualcosa che non si capisce, invece di lasciarla in attesa.
 */
void commissioning_state_protocol_error(commissioning_state_t *st);

/**
 * @brief Connessione chiusa.
 *
 * L'autenticazione vale per la connessione, non per la scheda: chiudendola si
 * torna a dover dimostrare di essere chi si dice, anche se la scheda resta
 * associata.
 */
void commissioning_state_disconnected(commissioning_state_t *st);

/** Stato dell'associazione nel senso del protocollo. */
uint8_t commissioning_state_protocol_state(const commissioning_state_t *st);

/** Secondi che restano alla finestra, arrotondati per eccesso. */
uint32_t commissioning_state_seconds_left(const commissioning_state_t *st);

/** Vero se lo schermo deve mostrare la schermata di commissioning. */
bool commissioning_state_screen_active(const commissioning_state_t *st);
