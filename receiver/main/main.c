#include "receiver.h"
#include "wifi_network.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

void app_main(void)
{
    /* USB is a machine protocol; no application/Wi-Fi log may enter that stream. */
    esp_log_level_set("*",ESP_LOG_NONE);
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES||err==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());err=nvs_flash_init();}
    ESP_ERROR_CHECK(err);
    uint8_t mode=0;nvs_handle_t h;
    if(nvs_open("receiver",NVS_READONLY,&h)==ESP_OK){nvs_get_u8(h,"lan",&mode);nvs_close(h);}
    const wifi_network_options_t network={.ap_prefix="PLAYMAKER_RX_",.ap_ip="192.168.8.1",.station_enabled=mode==1};
    ESP_ERROR_CHECK(wifi_network_start(&network));
    ESP_ERROR_CHECK(receiver_start(mode==1));
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.max_uri_handlers=10;cfg.stack_size=8192;cfg.lru_purge_enable=true;
    httpd_handle_t server;ESP_ERROR_CHECK(httpd_start(&server,&cfg));
    ESP_ERROR_CHECK(wifi_network_register(server));
    ESP_ERROR_CHECK(receiver_register(server));
}
