/**
 * @file main.c
 * @brief Segnapunti da padel su Waveshare ESP32-C6-LCD-1.47.
 *
 * Un solo pulsante, quello di BOOT. I gesti valgono:
 *
 *   1 click         punto a NOI
 *   2 click         punto a LORO
 *   3 click         annulla l'ultima azione
 *   4 click o piu'  nessuna azione
 *   pressione 4 s   azzera la partita e ricomincia
 *
 * Il programma fa due cose soltanto: far avanzare la partita e tenere lo
 * schermo allineato a essa. Non prende nessuna decisione sul padel: le regole
 * stanno nel motore, che non sa che esista un display, e il disegno non sa che
 * esista un pulsante. Questa separazione e' quello che permette di provare
 * tutta la logica sul computer, senza scheda collegata, con
 * ``test\run_tests.ps1``.
 *
 * Il ciclo principale gira ogni 5 millisecondi. Non e' una necessita' del
 * gioco: serve al riconoscimento dei click, che deve distinguere uno, due e tre
 * click all'interno di una finestra di quattrocento millisecondi.
 *
 * NOTA sull'alimentazione: il pulsante di BOOT e' anche il piedino che sceglie
 * la modalita' di avvio. Se la scheda viene accesa con il pulsante gia' premuto
 * parte il bootloader invece del segnapunti. Si preme dopo l'accensione.
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "button.h"
#include "controller.h"
#include "display.h"
#include "match.h"
#include "ui.h"
#include "ui_view.h"

static const char *TAG = "segnapunti";

/* -------------------------------------------------------------------------- */
/* Controlli fatti in compilazione                                            */
/* -------------------------------------------------------------------------- */

/* La piedinatura scritta in board.h e' quella di questa scheda: compilando per
   un altro chip il display non si accenderebbe, e senza accendersi il difetto
   sarebbe difficile da capire. Meglio fermarsi subito. */
#if !CONFIG_IDF_TARGET_ESP32C6
#error "Questa scheda monta un ESP32-C6. Esegui: idf.py set-target esp32c6"
#endif

/* -------------------------------------------------------------------------- */
/* Parametri                                                                  */
/* -------------------------------------------------------------------------- */

/** Periodo del ciclo principale. Vedi il commento in testa al file. */
#define POLL_INTERVAL_MS 5

/** Per quanto resta a video la figura di prova, quando e' attiva. */
#define TEST_PATTERN_SECONDS 5

/** Il pulsante porta il piedino a massa quando e' premuto. */
#define BUTTON_PRESSED_LEVEL 0

/**
 * Chi serve per primo.
 *
 * Nel padel il primo servizio si sorteggia, quindi non c'e' una risposta
 * giusta: si sceglie da menuconfig.
 */
#ifdef CONFIG_PADEL_FIRST_SERVER_LORO
#define FIRST_SERVER TEAM_THEM
#else
#define FIRST_SERVER TEAM_US
#endif

/* -------------------------------------------------------------------------- */
/* Pulsante                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * Prepara il piedino del pulsante come ingresso.
 *
 * Niente interrupt: il pulsante viene letto dal ciclo principale, che e' anche
 * l'unico posto dove si puo' stampare sul monitor seriale senza rischi.
 */
static void button_gpio_init(void)
{
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));
}

/* -------------------------------------------------------------------------- */
/* Aiutanti                                                                   */
/* -------------------------------------------------------------------------- */

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:    return "ESP32";
    case CHIP_ESP32S2:  return "ESP32-S2";
    case CHIP_ESP32S3:  return "ESP32-S3";
    case CHIP_ESP32C2:  return "ESP32-C2";
    case CHIP_ESP32C3:  return "ESP32-C3";
    case CHIP_ESP32C5:  return "ESP32-C5";
    case CHIP_ESP32C6:  return "ESP32-C6";
    case CHIP_ESP32C61: return "ESP32-C61";
    case CHIP_ESP32H2:  return "ESP32-H2";
    case CHIP_ESP32H21: return "ESP32-H21";
    case CHIP_ESP32H4:  return "ESP32-H4";
    case CHIP_ESP32P4:  return "ESP32-P4";
    default:            return "sconosciuto";
    }
}

#if CONFIG_PADEL_LOG_EVENTS || CONFIG_PADEL_DEBUG_INVARIANTS
/** Nome del gesto riconosciuto, per i log. */
static const char *event_name(btn_event_t event)
{
    switch (event) {
    case BTN_EVT_SINGLE: return "1 click -> NOI";
    case BTN_EVT_DOUBLE: return "2 click -> LORO";
    case BTN_EVT_TRIPLE: return "3 click -> annulla";
    case BTN_EVT_LONG:   return "pressione lunga -> azzera";
    default:             return "nessuno";
    }
}
#endif

