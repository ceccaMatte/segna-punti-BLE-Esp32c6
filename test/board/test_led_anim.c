/**
 * @file test_led_anim.c
 * @brief Test host dello spettacolo di luci sul LED di bordo.
 *
 * Lo spettacolo e' fatto di tempo, ed e' proprio il tempo la cosa piu' difficile
 * da controllare guardando la scheda: un colore sbagliato per cinquanta
 * millisecondi non si nota, mentre si nota benissimo se il colore finale e'
 * quello sbagliato, se lo spettacolo non finisce mai o se si spegne a meta'.
 * Qui si chiede il colore a intervalli e si guarda cosa succede.
 *
 * SPDX-License-Identifier: MIT
 */

#include "led_anim.h"

#include "history.h"
#include "palette.h"
#include "test_util.h"

/* -------------------------------------------------------------------------- */
/* Aiutanti                                                                   */
/* -------------------------------------------------------------------------- */

/** Il colore che deve restare acceso dopo un punto, tetto compreso. */
static led_rgb_t expected(team_t team)
{
    const led_rgb_t full = (team == TEAM_US)
        ? (led_rgb_t){ PALETTE_NOI_R, PALETTE_NOI_G, PALETTE_NOI_B }
        : (led_rgb_t){ PALETTE_LORO_R, PALETTE_LORO_G, PALETTE_LORO_B };

    const led_rgb_t out = {
        .r = (uint8_t)((uint32_t)full.r * LED_BRIGHTNESS_PERMILLE / 1000u),
        .g = (uint8_t)((uint32_t)full.g * LED_BRIGHTNESS_PERMILLE / 1000u),
        .b = (uint8_t)((uint32_t)full.b * LED_BRIGHTNESS_PERMILLE / 1000u),
    };
    return out;
}

static bool same(led_rgb_t a, led_rgb_t b)
{
    return (a.r == b.r) && (a.g == b.g) && (a.b == b.b);
}

static bool dark(led_rgb_t c)
{
    return (c.r == 0u) && (c.g == 0u) && (c.b == 0u);
}

/* -------------------------------------------------------------------------- */
/* Casi                                                                       */
/* -------------------------------------------------------------------------- */

static void test_spento_finche_nessuno_segna(void)
{
    const char *name = "LED: spento finche' nessuno ha segnato";
    test_begin(name);

    led_anim_init();

    CHECK(!led_anim_showing(0));
    CHECK(dark(led_anim_colour(0)));
    CHECK(dark(led_anim_colour(12345u)));

    /* Il primo aggiornamento manda il colore al LED, i successivi tacciono:
       il ciclo principale gira ogni cinque millisecondi e non deve scrivere
       sul LED duecento volte al secondo per dirgli sempre la stessa cosa. */
    led_rgb_t out = { 9u, 9u, 9u };
    CHECK(led_anim_update(0, &out));
    CHECK(dark(out));
    CHECK(!led_anim_update(0, &out));
    CHECK(!led_anim_update(1000u, &out));

    test_end(name);
}

static void test_spettacolo_vario(void)
{
    const char *name = "LED: lo spettacolo cambia colore e non supera il tetto";
    test_begin(name);

    led_anim_init();
    led_anim_point(TEAM_THEM, 0);

    const uint8_t cap = (uint8_t)(255u * LED_BRIGHTNESS_PERMILLE / 1000u);

    led_rgb_t previous = led_anim_colour(0);
    int changes = 0;

    for (uint32_t t = 10u; t <= LED_SHOW_MS; t += 10u) {
        const led_rgb_t c = led_anim_colour(t);

        if (!same(c, previous)) {
            changes++;
        }
        previous = c;

        CHECK(c.r <= cap);
        CHECK(c.g <= cap);
        CHECK(c.b <= cap);

        /* Mai spento: un lampo nero si legge come un guasto, non come un
           effetto. */
        CHECK(!dark(c));
    }

    /* Centosessanta campioni: con due o tre colori sarebbe un'accensione,
       non uno spettacolo. */
    CHECK(changes > 100);

    test_end(name);
}

static void test_punto_finisce_sul_colore(void)
{
    const char *name = "LED: un punto finisce sul colore della squadra";
    test_begin(name);

    led_anim_init();
    led_anim_point(TEAM_US, 1000u);

    const led_rgb_t noi = expected(TEAM_US);

    CHECK(led_anim_showing(1000u));
    CHECK(led_anim_showing(1000u + LED_SHOW_MS - 1u));
    CHECK(!led_anim_showing(1000u + LED_SHOW_MS));

    /* Durante lo spettacolo il colore non e' ancora quello finale... */
    CHECK(!same(led_anim_colour(1000u), noi));
    CHECK(!same(led_anim_colour(1000u + LED_SHOW_MS / 2u), noi));

    /* ...e non e' nemmeno spento. */
    CHECK(!dark(led_anim_colour(1000u)));
    CHECK(!dark(led_anim_colour(1000u + LED_SHOW_MS / 2u)));

    /* Alla fine e' esattamente il colore della squadra, e ci resta. */
    CHECK(same(led_anim_colour(1000u + LED_SHOW_MS), noi));
    CHECK(same(led_anim_colour(1000u + LED_SHOW_MS + 60000u), noi));

    test_end(name);
}

