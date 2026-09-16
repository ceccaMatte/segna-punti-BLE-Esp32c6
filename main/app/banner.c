/**
 * @file banner.c
 * @brief Il cartello di avvio. Vedi banner.h per il perche'.
 *
 * SPDX-License-Identifier: MIT
 */

#include "banner.h"

#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"

#include "app.h"
#include "board.h"
#include "button.h"
#include "commissioning_manager.h"
#include "controller.h"
#include "display.h"
#include "match.h"

/* Lo stesso nome del ciclo principale: il cartello e' la prima cosa che si
   legge, e mescolare due prefissi diversi nella stessa schermata confonde. */
static const char *TAG = "segnapunti";

/** Come si chiama, per esteso, il chip che sta facendo girare tutto questo. */
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

void banner_print(void)
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
    ESP_LOGI(TAG, "  LED RGB    GPIO%d, luminosita' %d%%",
             BOARD_RGB_GPIO, CONFIG_PADEL_LED_BRIGHTNESS / 10);
    ESP_LOGI(TAG, "  pulsante   GPIO%d, attivo basso", BOARD_BUTTON_GPIO);
    ESP_LOGI(TAG, "  gesti      1 click NOI | 2 click LORO | 3 click annulla");
    ESP_LOGI(TAG, "             4 click azzera | %" PRIu32 " ms MOMENT",
             (uint32_t)BTN_MOMENT_MIN_MS);
    ESP_LOGI(TAG, "             %" PRIu32 " ms apre il commissioning",
             (uint32_t)BTN_PAIRING_HOLD_MS);
    ESP_LOGI(TAG, "  finestra   %" PRIu32 " ms per i click multipli",
             (uint32_t)BTN_MULTI_CLICK_MS);
    ESP_LOGI(TAG, "  avviso     da %d ms lo schermo invita a tenere premuto",
             CONFIG_PADEL_HOLD_HINT_MS);
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

void banner_print_radio(void)
{
    ESP_LOGI(TAG, "  Bluetooth  %s, protocollo v%d",
             commissioning_manager_device_name(), PADEL_PROTOCOL_VERSION);
    ESP_LOGI(TAG, "  associaz.  GPIO%d tenuto basso per %d ms apre la finestra",
             CONFIG_PADEL_COMMISSIONING_GPIO, CONFIG_PADEL_COMMISSIONING_HOLD_MS);
}
