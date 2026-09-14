/**
 * @file rgb_led.c
 * @brief Pilotaggio del WS2812B di bordo con il periferico RMT.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rgb_led.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "led";

/* -------------------------------------------------------------------------- */
/* Tempi del WS2812                                                           */
/* -------------------------------------------------------------------------- */

/*
 * Il LED distingue lo zero dall'uno dalla durata dell'impulso alto: 0,3 us per
 * lo zero e 0,9 us per l'uno, con l'impulso basso che completa il periodo.
 *
 * Il periferico conta in colpi di clock, quindi si sceglie una risoluzione da
 * dieci megahertz: un colpo vale esattamente un decimo di microsecondo e i
 * tempi si scrivono come numeri interi piccoli. Con una risoluzione diversa
 * bisognerebbe arrotondare, e l'arrotondamento cambierebbe da un chip
 * all'altro.
 */
#define WS2812_RESOLUTION_HZ (10 * 1000 * 1000)
#define WS2812_TICKS_PER_US  10

/** I quattro tempi, in colpi di clock. */
#define WS2812_T0H_TICKS 3u  /* zero:  0,3 us alti  */
#define WS2812_T0L_TICKS 9u  /*        0,9 us bassi */
#define WS2812_T1H_TICKS 9u  /* uno:   0,9 us alti  */
#define WS2812_T1L_TICKS 3u  /*        0,3 us bassi */

/*
 * Pausa finale: il LED la usa per capire che il colore e' completo.
 *
 * Il foglio dati chiede almeno cinquanta microsecondi, ma le versioni piu'
 * recenti del componente ne vogliono duecentottanta. Il costo e' di un quarto
 * di millisecondo per colore: si sta larghi, che costa meno di un LED che
 * qualche volta non aggiorna.
 */
#define WS2812_RESET_TICKS (280u * WS2812_TICKS_PER_US)

/** Un colore sono ventiquattro bit piu' il simbolo della pausa finale. */
#define WS2812_SYMBOLS 25u

/**
 * Simboli pronti per il periferico.
 *
 * Il periferico legge questa memoria mentre trasmette, quindi non si puo'
 * riscriverla finche' la trasmissione non e' finita: e' il motivo per cui
 * rgb_led_set() aspetta. Tenerla fissa evita di allocare a ogni colore, e con
 * un LED solo non c'e' nessun motivo di averne piu' di una.
 */
static rmt_symbol_word_t s_symbols[WS2812_SYMBOLS];

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static bool s_ready;

/* -------------------------------------------------------------------------- */
/* Simboli                                                                    */
/* -------------------------------------------------------------------------- */

/** Un bit: impulso alto corto o lungo, poi il basso che completa il periodo. */
static rmt_symbol_word_t bit_symbol(bool one)
{
    if (one) {
        return (rmt_symbol_word_t){ .level0 = 1, .duration0 = WS2812_T1H_TICKS,
                                    .level1 = 0, .duration1 = WS2812_T1L_TICKS };
    }
    return (rmt_symbol_word_t){ .level0 = 1, .duration0 = WS2812_T0H_TICKS,
                                .level1 = 0, .duration1 = WS2812_T0L_TICKS };
}

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

esp_err_t rgb_led_init(int gpio)
{
    if (s_ready) {
        return ESP_OK;
    }
    if (gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * La coda dei simboli e' dimensionata per un colore intero, la pausa
     * compresa: il periferico di questo chip ne ha quarantotto per canale.
     */
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = (gpio_num_t)gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RESOLUTION_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
    };

    esp_err_t err = rmt_new_tx_channel(&channel_config, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "canale RMT non disponibile: %s", esp_err_to_name(err));
        return err;
    }

    /* Il codificatore di copia non ha niente da configurare: ricopia i simboli
       cosi' come sono. La configurazione esiste lo stesso, e va passata. */
    const rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codificatore non disponibile: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "canale RMT non avviato: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;

    /* Si parte da spento: il LED di bordo non ha memoria di chi c'era prima. */
    const led_rgb_t off = { 0u, 0u, 0u };
    rgb_led_set(&off);

    ESP_LOGI(TAG, "LED RGB acceso su GPIO%d", gpio);
    return ESP_OK;
}

bool rgb_led_ready(void)
{
    return s_ready;
}

void rgb_led_set(const led_rgb_t *colour)
{
    if (!s_ready || colour == NULL) {
        return;
    }

    /*
     * Il LED vuole i canali nell'ordine verde, rosso, blu, e ogni canale a
     * partire dal bit piu' significativo. Sembra una stranezza del componente,
     * ed e' invece come e' fatto: sbagliare qui scambia i colori senza che
     * niente si lamenti.
     */
    const uint8_t channels[3] = { colour->g, colour->r, colour->b };

    size_t index = 0;
    for (size_t channel = 0; channel < 3u; ++channel) {
        for (int bit = 7; bit >= 0; --bit) {
            s_symbols[index++] = bit_symbol(((channels[channel] >> bit) & 1u) != 0u);
        }
    }

    /* La pausa finale si scrive in due meta' per stare dentro i quindici bit
       che il simbolo concede a ciascun livello. */
    s_symbols[index] = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = WS2812_RESET_TICKS / 2u,
        .level1 = 0, .duration1 = WS2812_RESET_TICKS / 2u,
    };

    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };

    const esp_err_t err = rmt_transmit(s_channel, s_encoder, s_symbols,
                                       sizeof(s_symbols), &transmit_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "colore rifiutato: %s", esp_err_to_name(err));
        return;
    }

    /*
     * Si aspetta la fine prima di tornare: i simboli sono una copia sola e il
     * colore successivo li riscriverebbe mentre il periferico li sta ancora
     * leggendo. Una trasmissione intera, pausa compresa, sta sotto il
     * millisecondo.
     */
    (void)rmt_tx_wait_all_done(s_channel, 50);
}
