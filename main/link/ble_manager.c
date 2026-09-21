#include "ble_manager.h"

#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "config_runtime.h"
#include "power_manager.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "sdkconfig.h"

#define ACTION_QUEUE_LEN 8u
#define CONTROL_CLAIM 0x01u
#define CONTROL_AUTH  0x02u

#define RETRY_FAST_MS 400u
#define RETRY_SLOW_MS 5000u
#define RETRY_FAST_COUNT 5u
#define MAX_SEND_ATTEMPTS 10u

static const char *TAG = "ble_manager";

typedef struct {
    wearable_action_packet_t packet;
    uint8_t attempts;
    uint32_t last_send_ms;
} queued_action_t;

/*
 * UUID byte order is reversed for NimBLE's BLE_UUID128_INIT representation.
 * Human-readable UUIDs remain defined in wearable_protocol.h and in the web
 * client. Keep both representations synchronized when the protocol changes.
 */
static const ble_uuid128_t SVC_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,
                     0x6e,0x4b,0x65,0x6f,0x01,0x00,0xd1,0xc4);
static const ble_uuid128_t ACTION_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,
                     0x6e,0x4b,0x65,0x6f,0x02,0x00,0xd1,0xc4);
static const ble_uuid128_t ACK_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,
                     0x6e,0x4b,0x65,0x6f,0x03,0x00,0xd1,0xc4);
static const ble_uuid128_t CONTROL_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,
                     0x6e,0x4b,0x65,0x6f,0x04,0x00,0xd1,0xc4);
static const ble_uuid128_t STATUS_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,
                     0x6e,0x4b,0x65,0x6f,0x05,0x00,0xd1,0xc4);
static const ble_uuid128_t CONFIG_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,
                     0x6e,0x4b,0x65,0x6f,0x06,0x00,0xd1,0xc4);

static ble_ack_cb_t s_ack_cb;
static ble_app_event_cb_t s_event_cb;
static void *s_cb_ctx;

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_action_val_handle;
static uint16_t s_status_val_handle;

static bool s_action_subscribed;
static bool s_status_subscribed;
static bool s_authenticated;
static bool s_commissioned;

static uint8_t s_pair_token[WEARABLE_TOKEN_LEN];
static int64_t s_pairing_until_ms;
static bool s_last_pairing_open;
static uint8_t s_own_addr_type;
static char s_name[24];

static queued_action_t s_queue[ACTION_QUEUE_LEN];
static uint8_t s_q_head;
static uint8_t s_q_count;
static uint32_t s_session_id;
static uint32_t s_next_sequence = 1;
static SemaphoreHandle_t s_lock;

static uint32_t now_ms32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int64_t now_ms64(void)
{
    return esp_timer_get_time() / 1000;
}

static bool pairing_open(void)
{
    return s_pairing_until_ms > 0 &&
           now_ms64() < s_pairing_until_ms;
}

static void app_event(ble_app_event_t event)
{
    if (s_event_cb != NULL) {
        s_event_cb(event, s_cb_ctx);
    }
}

static void queue_pop_locked(void)
{
    if (s_q_count == 0u) {
        return;
    }

    s_q_head = (uint8_t)((s_q_head + 1u) % ACTION_QUEUE_LEN);
    s_q_count--;
}

static bool token_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;

    for (uint8_t i = 0; i < WEARABLE_TOKEN_LEN; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0u;
}

static bool token_nonzero(const uint8_t *token)
{
    uint8_t any = 0;

    for (uint8_t i = 0; i < WEARABLE_TOKEN_LEN; ++i) {
        any |= token[i];
    }

    return any != 0u;
}

static void notify_status(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        !s_status_subscribed) {
        return;
    }

    uint8_t data[WEARABLE_STATUS_PACKET_SIZE];
    const size_t size =
        wearable_encode_status(s_commissioned,
                               s_authenticated,
                               pairing_open(),
                               power_manager_battery_mv(),
                               data,
                               sizeof(data));
    if (size == 0u) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, size);
    if (om == NULL) {
        return;
    }

    const int rc =
        ble_gatts_notify_custom(s_conn_handle,
                                s_status_val_handle,
                                om);
    if (rc != 0) {
        ESP_LOGW(TAG, "status notify failed: %d", rc);
    }
}