static void test_annulla_non_fa_spettacolo(void)
{
    const char *name = "LED: annulla non fa spettacolo, il colore torna indietro";
    test_begin(name);

    led_anim_init();
    led_anim_point(TEAM_THEM, 0);   /* segna LORO */
    led_anim_point(TEAM_US, 500u);  /* poi NOI: lo spettacolo riparte da capo */

    CHECK(led_anim_showing(500u));

    /* Annullando si torna al punto di LORO: subito, senza animazione. */
    led_anim_undo();

    CHECK(!led_anim_showing(500u));
    CHECK(same(led_anim_colour(500u), expected(TEAM_THEM)));

    /* Annullando ancora non resta nessun punto: spento. */
    led_anim_undo();

    CHECK(!led_anim_showing(500u));
    CHECK(dark(led_anim_colour(500u)));

    /* Un annullamento in piu' non fa danni. */
    led_anim_undo();
    CHECK(dark(led_anim_colour(500u)));

    test_end(name);
}

static void test_azzeramento(void)
{
    const char *name = "LED: l'azzeramento spegne e svuota la memoria";
    test_begin(name);

    led_anim_init();
    led_anim_point(TEAM_US, 0);
    led_anim_point(TEAM_THEM, 100u);

    led_anim_reset();

    CHECK(!led_anim_showing(200u));
    CHECK(dark(led_anim_colour(200u)));

    /* Svuotata la memoria, un annullamento non riporta nessuno. */
    led_anim_undo();
    CHECK(dark(led_anim_colour(200u)));

    test_end(name);
}

static void test_secondo_punto_sostituisce(void)
{
    const char *name = "LED: due punti ravvicinati, vince l'ultimo";
    test_begin(name);

    led_anim_init();
    led_anim_point(TEAM_THEM, 0);
    led_anim_point(TEAM_US, 300u);

    CHECK(led_anim_showing(300u));
    CHECK(!led_anim_showing(300u + LED_SHOW_MS));
    CHECK(same(led_anim_colour(300u + LED_SHOW_MS), expected(TEAM_US)));

    /* Lo spettacolo e' ripartito da capo: a meta' strada il colore non e'
       ancora quello finale, anche se il primo punto era cominciato prima. */
    CHECK(!same(led_anim_colour(300u + LED_SHOW_MS / 2u), expected(TEAM_US)));

    test_end(name);
}

static void test_memoria_punti(void)
{
    const char *name = "LED: piu' punti di quanti ne stiano, conta l'ultimo";
    test_begin(name);

    led_anim_init();

    const int total = HISTORY_CAPACITY + 4;
    for (int i = 0; i < total; i++) {
        led_anim_point((i % 2 == 0) ? TEAM_THEM : TEAM_US, (uint32_t)i * 5000u);
    }

    /* L'ultimo punto ha avviato lo spettacolo. */
    CHECK(led_anim_showing((uint32_t)(total - 1) * 5000u));

    /*
     * Tre annullamenti riportano ai tre punti precedenti, che sono ancora in
     * memoria anche se i primi punti della partita sono stati dimenticati.
     */
    for (int k = 1; k <= 3; k++) {
        const team_t before = ((total - 1 - k) % 2 == 0) ? TEAM_THEM : TEAM_US;
        led_anim_undo();

        CHECK(!led_anim_showing(0));
        CHECK(same(led_anim_colour(0), expected(before)));
    }

    test_end(name);
}

static void test_valori_fissi(void)
{
    const char *name = "LED: i colori sono quelli, e sono questi";
    test_begin(name);

    /*
     * Questi numeri sono il contratto, non una comodita': il verde e' quello
     * dei pannelli e l'azzurro quello di NOI, al quaranta per cento. Se
     * cambiano, e' cambiato qualcosa che si vede sullo schermo e sul tavolo, e
     * allora il test deve fermarsi qui invece di adeguarsi da solo.
     */
    CHECK_EQ(PALETTE_LORO_R, 58);
    CHECK_EQ(PALETTE_LORO_G, 216);
    CHECK_EQ(PALETTE_LORO_B, 138);
    CHECK_EQ(PALETTE_NOI_R, 74);
    CHECK_EQ(PALETTE_NOI_G, 188);
    CHECK_EQ(PALETTE_NOI_B, 252);
    CHECK_EQ(LED_BRIGHTNESS_PERMILLE, 400);

    led_anim_init();
    led_anim_point(TEAM_THEM, 0);
    const led_rgb_t loro = led_anim_colour(LED_SHOW_MS);
    CHECK_EQ(loro.r, 23);
    CHECK_EQ(loro.g, 86);
    CHECK_EQ(loro.b, 55);

    led_anim_init();
    led_anim_point(TEAM_US, 0);
    const led_rgb_t noi = led_anim_colour(LED_SHOW_MS);
    CHECK_EQ(noi.r, 29);
    CHECK_EQ(noi.g, 75);
    CHECK_EQ(noi.b, 100);

    test_end(name);
}

static void test_ripetibile(void)
{
    const char *name = "LED: lo stesso istante da' sempre lo stesso colore";
    test_begin(name);

    led_anim_init();
    led_anim_point(TEAM_US, 0);

    /* Il colore dipende solo dall'istante: se cosi' non fosse, il LED
       dipenderebbe da quante volte e' stato chiesto, e sarebbe impossibile
       ragionarci sopra. */
    for (uint32_t t = 0; t <= LED_SHOW_MS; t += 137u) {
        CHECK(same(led_anim_colour(t), led_anim_colour(t)));
    }

    led_rgb_t out;
    CHECK(led_anim_update(200u, &out));
    CHECK(!led_anim_update(200u, &out));
    CHECK(led_anim_update(210u, &out));

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_led_anim_all(void)
{
    printf("Spettacolo di luci del LED\n");

    test_spento_finche_nessuno_segna();
    test_spettacolo_vario();
    test_punto_finisce_sul_colore();
    test_annulla_non_fa_spettacolo();
    test_azzeramento();
    test_secondo_punto_sostituisce();
    test_memoria_punti();
    test_valori_fissi();
    test_ripetibile();

    printf("\n");
}
