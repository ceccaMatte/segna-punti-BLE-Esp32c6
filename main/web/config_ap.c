#include "config_ap.h"

#include <stdio.h>
#include <string.h>

#include "ble_manager.h"
#include "config_runtime.h"
#include "power_manager.h"
#include "wearable_protocol.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "config_ap";

extern const unsigned char config_ap_page_html_start[]
    asm("_binary_config_ap_page_html_start");
extern const unsigned char config_ap_page_html_end[]
    asm("_binary_config_ap_page_html_end");
extern const unsigned char config_ap_app_js_start[]
    asm("_binary_config_ap_app_js_start");
extern const unsigned char config_ap_app_js_end[]
    asm("_binary_config_ap_app_js_end");

static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;

static esp_err_t send_embedded(httpd_req_t *req,
                               const char *content_type,
                               const unsigned char *start,
                               const unsigned char *end)
{
    size_t len = (size_t)(end - start);

    /*
     * ESP-IDF EMBED_TXTFILES appends a NUL terminator. That byte is useful
     * when the asset is treated as a C string, but it must not be sent as part
     * of JavaScript source: browsers can reject a script containing a raw NUL
     * before any of its initialization code runs.
     */
    while (len > 0u && start[len - 1u] == 0u) {
        len--;
    }

    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req,
                           (const char *)start,
                           (ssize_t)len);
}

static esp_err_t root_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET /");
    return send_embedded(req,
                         "text/html; charset=utf-8",
                         config_ap_page_html_start,
                         config_ap_page_html_end);
}

static esp_err_t app_js_get(httpd_req_t *req)
{
    const size_t raw_len =
        (size_t)(config_ap_app_js_end - config_ap_app_js_start);
    ESP_LOGI(TAG,
             "HTTP GET /app.js -> embedded=%u bytes",
             (unsigned)raw_len);
    return send_embedded(req,
                         "application/javascript; charset=utf-8",
                         config_ap_app_js_start,
                         config_ap_app_js_end);
}

static esp_err_t config_get(httpd_req_t *req)
{
    wearable_config_t snapshot;
    if (!config_runtime_snapshot(&snapshot)) {
        httpd_resp_send_err(req,
                            HTTPD_500_INTERNAL_SERVER_ERROR,
                            "config unavailable");
        return ESP_FAIL;
    }

    uint8_t payload[WEARABLE_CONFIG_MAX_WIRE_SIZE];
    const size_t len =
        wearable_encode_config(&snapshot,
                               payload,
                               sizeof(payload));
    if (len == 0u) {
        httpd_resp_send_err(req,
                            HTTPD_500_INTERNAL_SERVER_ERROR,
                            "encode failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "HTTP GET /api/config -> %u bytes",
             (unsigned)len);

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req,
                           (const char *)payload,
                           (ssize_t)len);
}

