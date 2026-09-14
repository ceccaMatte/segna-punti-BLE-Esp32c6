/**
 * @file led_anim.c
 * @brief Lo spettacolo di luci sul LED RGB.
 *
 * SPDX-License-Identifier: MIT
 */

#include "led_anim.h"

#include <string.h>

#include "history.h"
#include "palette.h"

/* -------------------------------------------------------------------------- */
/* Come e' fatto lo spettacolo                                                */
/* -------------------------------------------------------------------------- */

/*
 * Due fasi, una dopo l'altra:
 *
 *   0 .. LED_SPIN_MS            la tinta gira e la luminosita' respira
 *   LED_SPIN_MS .. LED_SHOW_MS  la tinta si posa su quella della squadra,
 *                               sempre piu' lentamente
 *
 * Alla fine della prima fase il colore e' ancora quello che girava, quindi nel
 * punto di incontro fra le due fasi non c'e' nessun salto: la seconda fase e'
 * una fusione, non un cambio.
 *
 * Durante lo spettacolo il colore non si spegne mai del tutto. Un lampo nero
 * sembrerebbe un guasto invece che un effetto, e per la stessa ragione il
 * respiro di luminosita' non parte da zero.
 */

/** Da qui comincia la posa sul colore della squadra. */
#define LED_SPIN_MS    1000u

/** Giri completi di tinta durante la prima fase. */
#define LED_SPIN_TURNS 2u

/** Durata di un respiro di luminosita'. */
#define LED_PULSE_MS   250u

/** Luminosita' minima del respiro, in duecentocinquantacinquesimi. */
#define LED_PULSE_MIN  150u

#if LED_SHOW_MS <= LED_SPIN_MS
#error "Lo spettacolo deve durare piu' della fase che gira, altrimenti la posa sul colore non comincia mai."
#endif

/* -------------------------------------------------------------------------- */
/* Stato                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * Chi ha segnato, uno per punto.
 *
 * Serve all'annullamento: per sapere che colore lasciare acceso dopo aver tolto
 * un punto bisogna ricordarsi non solo l'ultimo, ma anche tutti quelli prima,
 * perche' si puo' annullare piu' volte di seguito.
 *
 * Il fondo scala e' lo stesso della cronologia della partita: i due si svuotano
 * e si riempiono insieme, quindi non possono raccontare storie diverse. Quando
 * e' pieno si scarta il piu' vecchio, esattamente come fa la cronologia, perche'
 * l'unico ricordo che conta e' l'ultimo.
 */
static uint8_t  s_scorers[HISTORY_CAPACITY];
static uint8_t  s_scorer_count;

/** Falso quando non c'e' nessuna squadra da mostrare: LED spento. */
static bool     s_lit;

/** Squadra dell'ultimo punto, valida solo con s_lit. */
static team_t   s_scorer;

/** Vero mentre lo spettacolo e' in corso. */
static bool     s_showing;

/** Istante in cui e' cominciato lo spettacolo. */
static uint32_t s_show_start;

/** Ultimo colore pubblicato, e se e' mai stato pubblicato. */
static led_rgb_t s_published;
static bool      s_published_valid;

/* -------------------------------------------------------------------------- */
/* Colori                                                                     */
/* -------------------------------------------------------------------------- */

/** Il colore pieno di una squadra, senza tetto di luminosita'. */
static void team_colour(team_t team, led_rgb_t *out)
{
    if (team == TEAM_US) {
        out->r = PALETTE_NOI_R;
        out->g = PALETTE_NOI_G;
        out->b = PALETTE_NOI_B;
    } else {
        out->r = PALETTE_LORO_R;
        out->g = PALETTE_LORO_G;
        out->b = PALETTE_LORO_B;
    }
}

/** Applica il tetto di luminosita'. */
static led_rgb_t dim(led_rgb_t c)
{
    const uint32_t k = LED_BRIGHTNESS_PERMILLE;

    c.r = (uint8_t)((uint32_t)c.r * k / 1000u);
    c.g = (uint8_t)((uint32_t)c.g * k / 1000u);
    c.b = (uint8_t)((uint32_t)c.b * k / 1000u);
    return c;
}

/**
 * Mescola due colori.
 *
 * Con peso 255 restituisce il secondo esatto, non un'approssimazione: e' quello
 * che rende esatto il colore finale dello spettacolo.
 */
static led_rgb_t mix(led_rgb_t a, led_rgb_t b, uint8_t weight)
{
    const uint32_t w = weight;
    const uint32_t w_inv = 255u - weight;

    const led_rgb_t out = {
        .r = (uint8_t)((a.r * w_inv + b.r * w) / 255u),
        .g = (uint8_t)((a.g * w_inv + b.g * w) / 255u),
        .b = (uint8_t)((a.b * w_inv + b.b * w) / 255u),
    };
    return out;
}

/**
 * Da tinta, saturazione e luminosita' al colore.
 *
 * La ruota dei colori e' divisa in sei spicchi di sessanta gradi: dentro ogni
 * spicchio un canale sta al massimo, uno sta a zero e il terzo sale o scende.
 */