static int append_status(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t data[WEARABLE_STATUS_PACKET_SIZE];
    const size_t size =
        wearable_encode_status(s_commissioned,
                               s_authenticated,
                               pairing_open(),
                               power_manager_battery_mv(),
                               data,
                               sizeof(data));

    if (size == 0u) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return os_mbuf_append(ctxt->om, data, size) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static bool send_head_locked(void)
{
    if (s_q_count == 0u ||
        !s_authenticated ||
        !s_action_subscribed ||
        s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }

    queued_action_t *item = &s_queue[s_q_head];

    uint8_t data[WEARABLE_ACTION_PACKET_SIZE];
    const size_t size =
        wearable_encode_action(&item->packet,
                               data,
                               sizeof(data));
    if (size == 0u) {
        return false;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, size);
    if (om == NULL) {
        return false;
    }

    const int rc =
        ble_gatts_notify_custom(s_conn_handle,
                                s_action_val_handle,
                                om);

    /*
     * A failed local notify is still a send attempt for backoff purposes.
     * Otherwise an ENOMEM / transient host error would be retried every
     * 100 ms by retry_task and could hammer the BLE host indefinitely.
     */
    item->last_send_ms = now_ms32();
    if (item->attempts < UINT8_MAX) {
        item->attempts++;
    }

    if (rc != 0) {
        ESP_LOGW(TAG,
                 "action notify failed seq=%lu attempt=%u rc=%d",
                 (unsigned long)item->packet.sequence,
                 (unsigned)item->attempts,
                 rc);
        return false;
    }

    ESP_LOGD(TAG,
             "action sent seq=%lu attempt=%u",
             (unsigned long)item->packet.sequence,
             (unsigned)item->attempts);
    return true;
}

static int handle_control_write(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t payload[1 + WEARABLE_TOKEN_LEN];
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    if (len != sizeof(payload)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om,
                            payload,
                            sizeof(payload),
                            &copied) != 0 ||
        copied != len) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint8_t opcode = payload[0];
    const uint8_t *token = &payload[1];

    if (!token_nonzero(token)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (opcode == CONTROL_CLAIM) {
        if (s_commissioned || !pairing_open()) {
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }

        memcpy(s_pair_token, token, WEARABLE_TOKEN_LEN);

        if (!config_store_save_pairing_token(s_pair_token)) {
            memset(s_pair_token, 0, sizeof(s_pair_token));
            return BLE_ATT_ERR_UNLIKELY;
        }

        s_commissioned = true;
        s_authenticated = true;
        s_pairing_until_ms = 0;

        notify_status();
        app_event(BLE_APP_PAIRING_SUCCESS);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        send_head_locked();
        xSemaphoreGive(s_lock);
        return 0;
    }

    if (opcode == CONTROL_AUTH) {
        if (!s_commissioned ||
            !token_equal(s_pair_token, token)) {
            s_authenticated = false;
            notify_status();
            app_event(BLE_APP_AUTH_FAILED);
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }

        s_authenticated = true;
        notify_status();

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_q_count != 0u) {
            s_queue[s_q_head].attempts = 0;
            s_queue[s_q_head].last_send_ms = 0;
        }
        send_head_locked();
        xSemaphoreGive(s_lock);
        return 0;
    }

    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static int handle_ack_write(struct ble_gatt_access_ctxt *ctxt)
{
    if (!s_authenticated) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }

    uint8_t payload[WEARABLE_ACK_PACKET_SIZE];
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    if (len != WEARABLE_ACK_PACKET_SIZE) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om,
                            payload,
                            sizeof(payload),
                            &copied) != 0 ||
        copied != len) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    wearable_ack_packet_t ack;
    if (!wearable_decode_ack(payload, sizeof(payload), &ack)) {
        ESP_LOGW(TAG, "invalid ACK payload");
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG,
             "ACK rx session=%lu seq=%lu status=%u flags=0x%02x rev=%u score=%u-%u games=%u-%u sets=%u-%u",
             (unsigned long)ack.session_id,
             (unsigned long)ack.sequence,
             (unsigned)ack.status,
             (unsigned)ack.transition_flags,
             (unsigned)ack.state.revision,
             (unsigned)ack.state.points_a,
             (unsigned)ack.state.points_b,
             (unsigned)ack.state.games_a,
             (unsigned)ack.state.games_b,
             (unsigned)ack.state.sets_a,
             (unsigned)ack.state.sets_b);

    wearable_action_t action = WEARABLE_ACTION_POINT_A;
    bool terminal = false;
    bool matched = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_q_count != 0u) {
        queued_action_t *item = &s_queue[s_q_head];

        if (item->packet.session_id == ack.session_id &&
            item->packet.sequence == ack.sequence) {
            matched = true;
            action = item->packet.action;

            if (ack.status == WEARABLE_ACK_TEMPORARY_ERROR) {
                /*
                 * Keep the same item and the same idempotency key. It will be
                 * retried by retry_task; never turn a temporary backend issue
                 * into a second score action with a new sequence number.
                 */
                item->last_send_ms = now_ms32();
            } else {
                terminal = true;
                queue_pop_locked();
                send_head_locked();
            }
        }
    }

    xSemaphoreGive(s_lock);

    if (!matched) {
        ESP_LOGD(TAG,
                 "stale/unmatched ACK session=%lu seq=%lu",
                 (unsigned long)ack.session_id,
                 (unsigned long)ack.sequence);
        return 0;
    }

    if (terminal && s_ack_cb != NULL) {
        s_ack_cb(action, &ack, s_cb_ctx);
    }

    return 0;
}

