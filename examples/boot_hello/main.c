/*
 * Waveshare ESP32-C6-LCD-1.47 - BOOT button "Hello World"
 *
 * QUESTA E' UNA COPIA DI RIFERIMENTO, NON ENTRA NELLA COMPILAZIONE.
 *
 * Era il primo programma scritto per questa scheda, quando serviva solo a
 * verificare che la scheda rispondesse: premevi BOOT e sul monitor seriale
 * compariva "Hello World". Ha fatto il suo lavoro e ora il posto principale e'
 * occupato dal segnapunti padel, che come verifica dell'hardware offre qualcosa
 * di meglio: la figura di prova sul display, attivabile da
 * "menuconfig" -> Segnapunti padel -> Mostra la figura di prova all'avvio.
 *
 * Il file resta qui perche' contiene, documentate, due cose che servono ancora:
 * la piedinatura del pulsante e la catena di stampa sul monitor seriale via USB
 * Serial/JTAG, che su questa scheda e' l'unica possibile: non c'e' nessun
 * convertitore USB-seriale, il connettore USB va dritto nel chip.
 *
 * Per rimetterlo in funzione come programma principale basta copiarlo sopra
 * main/main.c e rimettere le opzioni "Board configuration" nel Kconfig.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"

/* --- Board selection ------------------------------------------------------ */

/*
 * The selected board and the build target must agree: the BOOT button sits on
 * a different GPIO on each chip, so a mismatch would silently read the wrong
 * pin (on the ESP32-C5, GPIO9 is not broken out at all).
 */
#if CONFIG_BOARD_ESP32_C6_LCD_147 && !CONFIG_IDF_TARGET_ESP32C6
#error "Board 'ESP32-C6-LCD-1.47' requires target esp32c6. Run: idf.py set-target esp32c6"
#endif

#if CONFIG_BOARD_ESP32_C5_LCD_147 && !CONFIG_IDF_TARGET_ESP32C5
#error "Board 'ESP32-C5-LCD-1.47' requires target esp32c5. Run: idf.py set-target esp32c5"
#endif

#if CONFIG_BOARD_ESP32_C6_LCD_147
#define BOARD_NAME "Waveshare ESP32-C6-LCD-1.47"
#define BOARD_STRAP "GPIO8 must stay high; GPIO9 low at reset = download mode"
#elif CONFIG_BOARD_ESP32_C5_LCD_147
#define BOARD_NAME "Waveshare ESP32-C5-LCD-1.47"
#define BOARD_STRAP "GPIO27 must stay high; GPIO28 low at reset = download mode"
#else
#error "No board selected. Enable one in 'menuconfig' -> Board configuration."
#endif

/* --- Configuration (menuconfig -> Board configuration) -------------------- */

/** Onboard BOOT button of the selected board. */
#define BOOT_BUTTON_GPIO        ((gpio_num_t)CONFIG_BOOT_BUTTON_GPIO)

/** The button shorts the pin to GND while it is held down. */
#define BUTTON_PRESSED_LEVEL    (0)

/** Poll period of the debounce state machine, in milliseconds. */
#define POLL_INTERVAL_MS        (CONFIG_POLL_INTERVAL_MS)

/** Identical consecutive samples required before a new level is accepted. */
#define DEBOUNCE_SAMPLES        (CONFIG_DEBOUNCE_SAMPLES)

/* --- Helpers -------------------------------------------------------------- */

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:        return "ESP32";
    case CHIP_ESP32S2:      return "ESP32-S2";
    case CHIP_ESP32S3:      return "ESP32-S3";
    case CHIP_ESP32C2:      return "ESP32-C2";
    case CHIP_ESP32C3:      return "ESP32-C3";
    case CHIP_ESP32C5:      return "ESP32-C5";
    case CHIP_ESP32C6:      return "ESP32-C6";
    case CHIP_ESP32C61:     return "ESP32-C61";
    case CHIP_ESP32H2:      return "ESP32-H2";
    case CHIP_ESP32H21:     return "ESP32-H21";
    case CHIP_ESP32H4:      return "ESP32-H4";
    case CHIP_ESP32P4:      return "ESP32-P4";
    default:                return "unknown";
    }
}

/**
 * Configure the BOOT button as a plain input.
 *
 * GPIO_INTR_DISABLE keeps this simple: the button is polled from a normal task
 * context, which is the only place where printf() may be called safely.
 */
static void boot_button_init(void)
{
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));
}

static void print_banner(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("\n");
    printf("==================================================\n");
    printf("  %s\n", BOARD_NAME);
    printf("  BOOT button demo\n");
    printf("==================================================\n");
    printf("  SoC      : %s, %u core(s), silicon rev v%u.%u\n",
           chip_model_name(chip_info.model),
           (unsigned int)chip_info.cores,
           (unsigned int)(chip_info.revision / 100U),
           (unsigned int)(chip_info.revision % 100U));
    printf("  ESP-IDF  : %s\n", esp_get_idf_version());
    printf("  Console  : USB Serial/JTAG\n");
    printf("  BOOT     : GPIO%d (active low, %d ms debounce)\n",
           (int)BOOT_BUTTON_GPIO,
           (int)(POLL_INTERVAL_MS * DEBOUNCE_SAMPLES));
    printf("--------------------------------------------------\n");
    printf("  Press the BOOT button to print \"Hello World\".\n");
    printf("  NOTE: holding BOOT while resetting enters\n");
    printf("        download mode instead of starting the app.\n");
    printf("        %s\n", BOARD_STRAP);
    printf("==================================================\n");
    printf("\n");
}

/**
 * Poll the button and print once per press.
 *
 * A new level only becomes "stable" after DEBOUNCE_SAMPLES identical readings,
 * so one physical press produces exactly one falling edge.
 */
static void watch_boot_button(void)
{
    int stable_level = gpio_get_level(BOOT_BUTTON_GPIO);
    int candidate_level = stable_level;
    int candidate_count = 0;
    uint32_t press_count = 0;

    while (true) {
        const int level = gpio_get_level(BOOT_BUTTON_GPIO);

        if (level == candidate_level) {
            if (candidate_count < DEBOUNCE_SAMPLES) {
                candidate_count++;
            }
        } else {
            candidate_level = level;
            candidate_count = 1;
        }

        if (candidate_count >= DEBOUNCE_SAMPLES && candidate_level != stable_level) {
            stable_level = candidate_level;

            if (stable_level == BUTTON_PRESSED_LEVEL) {
                press_count++;
#if CONFIG_PRINT_PRESS_COUNTER
                printf("Hello World (#%u)\n", (unsigned int)press_count);
#else
                printf("Hello World\n");
#endif
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

/* --- Entry point ---------------------------------------------------------- */

void app_main(void)
{
    boot_button_init();
    print_banner();

    /* Never returns. */
    watch_boot_button();
}
