idf_component_register(
    SRCS "main.c" "receiver.c" "usb_protocol.c"
        "../../main/link/wifi_network.c"
        "../../main/link/wearable_protocol.c"
        "../../main/core/config_model.c"
        "../../shared/discovery/playmaker_discovery.c"
    INCLUDE_DIRS "." "../../main/link" "../../main/core" "../../shared/discovery"
    REQUIRES nvs_flash esp_wifi esp_http_server esp_http_client esp_event esp_netif
        esp_timer esp_system esp_driver_usb_serial_jtag espressif__cjson
    EMBED_TXTFILES "web/index.html" "web/app.js" "../../web-wearable/brand.css"
)
