/**
 * @file ui.h
 * @brief Disegno della schermata del segnapunti.
 *
 * Riceve una descrizione di quello che va mostrato (::ui_view_t), confronta con
 * quello che c'e' gia' sullo schermo e ridisegna soltanto le parti cambiate.
 *
 * Mentre si gioca, un punto fa cambiare solo il pannello della squadra che l'ha
 * vinto: si ridisegnano circa diciassettemila pixel invece dei cinquantacinquemila
 * dello schermo intero. Il pannello, e non la sola cifra, e' l'unita' di
 * ridisegno: un pannello ha gli angoli arrotondati, quindi rifare solo la cifra
 * lascerebbe degli spigoli vivi sopra la sagoma.
 *
 * Questo modulo non conosce le regole del padel: non sa cosa sia un vantaggio
 * ne' un tie-break. Riceve testo gia' pronto.
 */
#pragma once

#include <stdbool.h>

#include "ui_view.h"

/**
 * @brief Prepara lo schermo: cancella tutto e disegna la parte fissa.
 *
 * Da chiamare una volta dopo display_init().
 */
void ui_init(void);

/**
 * @brief Allinea lo schermo alla descrizione ricevuta.
 *
 * Se non e' cambiato niente non fa nulla. La prima chiamata dopo ui_init()
 * disegna tutto.
 */
void ui_update(const ui_view_t *view);

/**
 * @brief Dimentica quello che c'e' sullo schermo.
 *
 * La prossima ::ui_update ridisegnera' ogni cosa. Serve dopo che qualcun altro
 * ha scritto sul pannello, per esempio la figura di prova.
 */
void ui_invalidate(void);

/** Quante zone sono state ridisegnate nell'ultimo aggiornamento. */
int ui_last_dirty_count(void);

/** Quanti pixel sono stati ridisegnati nell'ultimo aggiornamento. */
unsigned long ui_last_dirty_pixels(void);
