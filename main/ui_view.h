/**
 * @file ui_view.h
 * @brief Descrizione di quello che deve apparire sullo schermo.
 *
 * Questo modulo sta fra il motore del punteggio e il disegno vero e proprio.
 * Trasforma uno stato di partita in un piccolo riassunto di quello che va
 * mostrato, e sa dire quali parti sono cambiate rispetto alla volta precedente.
 *
 * E' la parte che decide *cosa* cambia; il disegno decide solo *come* si
 * disegna. Tenendoli separati, la logica dell'aggiornamento si puo' provare sul
 * PC: e' li' che si nascondono gli errori che sulla scheda si vedrebbero solo
 * come uno sfarfallio sporadico.
 *
 * Nessuna dipendenza da ESP-IDF.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "match.h"

/** Lunghezza massima del testo di un punteggio, terminatore incluso. */
#define UI_SCORE_TEXT_MAX 8

/* -------------------------------------------------------------------------- */
/* Disposizione sullo schermo, in pixel                                      */
/* -------------------------------------------------------------------------- */

/*
 * La disposizione sta qui, insieme alla descrizione di cosa va mostrato, e non
 * nel modulo che disegna. Cosi' i riquadri delle zone sono noti anche ai test
 * sul PC: la promessa "nessun ridisegno a schermo intero durante il gioco" si
 * puo' verificare in modo esatto, invece di guardare lo schermo e sperare.
 *
 * Da sinistra a destra e dall'alto in basso:
 *
 *   y   4 .. 15   titolo
 *   y  18 .. 34   riga di separazione, con l'emblema al centro
 *   y   3 .. 19   distintivo del tie-break, in alto a destra
 *   y  35 .. 193  i due pannelli con il loro alone, LORO a sinistra e NOI a destra
 *   y 196 .. 250  riquadro dei game
 *   y 256 .. 310  riquadro dei set
 */

#define UI_SCREEN_W      172
#define UI_SCREEN_H      320

#define UI_HEADER_TEXT_Y 3

/** Riga su cui e' centrato l'emblema della riga di separazione. */
#define UI_DIVIDER_CY    26

#define UI_PANEL_Y       38
#define UI_PANEL_W       79
#define UI_PANEL_H       152
#define UI_PANEL_LORO_X  3
#define UI_PANEL_NOI_X   90

/**
 * Quanti pixel di alone escono dal bordo del pannello.
 *
 * Le zone da ridisegnare devono comprendere anche questi, altrimenti l'alone
 * resta indietro quando il pannello cambia e si vede un bordo fantasma.
 */
#define UI_PANEL_GLOW    3

#define UI_CARD_X        8
#define UI_CARD_W        156
#define UI_CARD_H        54
#define UI_GAME_Y        196
#define UI_SET_Y         256

#define UI_TB_X          142
#define UI_TB_Y          3
#define UI_TB_W          26
#define UI_TB_H          16

/*
 * Misure del contenuto dentro i riquadri.
 *
 * Stanno qui, e non nel file che disegna, perche' i test sul PC devono poterle
 * usare: sono le stesse che decidono se una cifra entra nel pannello, e un
 * errore qui si vedrebbe solo come testo tagliato sullo schermo.
 */

/** Margine fra il bordo del pannello e quello che ci sta dentro. */
#define UI_PANEL_INSET   3

/** Raggio degli angoli dei pannelli. */
#define UI_PANEL_RADIUS  8

/** Spessore del bordo luminoso dei pannelli. */
#define UI_PANEL_BORDER  2

/** Spazio orizzontale per la cifra grande. */
#define UI_SCORE_MAX_W   (UI_PANEL_W - 2 * UI_PANEL_INSET)

/** Centro delle colonne, per allineare nomi, pallini e punteggi. */
#define UI_COL_LORO_CX   (UI_PANEL_LORO_X + UI_PANEL_W / 2)
#define UI_COL_NOI_CX    (UI_PANEL_NOI_X + UI_PANEL_W / 2)

/** Centro della schermata, dove stanno titolo, emblema ed etichette. */
#define UI_CENTER_CX     (UI_SCREEN_W / 2)

/** Riga superiore del nome della squadra. */
#define UI_NAME_Y        (UI_PANEL_Y + 14)

/** Centro del pallino che indica chi serve. */
#define UI_DOT_CY        (UI_PANEL_Y + 50)

/** Raggio del pallino. */
#define UI_DOT_R         5

/** Riga superiore della scritta SERVE, sotto il pallino. */
#define UI_SERVE_Y       (UI_PANEL_Y + 58)

/** Fascia verticale in cui viene centrata la cifra grande. */
#define UI_SCORE_Y       (UI_PANEL_Y + 72)
#define UI_SCORE_H       80

/** Margine fra il bordo del riquadro e l'etichetta GAME o SET. */
#define UI_CARD_LABEL_DY 7

/** Riga superiore dei due numeri dentro il riquadro. */
#define UI_CARD_VALUE_DY 22

/** Distanza fra un numero e la barretta che lo separa dall'altro. */
#define UI_CARD_DASH_GAP 8

/** Dimensioni della barretta fra i due numeri. */
#define UI_CARD_DASH_W   10
#define UI_CARD_DASH_H   3