static led_rgb_t from_hsv(uint32_t hue, uint8_t sat, uint8_t value)
{
    const uint32_t sector = hue / 60u;   /* 0 .. 5 */
    const uint32_t rem = hue % 60u;      /* quanto si e' entrati nello spicchio */

    const uint8_t low = (uint8_t)((uint32_t)value * (255u - sat) / 255u);
    const uint8_t falling = (uint8_t)((uint32_t)value *
                                      (255u - (uint32_t)sat * rem / 60u) / 255u);
    const uint8_t rising = (uint8_t)((uint32_t)value *
                                     (255u - (uint32_t)sat * (60u - rem) / 60u) / 255u);

    switch (sector) {
    case 0:  return (led_rgb_t){ value, rising, low };
    case 1:  return (led_rgb_t){ falling, value, low };
    case 2:  return (led_rgb_t){ low, value, rising };
    case 3:  return (led_rgb_t){ low, falling, value };
    case 4:  return (led_rgb_t){ rising, low, value };
    default: return (led_rgb_t){ value, low, falling };
    }
}

/* -------------------------------------------------------------------------- */
/* Le due fasi                                                                */
/* -------------------------------------------------------------------------- */

/** La giostra: la tinta gira e la luminosita' respira. */
static led_rgb_t spinning(uint32_t elapsed_ms)
{
    const uint32_t hue = (elapsed_ms * (LED_SPIN_TURNS * 360u) / LED_SPIN_MS) % 360u;

    /* Respiro a dente di sega raddrizzato: sale e scende senza salti. */
    const uint32_t ramp = (elapsed_ms % LED_PULSE_MS) * 255u / LED_PULSE_MS;
    const uint32_t breath = (ramp < 128u) ? (ramp * 2u) : ((255u - ramp) * 2u);
    const uint8_t value = (uint8_t)(LED_PULSE_MIN +
                                    breath * (255u - LED_PULSE_MIN) / 255u);

    return from_hsv(hue, 255u, value);
}

/**
 * Quanto si e' vicini al colore della squadra, da 0 a 255.
 *
 * La crescita rallenta avvicinandosi alla fine, cosi' il colore si posa invece
 * di arrivare di colpo: e' l'unica differenza che si nota fra un incrocio di
 * colori e un interruttore.
 */
static uint8_t settling(uint32_t elapsed_ms)
{
    if (elapsed_ms <= LED_SPIN_MS) {
        return 0u;
    }
    if (elapsed_ms >= LED_SHOW_MS) {
        return 255u;
    }

    const uint32_t x = (elapsed_ms - LED_SPIN_MS) * 255u / (LED_SHOW_MS - LED_SPIN_MS);
    const uint32_t inv = 255u - x;
    return (uint8_t)(255u - inv * inv / 255u);
}

/* -------------------------------------------------------------------------- */
/* Memoria dei punti                                                          */
/* -------------------------------------------------------------------------- */

static void remember(team_t scorer)
{
    if (s_scorer_count >= HISTORY_CAPACITY) {
        /* Si scarta il piu' vecchio: l'ultimo deve restare l'ultimo. */
        memmove(&s_scorers[0], &s_scorers[1], (size_t)(HISTORY_CAPACITY - 1));
        s_scorer_count = HISTORY_CAPACITY - 1;
    }
    s_scorers[s_scorer_count++] = (uint8_t)scorer;
}

/** Riallinea il colore mostrato a chi ha segnato per ultimo. */
static void show_last(void)
{
    if (s_scorer_count == 0u) {
        s_lit = false;
        return;
    }
    s_lit = true;
    s_scorer = (team_t)s_scorers[s_scorer_count - 1];
}

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

void led_anim_init(void)
{
    led_anim_reset();
    s_published_valid = false;
}

void led_anim_reset(void)
{
    s_scorer_count = 0u;
    s_lit = false;
    s_showing = false;
    s_show_start = 0u;
}

void led_anim_point(team_t scorer, uint32_t now_ms)
{
    remember(scorer);
    s_lit = true;
    s_scorer = scorer;
    s_showing = true;
    s_show_start = now_ms;
}

void led_anim_undo(void)
{
    if (s_scorer_count > 0u) {
        s_scorer_count--;
    }
    /* Nessuno spettacolo: il colore si sposta e basta. */
    s_showing = false;
    show_last();
}

bool led_anim_showing(uint32_t now_ms)
{
    return s_showing && (uint32_t)(now_ms - s_show_start) < LED_SHOW_MS;
}

led_rgb_t led_anim_colour(uint32_t now_ms)
{
    if (!s_lit) {
        return (led_rgb_t){ 0u, 0u, 0u };
    }

    led_rgb_t target;
    team_colour(s_scorer, &target);

    if (!led_anim_showing(now_ms)) {
        return dim(target);
    }

    const uint32_t elapsed = (uint32_t)(now_ms - s_show_start);
    return dim(mix(spinning(elapsed), target, settling(elapsed)));
}

bool led_anim_update(uint32_t now_ms, led_rgb_t *out)
{
    const led_rgb_t colour = led_anim_colour(now_ms);

    if (s_published_valid &&
        colour.r == s_published.r &&
        colour.g == s_published.g &&
        colour.b == s_published.b) {
        return false;
    }

    s_published = colour;
    s_published_valid = true;

    if (out != NULL) {
        *out = colour;
    }
    return true;
}
