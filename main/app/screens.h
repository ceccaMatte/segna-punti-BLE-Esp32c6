/**
 * @file screens.h
 * @brief Cosa c'e' scritto sullo schermo, in questo momento.
 *
 * Le schermate sono due e non convivono mai: il punteggio della partita e la
 * schermata di commissioning. Chi decide quale delle due si vede e' questo
 * modulo, e lo decide guardando lo stato dell'associazione: la pagina web non
 * chiede di essere mostrata, si limita a esistere.
 *
 * Il ritorno dalla schermata di commissioning e' l'unico passaggio che ha
 * bisogno di attenzione: quella copre tutto lo schermo, quindi quando sparisce
 * non basta ridisegnare le zone cambiate — va rifatto tutto, intestazione
 * compresa.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Prepara lo schermo: pannello, schermata di gioco e schermata di commissioning. */
void screens_init(void);

/**
 * @brief Mostra quello che c'e' da mostrare.
 *
 * @param now_ms millisecondi dall'avvio: il titolo del commissioning lampeggia
 *               con quelli.
 */
void screens_update(uint32_t now_ms);
