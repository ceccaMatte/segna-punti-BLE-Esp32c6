/**
 * @file button.h
 * @brief Macchina a stati del pulsante unico (BOOT, GPIO9).
 *
 * Modulo di pura logica: riceve il livello grezzo del pin e il tempo
 * trascorso, e restituisce un evento. Non conosce GPIO ne' ESP-IDF, quindi
 * si testa su PC simulando pressioni e rilasci.
 *
 * ---------------------------------------------------------------------------
 * Perche' serve una macchina a stati
 * ---------------------------------------------------------------------------
 * Con un solo pulsante bisogna distinguere quattro gesture. Il problema e' che
 * al momento del rilascio non si puo' sapere se e' finita li' o se stanno
 * arrivando altri click: il click singolo va quindi *atteso*, non eseguito.
 *
 *   - il rilascio apre una finestra (BTN_MULTI_CLICK_MS). Se entro la finestra
 *     arriva un'altra pressione, il conteggio sale e la finestra riparte.
 *   - solo allo scadere della finestra la sequenza viene interpretata.
 *   - la pressione lunga ha priorita': appena raggiunge BTN_LONG_PRESS_MS
 *     emette LONG, azzera il conteggio dei click (la sequenza pendente viene
 *     scartata) e passa a BTN_RELEASE_WAIT, che non genera altri eventi fino
 *     al rilascio.
 *   - quattro o piu' click non producono nulla: la sequenza viene scartata.
 *     Il contatore NON viene saturato a 3, altrimenti quattro pressioni
 *     accidentali eseguirebbero un UNDO.
 *
 * ---------------------------------------------------------------------------
 * Tabella delle transizioni (level = livello gia' filtrato)
 * ---------------------------------------------------------------------------
 * | Stato         | Condizione                        | Azione            | Nuovo stato  |
 * |---------------|-----------------------------------|-------------------|--------------|
 * | IDLE          | level == premuto                  | clicks = 1        | PRESS        |
 * | PRESS         | hold >= LONG_PRESS_MS             | clicks = 0, LONG  | RELEASE_WAIT |
 * | PRESS         | level == rilasciato               | window = 0        | CLICK_WAIT   |
 * | CLICK_WAIT    | level == premuto                  | clicks++          | PRESS        |
 * | CLICK_WAIT    | window >= MULTI_CLICK_MS          | dispatch(clicks)  | IDLE         |
 * | RELEASE_WAIT  | level == rilasciato               | nessuno           | IDLE         |
 *
 * Nota sul debounce: il rilascio viene riconosciuto fino a BTN_DEBOUNCE_MS dopo
 * l'istante reale, quindi la durata misurata di una pressione lunga puo'
 * risultare maggiore di circa 25 ms su 4000 (0,6%). Ininfluente in pratica.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Tempo di stabilita' richiesto per accettare un cambio di livello. */
#ifndef BTN_DEBOUNCE_MS
#define BTN_DEBOUNCE_MS 25u
#endif

/** Durata oltre la quale la pressione diventa RESET. */
#ifndef BTN_LONG_PRESS_MS
#define BTN_LONG_PRESS_MS 4000u
#endif

/** Finestra entro cui click successivi vengono accorpati nella stessa sequenza. */
#ifndef BTN_MULTI_CLICK_MS
#define BTN_MULTI_CLICK_MS 400u
#endif

/** Evento prodotto dalla macchina a stati. */
typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_SINGLE, /**< 1 click  -> punto a NOI  */
    BTN_EVT_DOUBLE, /**< 2 click  -> punto a LORO */
    BTN_EVT_TRIPLE, /**< 3 click  -> UNDO         */
    BTN_EVT_LONG    /**< 4 s      -> RESET        */
} btn_event_t;

/** Stato interno della macchina. */
typedef enum {
    BTN_STATE_IDLE = 0,
    BTN_STATE_PRESS,
    BTN_STATE_CLICK_WAIT,
    BTN_STATE_RELEASE_WAIT
} btn_state_t;

/** Istanza della macchina a stati. Una per pulsante. */
typedef struct {
    btn_state_t state;
    bool        level;         /**< livello logico filtrato (true = premuto) */
    bool        raw_last;      /**< ultimo campione grezzo                   */
    uint32_t    raw_stable_ms; /**< da quanto il grezzo e' stabile           */
    uint32_t    hold_ms;       /**< durata della pressione in corso          */
    uint32_t    window_ms;     /**< tempo trascorso nella finestra multi-click */
    uint8_t     clicks;        /**< click contati nella sequenza corrente    */
} button_t;

/** Riporta la macchina allo stato di riposo (pulsante rilasciato). */
void button_init(button_t *b);

/**
 * @brief Fa avanzare la macchina a stati di un passo.
 *
 * Da chiamare a intervalli regolari (il firmware usa 5 ms).
 *
 * @param b           istanza da aggiornare.
 * @param raw_pressed livello grezzo letto dal pin, true se premuto.
 * @param dt_ms       millisecondi trascorsi dall'ultima chiamata.
 * @return l'evento prodotto in questo passo, oppure BTN_EVT_NONE.
 */
btn_event_t button_update(button_t *b, bool raw_pressed, uint32_t dt_ms);
