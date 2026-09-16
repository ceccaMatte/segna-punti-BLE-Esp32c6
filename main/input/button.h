/**
 * @file button.h
 * @brief Macchina a stati del pulsante unico (BOOT, GPIO9).
 *
 * Modulo di pura logica: riceve il livello grezzo del pin e il tempo
 * trascorso, e restituisce un evento. Non conosce GPIO ne' ESP-IDF, quindi
 * si testa su PC simulando pressioni e rilasci.
 *
 * ---------------------------------------------------------------------------
 * I gesti
 * ---------------------------------------------------------------------------
 *   un click            punto a NOI
 *   due click           punto a LORO
 *   tre click           annulla l'ultima azione
 *   quattro click       azzera la partita
 *   cinque click o piu' nessuna azione
 *   pressione fra BTN_MOMENT_MIN_MS e BTN_PAIRING_HOLD_MS, rilasciata:
 *                       MOMENT (un segno nel tempo, non tocca il punteggio)
 *   pressione oltre BTN_PAIRING_HOLD_MS:
 *                       PAIRING (apre il commissioning)
 *
 * ---------------------------------------------------------------------------
 * Perche' serve una macchina a stati
 * ---------------------------------------------------------------------------
 * Al momento del rilascio non si puo' sapere se e' finita li' o se stanno
 * arrivando altri click: il click singolo va quindi *atteso*, non eseguito.
 *
 *   - il rilascio apre una finestra (BTN_MULTI_CLICK_MS). Se entro la finestra
 *     arriva un'altra pressione, il conteggio sale e la finestra riparte.
 *   - solo allo scadere della finestra la sequenza viene interpretata.
 *   - MOMENT non si decide quando si supera la soglia, ma al rilascio: e' chi
 *     molla il pulsante a dire che il gesto e' finito, e finche' il dito e'
 *     giu' puo' sempre arrivare la soglia del commissioning. Una pressione
 *     che finisce oltre la soglia breve scarta la sequenza di click in sospeso:
 *     il gesto e' uno solo, il piu' lungo.
 *   - la soglia del commissioning, invece, scatta mentre si tiene premuto,
 *     perche' quello che apre — la finestra di associazione — deve cominciare
 *     subito: chi tiene premuto sta aspettando un effetto, non un rilascio.
 *     Il conteggio del tempo si ferma sulla soglia, cosi' l'evento esce una
 *     volta sola anche se il pulsante resta premuto per minuti, e da li' in
 *     avanti non esce piu' niente: il rilascio non produce anche il MOMENT.
 *   - quattro click azzerano la partita; cinque o piu' non producono nulla.
 *     Il contatore NON viene saturato a quattro, altrimenti una raffica
 *     accidentale eseguirebbe l'azzeramento.
 *
 * ---------------------------------------------------------------------------
 * Tabella delle transizioni (level = livello gia' filtrato)
 * ---------------------------------------------------------------------------
 * | Stato         | Condizione                          | Azione                    | Nuovo stato  |
 * |---------------|-------------------------------------|---------------------------|--------------|
 * | IDLE          | level == premuto                    | clicks = 1, hold = 0      | PRESS        |
 * | PRESS         | hold >= PAIRING_HOLD_MS             | clicks = 0, hold = soglia, esce PAIRING | RELEASE_WAIT |
 * | PRESS         | level == rilasciato, hold >= MOMENT_MIN_MS | clicks = 0, esce MOMENT | IDLE   |
 * | PRESS         | level == rilasciato, hold < MOMENT_MIN_MS  | window = 0          | CLICK_WAIT   |
 * | CLICK_WAIT    | level == premuto                    | clicks++                  | PRESS        |
 * | CLICK_WAIT    | window >= MULTI_CLICK_MS            | dispatch(clicks)          | IDLE         |
 * | RELEASE_WAIT  | level == rilasciato                 | nessuno                   | IDLE         |
 *
 * Nota sul debounce: il rilascio viene riconosciuto fino a BTN_DEBOUNCE_MS dopo
 * l'istante reale, quindi la durata misurata di una pressione puo' risultare
 * maggiore di circa 25 ms. Una pressione di poco sotto la soglia del MOMENT
 * puo' quindi contare come MOMENT: con soglie di quest'ordine l'effetto e'
 * dell'ordine del 3%, e si vede solo in laboratorio.
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

/**
 * Durata minima di una pressione perche' il rilascio produca MOMENT.
 *
 * E' una soglia *al rilascio*: sotto questo tempo la pressione e' un click,
 * sopra e' un gesto a se' che non tocca il punteggio. Si misura dal momento in
 * cui il livello filtrato e' diventato "premuto".
 */
#ifndef BTN_MOMENT_MIN_MS
#define BTN_MOMENT_MIN_MS 800u
#endif

/**
 * Durata oltre la quale la pressione apre il commissioning.
 *
 * E' la soglia di PAIRING e scatta mentre si tiene premuto: raggiunta la
 * soglia la scheda cancella l'associazione e apre la finestra per associarne
 * una nuova. Dev'essere maggiore della soglia del MOMENT, altrimenti i due
 * gesti non si distinguerebbero.
 */
#ifndef BTN_PAIRING_HOLD_MS
#define BTN_PAIRING_HOLD_MS 5000u
#endif

#if BTN_PAIRING_HOLD_MS <= BTN_MOMENT_MIN_MS
#error "BTN_PAIRING_HOLD_MS deve essere maggiore di BTN_MOMENT_MIN_MS"
#endif

/** Finestra entro cui click successivi vengono accorpati nella stessa sequenza. */
#ifndef BTN_MULTI_CLICK_MS
#define BTN_MULTI_CLICK_MS 400u
#endif

/** Evento prodotto dalla macchina a stati. */
typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_SINGLE,    /**< 1 click                     -> punto a NOI */
    BTN_EVT_DOUBLE,    /**< 2 click                     -> punto a LORO */
    BTN_EVT_TRIPLE,    /**< 3 click                     -> UNDO        */
    BTN_EVT_QUADRUPLE, /**< 4 click                     -> RESET       */
    BTN_EVT_MOMENT,    /**< rilasciato oltre MOMENT_MIN -> MOMENT      */
    BTN_EVT_PAIRING    /**< tenuto oltre PAIRING_HOLD   -> commissioning */
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
                               /*   (si ferma alla soglia del pairing)        */
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