static int handle_config_access(struct ble_gatt_access_ctxt *ctxt)
{
    if (!s_authenticated) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        wearable_config_t config_snapshot;
        if (!config_runtime_snapshot(&config_snapshot)) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        uint8_t snapshot[WEARABLE_CONFIG_MAX_WIRE_SIZE];
        const size_t size =
            wearable_encode_config(&config_snapshot,
                                   snapshot,
                                   sizeof(snapshot));
        if (size == 0u) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        return os_mbuf_append(ctxt->om, snapshot, size) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t command[32];
        const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

        if (len == 0u || len > sizeof(command)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint16_t copied = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om,
                                command,
                                sizeof(command),
                                &copied) != 0 ||
            copied != len) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        const esp_err_t err =
            config_runtime_apply_command(command, len);
        if (err == ESP_ERR_INVALID_ARG) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (err != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        ESP_LOGI(TAG,
                 "config command committed via BLE opcode=0x%02x",
                 (unsigned)command[0]);
        app_event(BLE_APP_CONFIG_CHANGED);
        return 0;
    }

    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static int gatt_access(uint16_t conn_handle,
                       uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt,
                       void *arg)
{
    (void)conn_handle;
    (void)attr_handle;

    const uintptr_t which = (uintptr_t)arg;

    switch (which) {
    case 1:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return handle_ack_write(ctxt);
        }
        break;

    case 2:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return handle_control_write(ctxt);
        }
        break;

    case 3:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_status(ctxt);
        }
        break;

    case 4:
        return handle_config_access(ctxt);

    default:
        break;
    }

    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static const struct ble_gatt_svc_def GATT_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &ACTION_UUID.u,
                .access_cb = gatt_access,
                .arg = (void *)0,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_action_val_handle,
            },
            {
                .uuid = &ACK_UUID.u,
                .access_cb = gatt_access,
                .arg = (void *)1,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &CONTROL_UUID.u,
                .access_cb = gatt_access,
                .arg = (void *)2,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &STATUS_UUID.u,
                .access_cb = gatt_access,
                .arg = (void *)3,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_val_handle,
            },
            {
                .uuid = &CONFIG_UUID.u,
                .access_cb = gatt_access,
                .arg = (void *)4,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&SVC_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields failed: %d", rc);
        return;
    }

    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan response failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &params,
                           gap_event,
                           NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising failed: %d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_authenticated = false;
            s_action_subscribed = false;
            s_status_subscribed = false;
            app_event(BLE_APP_CONNECTED);
        } else {
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_authenticated = false;
        s_action_subscribed = false;
        s_status_subscribed = false;
        app_event(BLE_APP_DISCONNECTED);
        advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_action_val_handle) {
            s_action_subscribed =
                event->subscribe.cur_notify != 0;

            if (s_action_subscribed &&
                s_authenticated) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (s_q_count != 0u) {
                    s_queue[s_q_head].attempts = 0;
                    s_queue[s_q_head].last_send_ms = 0;
                }
                send_head_locked();
                xSemaphoreGive(s_lock);
            }
        } else if (event->subscribe.attr_handle ==
                   s_status_val_handle) {
            s_status_subscribed =
                event->subscribe.cur_notify != 0;

            if (s_status_subscribed) {
                notify_status();
            }
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    default:
        return 0;
    }
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGE(TAG, "BLE address unavailable");
        return;
    }

    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(TAG, "BLE address type unavailable");
        return;
    }

    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static void host_task(void *param)
{
    (void)param;

    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void retry_task(void *arg)
{
    (void)arg;

    for (;;) {
        const bool open = pairing_open();
        if (open != s_last_pairing_open) {
            s_last_pairing_open = open;
            notify_status();
        }

        bool action_failed = false;

        xSemaphoreTake(s_lock, portMAX_DELAY);

        if (s_q_count != 0u &&
            s_authenticated &&
            s_action_subscribed &&
            s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            queued_action_t *item = &s_queue[s_q_head];

            const uint32_t interval =
                item->attempts < RETRY_FAST_COUNT
                    ? RETRY_FAST_MS
                    : RETRY_SLOW_MS;

            if (item->attempts >= MAX_SEND_ATTEMPTS &&
                (uint32_t)(now_ms32() - item->last_send_ms) >= interval) {
                ESP_LOGW(TAG,
                         "dropping unacknowledged action seq=%lu after %u attempts",
                         (unsigned long)item->packet.sequence,
                         (unsigned)item->attempts);
                queue_pop_locked();
                send_head_locked();
                action_failed = true;
            } else if ((uint32_t)(now_ms32() -
                                  item->last_send_ms) >= interval) {
                send_head_locked();
            }
        }

        xSemaphoreGive(s_lock);

        if (action_failed) {
            app_event(BLE_APP_ACTION_FAILED);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t ble_manager_start(wearable_config_t *config,
                            ble_ack_cb_t ack_cb,
                            ble_app_event_cb_t event_cb,
                            void *ctx)
{
    if (config == NULL || ack_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ack_cb = ack_cb;
    s_event_cb = event_cb;
    s_cb_ctx = ctx;

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_session_id = esp_random();
    if (s_session_id == 0u) {
        s_session_id = 1u;
    }

    s_commissioned =
        config_store_load_pairing_token(s_pair_token);

    if (!s_commissioned) {
        s_pairing_until_ms =
            now_ms64() +
            CONFIG_WEARABLE_PAIRING_WINDOW_MS;
    }
    s_last_pairing_open = pairing_open();

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BT);
    if (err != ESP_OK) {
        return err;
    }

    snprintf(s_name,
             sizeof(s_name),
             "PLAYMAKER_%02X%02X",
             mac[4],
             mac[5]);

    err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_svc_gap_device_name_set(s_name);
    if (rc != 0) {
        return ESP_FAIL;
    }

    rc = ble_gatts_count_cfg(GATT_SERVICES);
    if (rc != 0) {
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(GATT_SERVICES);
    if (rc != 0) {
        return ESP_FAIL;
    }

    nimble_port_freertos_init(host_task);

    if (xTaskCreate(retry_task,
                    "ble_retry",
                    3072,
                    NULL,
                    5,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "BLE ready as %s, commissioned=%d",
             s_name,
             (int)s_commissioned);
    return ESP_OK;
}

bool ble_manager_send_action(wearable_action_t action)
{
    if (action > WEARABLE_ACTION_VAR ||
        !s_authenticated ||
        s_lock == NULL) {
        return false;
    }

    bool queued = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_q_count < ACTION_QUEUE_LEN) {
        const uint8_t index =
            (uint8_t)((s_q_head + s_q_count) %
                      ACTION_QUEUE_LEN);

        s_queue[index] = (queued_action_t){
            .packet = {
                .session_id = s_session_id,
                .sequence = s_next_sequence++,
                .action = action,
            },
            .attempts = 0,
            .last_send_ms = 0,
        };

        s_q_count++;
        queued = true;

        if (s_q_count == 1u) {
            send_head_locked();
        }
    }

    xSemaphoreGive(s_lock);

    return queued;
}

bool ble_manager_enter_pairing(void)
{
    if (s_lock == NULL) {
        return false;
    }

    /*
     * Persistence is the source of truth. If NVS cannot forget the old token,
     * do not pretend that the device entered pairing only in RAM.
     */
    if (!config_store_clear_pairing_token()) {
        return false;
    }

    memset(s_pair_token, 0, sizeof(s_pair_token));
    s_commissioned = false;
    s_authenticated = false;
    s_pairing_until_ms =
        now_ms64() +
        CONFIG_WEARABLE_PAIRING_WINDOW_MS;
    s_last_pairing_open = true;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_q_head = 0;
    s_q_count = 0;
    xSemaphoreGive(s_lock);

    /*
     * Notify the old client before disconnecting. The configuration page uses
     * this notification to stop auto-reconnect, otherwise it could instantly
     * reclaim the wearable and prevent a new phone/browser from pairing.
     */
    notify_status();

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle,
                          BLE_ERR_REM_USER_CONN_TERM);
    }

    return true;
}

bool ble_manager_is_authenticated(void)
{
    return s_authenticated;
}
