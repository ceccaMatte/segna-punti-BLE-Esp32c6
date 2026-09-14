/**
 * @file display.c
 * @brief Pilotaggio del pannello ST7789 da 172x320 su bus SPI.
 *
 * Struttura del file:
 *   - il buffer dell'immagine, in memoria interna;
 *   - un buffer di servizio piu' piccolo, usato per parlare con il pannello;
 *   - l'inizializzazione di bus, controller e retroilluminazione;
 *   - l'aggiornamento a pezzi.
 *
 * Le quattro incognite che non si possono dedurre dai documenti sono raccolte
 * nelle costanti qui sotto, in modo da poterle cambiare senza toccare la logica.
 */
#include "display.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"

#include "board.h"
#include "gfx.h"

static const char *TAG = "display";

/* -------------------------------------------------------------------------- */
/* Costanti da verificare sulla scheda                                        */
/* -------------------------------------------------------------------------- */

/**
 * Ordine dei colori.
 *
 * Se il rosso e il blu risultano scambiati, questa e' la riga da cambiare.
 */
#define LCD_RGB_ORDER   LCD_RGB_ELEMENT_ORDER_RGB

/**
 * Ordine dei byte dentro ogni pixel.
 *
 * Il controller ST7789 sa leggere i pixel come li tiene in memoria un processore
 * little-endian. Chiedendo questo, il pannello fa da solo lo scambio che
 * altrimenti andrebbe fatto a mano su ogni byte prima di trasmettere.
 *
 * Se sullo schermo compaiono colori completamente sballati, il pannello ignora
 * questa impostazione: mettere LCD_RGB_DATA_ENDIAN_BIG e scambiare i byte in
 * display_flush_chunk().
 */
#define LCD_ENDIAN      LCD_RGB_DATA_ENDIAN_LITTLE

/**
 * Orientamento.
 *
 * Il pannello esce dalla fabbrica in verticale, 172 in larghezza e 320 in
 * altezza. Lasciando tutto a falso non serve nessuna rotazione e il margine
 * laterale di 34 pixel cade gia' dalla parte giusta.
 *
 * Se l'immagine appare capovolta, specchiata o girata di lato, si agisce qui.
 */
#define LCD_SWAP_XY     false
#define LCD_MIRROR_X    false
#define LCD_MIRROR_Y    false

/**
 * Inversione dei colori.
 *
 * Questo e' un pannello a cristalli liquidi con la polarizzazione invertita
 * rispetto agli ST7789 normali: senza questa riga si vedrebbe il negativo
 * dell'immagine. Non e' una preferenza estetica, e' un requisito del pannello.
 */
#define LCD_INVERT      true

/* -------------------------------------------------------------------------- */
/* Stato interno                                                              */
/* -------------------------------------------------------------------------- */

/** Spazio per il buffer di servizio. Circa sedici kilobyte. */
#define DISPLAY_STAGING_BYTES (16 * 1024)

/** L'immagine completa, un pixel per punto. Finisce in .bss, non sullo stack. */
static uint16_t s_pixels[LCD_H_RES * LCD_V_RES];

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t                 *s_staging;
static bool                      s_ready;

/* -------------------------------------------------------------------------- */
/* Interrogazioni                                                             */
/* -------------------------------------------------------------------------- */

bool display_ready(void)
{
    return s_ready;
}

int display_width(void)
{
    return LCD_H_RES;
}

int display_height(void)
{
    return LCD_V_RES;
}

uint16_t *display_pixels(void)
{
    return s_pixels;
}

/* -------------------------------------------------------------------------- */
/* Retroilluminazione                                                         */
/* -------------------------------------------------------------------------- */

/*
 * La luminosita' si regola con un segnale a larghezza di impulso. Non si arriva
 * mai al massimo: il produttore avverte che a piena potenza, dopo molte ore, il
 * pannello trattiene una traccia dell'immagine.
 */

static void backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)LCD_BL_LEDC_RES_BITS,
        .timer_num       = (ledc_timer_t)LCD_BL_LEDC_TIMER,
        .freq_hz         = LCD_BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t channel = {
        .gpio_num   = LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = (ledc_channel_t)LCD_BL_LEDC_CHANNEL,
        .timer_sel  = (ledc_timer_t)LCD_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void display_set_backlight(int permille)
{
    if (permille < 0) {
        permille = 0;
    }
    if (permille > LCD_BL_MAX_PERMILLE) {
        permille = LCD_BL_MAX_PERMILLE;
    }

    const uint32_t full_scale = (1u << LCD_BL_LEDC_RES_BITS) - 1u;
    const uint32_t duty = (uint32_t)permille * full_scale / 1000u;

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)LCD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)LCD_BL_LEDC_CHANNEL));
}

/* -------------------------------------------------------------------------- */
/* Bus e controller                                                           */
/* -------------------------------------------------------------------------- */