/** Cosa scrivere sul monitor seriale all'avvio. */
static void print_banner(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  Segnapunti padel - %s", BOARD_NAME);
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  chip       %s rev v%u.%u, ESP-IDF %s",
             chip_model_name(chip_info.model),
             (unsigned)(chip_info.revision / 100U),
             (unsigned)(chip_info.revision % 100U),
             esp_get_idf_version());
    ESP_LOGI(TAG, "  schermo    ST7789 %dx%d, margine X %d, SPI %d MHz",
             LCD_H_RES, LCD_V_RES, LCD_X_GAP, LCD_SPI_CLOCK_HZ / 1000000);
    ESP_LOGI(TAG, "  pulsante   GPIO%d, attivo basso", BOARD_BUTTON_GPIO);
    ESP_LOGI(TAG, "  gesti      1 click NOI | 2 click LORO | 3 click annulla");
    ESP_LOGI(TAG, "             4 click niente | %" PRIu32 " ms azzera",
             (uint32_t)BTN_LONG_PRESS_MS);
    ESP_LOGI(TAG, "  finestra   %" PRIu32 " ms per i click multipli",
             (uint32_t)BTN_MULTI_CLICK_MS);
    ESP_LOGI(TAG, "  partita    al meglio di %d set, serve per primo %s",
             2 * MATCH_SETS_TO_WIN - 1,
             (FIRST_SERVER == TEAM_US) ? "NOI" : "LORO");
    ESP_LOGI(TAG, "  vincitore  %" PRIu32 " ms a video", (uint32_t)CONTROLLER_FINISHED_SCREEN_MS);
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "  Il pulsante e' anche quello di avvio: se la scheda");
    ESP_LOGI(TAG, "  viene accesa con il pulsante premuto parte il");
    ESP_LOGI(TAG, "  bootloader invece del segnapunti.");
    ESP_LOGI(TAG, "==================================================");
}

/* -------------------------------------------------------------------------- */
/* Avvio                                                                      */
/* -------------------------------------------------------------------------- */

void app_main(void)
{
    print_banner();

    button_gpio_init();

    /*
     * Se lo schermo non parte, il segnapunti non viene interrotto: si annota il
     * motivo e si prosegue.
     *
     * Sembra strano, ma l'alternativa e' peggiore. Interrompere il programma
     * manda la scheda in un ciclo di riavvii, e in quel momento il monitor
     * seriale diventa inutilizzabile proprio quando serve di piu'. Andando
     * avanti, invece, il pulsante continua a funzionare e i log raccontano cosa
     * e' successo: un display spento si diagnostica, un ciclo di riavvii no.
     * Le funzioni di disegno si accorgono da sole che il pannello non c'e' e
     * non fanno nulla.
     */
    const esp_err_t display_error = display_init();
    if (display_error != ESP_OK) {
        ESP_LOGE(TAG, "schermo non disponibile (%s): si prosegue senza",
                 esp_err_to_name(display_error));
    } else {
        display_set_backlight(CONFIG_PADEL_BACKLIGHT_PERMILLE);
    }

#if CONFIG_PADEL_DISPLAY_TEST_PATTERN
    /*
     * Modalita' diagnostica.
     *
     * La figura viene mostrata per qualche secondo e poi lasciata al gioco.
     * Serve al primo montaggio: se le quattro barre appaiono nell'ordine
     * giusto e la squadretta bianca e' nell'angolo in alto a sinistra, allora
     * colori, margine e orientamento sono a posto.
     *
     * Non basta disegnarla e proseguire: la schermata del gioco comincia
     * proprio cancellando tutto, quindi la figura sparirebbe prima di poterla
     * vedere.
     */
    display_test_pattern();
    ESP_LOGW(TAG, "figura di prova per %d secondi", TEST_PATTERN_SECONDS);
    vTaskDelay(pdMS_TO_TICKS(TEST_PATTERN_SECONDS * 1000));
#endif

    ui_init();

    controller_init(FIRST_SERVER);

    button_t button;
    button_init(&button);

    /* Primo disegno: la partita e' appena cominciata, quindi si disegna tutto
       una volta sola. Da qui in poi cambieranno solo le zone che servono. */
    ui_view_t view;
    ui_view_build(controller_state(), &view);
    ui_update(&view);

    int64_t last_us = esp_timer_get_time();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        const int64_t now_us = esp_timer_get_time();
        int64_t elapsed_us = now_us - last_us;
        last_us = now_us;

        /* Il tempo si misura davvero invece di darlo per scontato: se il ciclo
           si dilunga, i contatori interni devono saperlo, altrimenti la
           pressione lunga scatterebbe in ritardo. I limiti servono a non far
           saltare i conti in caso di fermate molto lunghe. */
        if (elapsed_us < 1000) {
            elapsed_us = 1000;
        }
        if (elapsed_us > 100000) {
            elapsed_us = 100000;
        }
        const uint32_t dt_ms = (uint32_t)(elapsed_us / 1000);

        const bool pressed = (gpio_get_level(BOARD_BUTTON_GPIO) ==
                              BUTTON_PRESSED_LEVEL);
        const btn_event_t event = button_update(&button, pressed, dt_ms);

        if (event != BTN_EVT_NONE) {
            controller_handle_event(event);

#if CONFIG_PADEL_DEBUG_INVARIANTS
            if (!match_invariants_ok(controller_state())) {
                ESP_LOGE(TAG, "stato della partita incoerente dopo: %s", event_name(event));
            }
#endif
        }

        /* Il tempo che passa nella schermata del vincitore e' compito del
           controller, non del disegno: qui ci si limita a farglielo sapere. */
        controller_tick(dt_ms);

        ui_view_build(controller_state(), &view);
        ui_update(&view);

#if CONFIG_PADEL_LOG_EVENTS
        if (event != BTN_EVT_NONE) {
            ESP_LOGI(TAG, "%s", event_name(event));
        }
#endif

#if CONFIG_PADEL_LOG_REDRAW
        if (event != BTN_EVT_NONE) {
            ESP_LOGI(TAG, "ridisegno: %d zone, %lu pixel", ui_last_dirty_count(),
                     ui_last_dirty_pixels());
        }
#endif
    }
}
