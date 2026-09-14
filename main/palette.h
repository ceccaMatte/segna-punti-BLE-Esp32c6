/**
 * @file palette.h
 * @brief I colori delle due squadre, in un posto solo.
 *
 * Lo stesso colore compare in due posti che non si parlano: il pannello sullo
 * schermo e il LED RGB sulla scheda. Tenendo qui i valori si evita che i due si
 * allontanino con il tempo, che e' il modo piu' facile di ritrovarsi un verde
 * sul display e un altro verde sul LED senza accorgersene.
 *
 * I numeri sono a otto bit per canale, come li vuole il LED: chi disegna li
 * converte nel formato dello schermo con palette_screen().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#include "gfx.h"
#include "match.h"

/* Verde per LORO, azzurro per NOI: sono i colori dei due pannelli. */
#define PALETTE_LORO_R 58
#define PALETTE_LORO_G 216
#define PALETTE_LORO_B 138

#define PALETTE_NOI_R  74
#define PALETTE_NOI_G  188
#define PALETTE_NOI_B  252

/** Colore della squadra nel formato dello schermo, RGB565. */
static inline uint16_t palette_screen(team_t team)
{
    if (team == TEAM_US) {
        return GFX_RGB(PALETTE_NOI_R, PALETTE_NOI_G, PALETTE_NOI_B);
    }
    return GFX_RGB(PALETTE_LORO_R, PALETTE_LORO_G, PALETTE_LORO_B);
}