static esp_err_t recv_exact(httpd_req_t *req,
                            uint8_t *buffer,
                            size_t capacity,
                            size_t *out_len)
{
    if (req->content_len <= 0 ||
        (size_t)req->content_len > capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < (size_t)req->content_len) {
        const int ret =
            httpd_req_recv(req,
                           (char *)&buffer[received],
                           req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }

    *out_len = received;
    return ESP_OK;
}

static esp_err_t config_post(httpd_req_t *req)
{
    uint8_t command[32];
    size_t len = 0;

    esp_err_t err =
        recv_exact(req,
                   command,
                   sizeof(command),
                   &len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "HTTP POST /api/config invalid body len=%d",
                 req->content_len);
        httpd_resp_send_err(req,
                            HTTPD_400_BAD_REQUEST,
                            "invalid command length");
        return ESP_OK;
    }

    ESP_LOGI(TAG,
             "HTTP config command opcode=0x%02x len=%u",
             (unsigned)command[0],
             (unsigned)len);

    /*
     * Keep one physical pairing gesture at all times. The generic config
     * validator already enforces this, but the HTTP API returns a specific
     * conflict so the UI can explain the reason instead of showing a generic
     * "invalid configuration".
     */
    if (command[0] == 0x11u && len == 2u) {
        wearable_config_t snapshot;
        if (config_runtime_snapshot(&snapshot) &&
            command[1] < snapshot.mapping_count &&
            snapshot.mappings[command[1]].action ==
                WEARABLE_ACTION_ENTER_PAIRING) {
            uint8_t pairing_count = 0;
            for (uint8_t i = 0; i < snapshot.mapping_count; ++i) {
                if (snapshot.mappings[i].action ==
                    WEARABLE_ACTION_ENTER_PAIRING) {
                    pairing_count++;
                }
            }

            if (pairing_count <= 1u) {
                ESP_LOGW(TAG,
                         "reject delete mapping=%u: last pairing gesture",
                         (unsigned)command[1]);
                httpd_resp_set_status(req, "409 Conflict");
                httpd_resp_set_type(req, "text/plain; charset=utf-8");
                return httpd_resp_sendstr(
                    req,
                    "Deve esistere almeno una gesture di Pairing. "
                    "Crea prima una nuova gesture Pairing, poi elimina questa.");
            }
        }
    }

    err = config_runtime_apply_command(command, len);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req,
                            HTTPD_400_BAD_REQUEST,
                            "invalid configuration");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req,
                            HTTPD_500_INTERNAL_SERVER_ERROR,
                            "NVS commit failed");
        return ESP_OK;
    }

    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char json[256];
    const int len =
        snprintf(json,
                 sizeof(json),
                 "{\"ssid\":\"%s\","
                 "\"open\":true,"
                 "\"ip\":\"192.168.4.1\","
                 "\"bleAuthenticated\":%s,"
                 "\"batteryMv\":%u,"
                 "\"protocol\":%u}",
                 WEARABLE_CONFIG_AP_SSID,
                 ble_manager_is_authenticated() ? "true" : "false",
                 (unsigned)power_manager_battery_mv(),
                 (unsigned)WEARABLE_PROTOCOL_VERSION);

    if (len < 0 || (size_t)len >= sizeof(json)) {
        httpd_resp_send_err(req,
                            HTTPD_500_INTERNAL_SERVER_ERROR,
                            "status encode failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, len);
}

static esp_err_t not_found(httpd_req_t *req,
                           httpd_err_code_t error)
{
    (void)error;

    /*
     * Phones often probe a connectivity-check URL when joining an AP without
     * Internet. Redirect unknown GETs to the local configuration page so the
     * experience remains predictable even without a captive DNS server.
     */
    if (req->method == HTTP_GET) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", WEARABLE_CONFIG_AP_URL);
        return httpd_resp_send(req, NULL, 0);
    }

    return httpd_resp_send_err(req,
                               HTTPD_404_NOT_FOUND,
                               "not found");
}

static esp_err_t start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get,
    };
    const httpd_uri_t js = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = app_js_get,
    };
    const httpd_uri_t get_config = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get,
    };
    const httpd_uri_t post_config = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post,
    };
    const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get,
    };

    ESP_ERROR_CHECK_WITHOUT_ABORT(
        httpd_register_uri_handler(s_httpd, &root));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        httpd_register_uri_handler(s_httpd, &js));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        httpd_register_uri_handler(s_httpd, &get_config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        httpd_register_uri_handler(s_httpd, &post_config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        httpd_register_uri_handler(s_httpd, &status));

    httpd_register_err_handler(s_httpd,
                               HTTPD_404_NOT_FOUND,
                               not_found);
    return ESP_OK;
}

esp_err_t config_ap_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid,
            WEARABLE_CONFIG_AP_SSID,
            sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(WEARABLE_CONFIG_AP_SSID);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.beacon_interval = 100;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    err = start_http_server();
    if (err != ESP_OK) {
        return err;
    }

    esp_netif_ip_info_t ip = {0};
    if (esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) {
        ESP_LOGI(TAG,
                 "OPEN SoftAP ready SSID=%s IP=" IPSTR,
                 WEARABLE_CONFIG_AP_SSID,
                 IP2STR(&ip.ip));
    } else {
        ESP_LOGI(TAG,
                 "OPEN SoftAP ready SSID=%s URL=%s",
                 WEARABLE_CONFIG_AP_SSID,
                 WEARABLE_CONFIG_AP_URL);
    }

    ESP_LOGW(TAG,
             "configuration AP has NO PASSWORD by design");
    return ESP_OK;
}
