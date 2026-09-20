#include "esp_err.h"
#include "wearable_app.h"

void app_main(void)
{
    ESP_ERROR_CHECK(wearable_app_start());
}
