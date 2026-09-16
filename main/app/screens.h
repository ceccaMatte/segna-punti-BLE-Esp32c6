/**
 * @file screens.h
 * @brief Cosa c'e' scritto sullo schermo, in questo momento.
 *
 * Le schermate sono tre e non convivono mai: il punteggio della partita, la
 * schermata di commissioning e l'avviso che invita a tenere premuto. Chi
 * decide quale si vede e' questo modulo: guarda lo stato dell'associazione
 * (la pagina web non chiede di essere mostrata, si limita a esistere) e la
 * pressione in corso, che arriva da `gestures.c`.
 *
 * I ritorni dalle due schermate che coprono tutto sono l'unico passaggio che
 * ha bisogno di attenzione: quando spariscono non basta ridisegnare le zone
 * cambiate — va rifatto tutto, intestazione compresa.
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
