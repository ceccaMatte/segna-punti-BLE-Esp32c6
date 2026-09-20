#include "config_runtime.h"

#include "config_store.h"
#include "wearable_protocol.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "config_runtime";

static wearable_config_t *s_config;
static config_runtime_changed_cb_t s_changed_cb;
static void *s_changed_ctx;
static SemaphoreHandle_t s_lock;

esp_err_t config_runtime_init(wearable_config_t *config,
                              config_runtime_changed_cb_t changed_cb,
                              void *ctx)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_config = config;
    s_changed_cb = changed_cb;
    s_changed_ctx = ctx;

    ESP_LOGI(TAG,
             "initialized schema=%u mappings=%u",
             (unsigned)config->schema_version,
             (unsigned)config->mapping_count);
    return ESP_OK;
}

bool config_runtime_snapshot(wearable_config_t *out)
{
    if (out == NULL || s_config == NULL || s_lock == NULL) {
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = *s_config;
    xSemaphoreGive(s_lock);
    return true;
}

esp_err_t config_runtime_apply_command(const uint8_t *command,
                                       size_t len)
{
    if (command == NULL || len == 0u ||
        s_config == NULL || s_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    wearable_config_t next;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    next = *s_config;

    if (!wearable_apply_config_command(&next, command, len)) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG,
                 "rejected config command opcode=0x%02x len=%u",
                 (unsigned)command[0],
                 (unsigned)len);
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * NVS is the source of truth. Persist first and publish the in-RAM
     * configuration only after a successful commit.
     */
    if (!config_store_save(&next)) {
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG,
                 "NVS commit failed opcode=0x%02x",
                 (unsigned)command[0]);
        return ESP_FAIL;
    }

    *s_config = next;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG,
             "config committed opcode=0x%02x mappings=%u multi=%u long=%u sequence=%u simultaneous=%u",
             (unsigned)command[0],
             (unsigned)next.mapping_count,
             (unsigned)next.multi_click_gap_ms,
             (unsigned)next.long_press_ms,
             (unsigned)next.sequence_gap_ms,
             (unsigned)next.simultaneous_window_ms);

    if (s_changed_cb != NULL) {
        s_changed_cb(&next, s_changed_ctx);
    }

    return ESP_OK;
}