static esp_err_t bus_and_panel_init(void)
{
    const spi_bus_config_t bus = {
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_MAX_TRANSFER,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "bus SPI non disponibile");

    /*
     * La coda delle trasmissioni e' volutamente lunga uno.
     *
     * Con un solo posto in coda, una nuova richiesta resta bloccata finche' la
     * precedente non e' finita di uscire dal filo. Questo garantisce che il
     * buffer di servizio, che e' uno solo e viene riusato a ogni pezzo, non
     * venga riscritto mentre il pannello lo sta ancora leggendo.
     */
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num       = LCD_PIN_CS,
        .dc_gpio_num       = LCD_PIN_DC,
        .spi_mode          = 0,
        .pclk_hz           = LCD_SPI_CLOCK_HZ,
        .trans_queue_depth = 1,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                                 &io_config, &s_io),
                        TAG, "impossibile creare l'interfaccia del pannello");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ORDER,
        .data_endian    = LCD_ENDIAN,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel),
                        TAG, "controller ST7789 non riconosciuto");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset fallito");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "inizializzazione fallita");

    /* Da fare subito dopo l'inizializzazione, prima di mostrare qualcosa. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, LCD_INVERT), TAG, "inversione fallita");

    /* La memoria del controller e' piu' grande del vetro: il margine dice dove
       comincia la parte visibile. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, LCD_Y_GAP), TAG, "margine non impostato");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY), TAG, "scambio assi fallito");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y), TAG, "specchiatura fallita");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "accensione fallita");

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Avvio                                                                      */
/* -------------------------------------------------------------------------- */

esp_err_t display_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    memset(s_pixels, 0, sizeof(s_pixels));

    s_staging = (uint16_t *)heap_caps_malloc(DISPLAY_STAGING_BYTES, MALLOC_CAP_DMA);
    if (s_staging == NULL) {
        ESP_LOGE(TAG, "memoria per il buffer di servizio non disponibile");
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = bus_and_panel_init();
    if (err != ESP_OK) {
        return err;
    }

    backlight_init();

    s_ready = true;
    ESP_LOGI(TAG, "ST7789 %dx%d pronto, %d byte per l'immagine", LCD_H_RES, LCD_V_RES,
             (int)sizeof(s_pixels));
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Aggiornamento                                                              */
/* -------------------------------------------------------------------------- */

/**
 * Copia un pezzo dell'immagine nel buffer di servizio e lo manda al pannello.
 *
 * Si lavora per fasce orizzontali: una fascia e' larga quanto il rettangolo e
 * alta quanto basta per stare nel buffer di servizio. Cosi' un rettangolo
 * grande non richiede memoria aggiuntiva.
 */
static void flush_chunk(int x, int y, int w, int h)
{
    const int bytes_per_row = w * (int)sizeof(uint16_t);
    if (bytes_per_row <= 0) {
        return;
    }

    int rows_per_pass = DISPLAY_STAGING_BYTES / bytes_per_row;
    if (rows_per_pass < 1) {
        /* Piu' largo del buffer di servizio: si manda una riga per volta. */
        rows_per_pass = 1;
    }

    for (int done = 0; done < h; done += rows_per_pass) {
        int rows = h - done;
        if (rows > rows_per_pass) {
            rows = rows_per_pass;
        }

        for (int r = 0; r < rows; ++r) {
            const uint16_t *src = s_pixels + (size_t)(y + done + r) * (size_t)LCD_H_RES + (size_t)x;
            memcpy(s_staging + (size_t)r * (size_t)w, src, (size_t)bytes_per_row);
        }

        esp_lcd_panel_draw_bitmap(s_panel, x, y + done, x + w, y + done + rows, s_staging);
    }
}

void display_flush_rect(int x, int y, int w, int h)
{
    if (!s_ready || w <= 0 || h <= 0) {
        return;
    }

    /* Ritaglio sul pannello: chi chiama non deve preoccuparsi dei bordi. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) { w = LCD_H_RES - x; }
    if (y + h > LCD_V_RES) { h = LCD_V_RES - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    flush_chunk(x, y, w, h);
}

void display_flush_all(void)
{
    display_flush_rect(0, 0, LCD_H_RES, LCD_V_RES);
}

/* -------------------------------------------------------------------------- */
/* Figura di prova                                                            */
/* -------------------------------------------------------------------------- */

/*
 * Serve al primo avvio: se le barre appaiono nell'ordine giusto, con il rosso a
 * sinistra e il blu a destra, allora colori, margine e orientamento sono a
 * posto. Se appaiono capovolte o con i colori scambiati, si sa esattamente
 * quale costante cambiare.
 */
void display_test_pattern(void)
{
    if (!s_ready) {
        return;
    }

    /* Quattro barre verticali. Se appaiono in quest'ordine, da sinistra verso
       destra, allora l'orientamento e' corretto. */
    static const uint16_t bars[4] = {
        GFX_RGB(255, 0, 0),
        GFX_RGB(0, 255, 0),
        GFX_RGB(0, 0, 255),
        GFX_RGB(255, 255, 255),
    };

    for (int row = 0; row < LCD_V_RES; ++row) {
        uint16_t *dst = s_pixels + (size_t)row * (size_t)LCD_H_RES;
        for (int col = 0; col < LCD_H_RES; ++col) {
            dst[col] = bars[(col * 4) / LCD_H_RES];
        }
    }

    /* Una squadretta bianca nell'angolo in alto a sinistra: dice a colpo
       d'occhio dove comincia l'immagine, cioe' se il margine laterale di 34
       pixel e' stato applicato dalla parte giusta. */
    const uint16_t marker = GFX_RGB(255, 255, 255);
    for (int i = 0; i < 24; ++i) {
        s_pixels[(size_t)0 * LCD_H_RES + (size_t)i] = marker;
        s_pixels[(size_t)1 * LCD_H_RES + (size_t)i] = marker;
        s_pixels[(size_t)i * LCD_H_RES + 0] = marker;
        s_pixels[(size_t)i * LCD_H_RES + 1] = marker;
    }

    display_flush_all();
}