/*
 * Emblema della riga di separazione: due racchette con i manici che si
 * incrociano.
 *
 * A questa dimensione un disegno fedele sarebbe illeggibile: restano le due
 * sagome con i manici incrociati, che e' quanto basta a riconoscere il tema
 * senza rubare spazio al punteggio.
 */
#define UI_EMBLEM_HEAD_W  11
#define UI_EMBLEM_HEAD_H  11
#define UI_EMBLEM_HANDLE  7
#define UI_EMBLEM_W       (UI_EMBLEM_HEAD_W * 2 + 4)
#define UI_EMBLEM_H       (UI_EMBLEM_HEAD_H + UI_EMBLEM_HANDLE)

/** Riga superiore dell'emblema, centrato sulla riga di separazione. */
#define UI_EMBLEM_TOP     (UI_DIVIDER_CY - 9)

/** Spazio vuoto fra l'emblema e l'inizio dei due tratti di linea. */
#define UI_EMBLEM_GAP     6

/**
 * Le zone dello schermo che possono cambiare.
 *
 * Sono poche e volutamente grandi: il pannello intero, non solo la cifra. Un
 * pannello ha gli angoli arrotondati, quindi ridisegnare solo la cifra
 * lascerebbe degli spigoli vivi sopra la sagoma arrotondata. Ridisegnare il
 * pannello intero costa poco di piu' ed elimina del tutto il problema.
 */
typedef enum {
    UI_SLOT_TB = 0,   /**< distintivo del tie-break, in alto a destra   */
    UI_SLOT_LORO,     /**< pannello di sinistra, nome e punteggio       */
    UI_SLOT_NOI,      /**< pannello di destra, nome e punteggio         */
    UI_SLOT_GAME,     /**< riga dei game vinti nel set                  */
    UI_SLOT_SET,      /**< riga dei set vinti                           */
    UI_SLOT_OVERLAY,  /**< schermata del vincitore, copre tutto          */
    UI_SLOT_COUNT
} ui_slot_t;

/** Maschera di bit con un bit per zona. */
#define UI_SLOT_BIT(slot) (1u << (unsigned)(slot))

/** Maschera con tutte le zone. */
#define UI_SLOT_ALL ((1u << (unsigned)UI_SLOT_COUNT) - 1u)

/** Quello che deve apparire sullo schermo in questo momento. */
typedef struct {
    bool    tie_break;                     /**< partita decisa al tie-break   */
    char    loro_score[UI_SCORE_TEXT_MAX]; /**< "0" "15" "30" "40" "AD" "7"    */
    char    noi_score[UI_SCORE_TEXT_MAX];
    bool    loro_serve;                    /**< tocca a loro servire          */
    bool    noi_serve;
    uint8_t loro_games;                    /**< game vinti nel set corrente   */
    uint8_t noi_games;
    uint8_t loro_sets;                     /**< set vinti                     */
    uint8_t noi_sets;
    bool    overlay;                       /**< schermata finale visibile     */
    team_t  winner;                        /**< vale solo con overlay attivo  */
} ui_view_t;

/**
 * @brief Compila la descrizione dello schermo a partire dalla partita.
 *
 * Durante il tie-break il punteggio mostrato sono i punti numerici, non 0/15/30.
 */
void ui_view_build(const MatchState *m, ui_view_t *out);

/** Una descrizione vuota, come se non fosse ancora stato disegnato niente. */
void ui_view_clear(ui_view_t *out);

/**
 * @brief Dice quali zone vanno ridisegnate.
 *
 * Confronta la descrizione nuova con quella precedente e restituisce una
 * maschera con un bit per ogni zona cambiata.
 *
 * La comparsa o la scomparsa della schermata finale rende sporco tutto: quella
 * schermata copre l'intero pannello, quindi quando se ne va non resta nulla di
 * valido sotto.
 *
 * @return maschera di bit, zero se non e' cambiato niente.
 */
uint32_t ui_view_diff(const ui_view_t *previous, const ui_view_t *current);

/** Vero se le due descrizioni sono identiche in ogni campo. */
bool ui_view_equal(const ui_view_t *a, const ui_view_t *b);

/** Nome leggibile di una zona, per i log e i messaggi di errore. */
const char *ui_slot_name(ui_slot_t slot);

/**
 * @brief Il rettangolo che occupa una zona dello schermo.
 *
 * I rettangoli devono restare disgiunti fra loro: e' la premessa su cui si
 * appoggia l'aggiornamento a zone, perche' garantisce che ridisegnarne una non
 * possa rovinare il contenuto di un'altra. L'unica eccezione e' la schermata
 * del vincitore, che copre tutto ed e' percio' disegnata per ultima.
 */
gfx_rect_t ui_view_slot_rect(ui_slot_t slot);

/**
 * @brief La zona da mandare al pannello al primo disegno.
 *
 * Non e' l'unione delle zone, ed e' importante che non lo sia: l'intestazione,
 * la riga di separazione e il fondo della pagina vengono disegnati una volta
 * sola all'avvio e non appartengono a nessuna zona. Un primo aggiornamento
 * limitato alle zone li lascerebbe nella memoria del controller senza mai
 * mandarli al pannello, insieme ai pixel che c'erano all'accensione.
 */
gfx_rect_t ui_view_first_paint_rect(void);

/** Vero se due zone si sovrappongono. La schermata del vincitore e' esclusa. */
bool ui_slot_rects_overlap(void);
