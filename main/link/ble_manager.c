#include "ble_manager.h"

#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "power_manager.h"
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

typedef struct {
    wearable_action_packet_t packet;
    uint8_t attempts;
    uint32_t last_send_ms;
} queued_action_t;

static const ble_uuid128_t SVC_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,0x6e,0x4b,0x65,0x6f,0x01,0x00,0xd1,0xc4);
static const ble_uuid128_t ACTION_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,0x6e,0x4b,0x65,0x6f,0x02,0x00,0xd1,0xc4);
static const ble_uuid128_t ACK_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,0x6e,0x4b,0x65,0x6f,0x03,0x00,0xd1,0xc4);
static const ble_uuid128_t CONTROL_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,0x6e,0x4b,0x65,0x6f,0x04,0x00,0xd1,0xc4);
static const ble_uuid128_t STATUS_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,0x6e,0x4b,0x65,0x6f,0x05,0x00,0xd1,0xc4);
static const ble_uuid128_t CONFIG_UUID =
    BLE_UUID128_INIT(0x6b,0x6d,0x79,0x61,0x6c,0x70,0x30,0xae,0x6e,0x4b,0x65,0x6f,0x06,0x00,0xd1,0xc4);

static wearable_config_t *s_config;
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

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool pairing_open(void)
{
    return s_pairing_until_ms > 0 && (int64_t)now_ms() < s_pairing_until_ms;
}

static void app_event(ble_app_event_t event)
{
    if (s_event_cb) s_event_cb(event, s_cb_ctx);
}

static void notify_status(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_status_subscribed) return;

    uint8_t data[WEARABLE_STATUS_PACKET_SIZE];
    size_t n = wearable_encode_status(s_commissioned, s_authenticated, pairing_open(),
                                      power_manager_battery_mv(), data, sizeof(data));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, n);
    if (om) ble_gatts_notify_custom(s_conn_handle, s_status_val_handle, om);
}

static int append_status(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t data[WEARABLE_STATUS_PACKET_SIZE];
    size_t n = wearable_encode_status(s_commissioned, s_authenticated, pairing_open(),
                                      power_manager_battery_mv(), data, sizeof(data));
    return os_mbuf_append(ctxt->om, data, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void send_head_locked(void)
{
    if (s_q_count == 0 || !s_authenticated || !s_action_subscribed ||
        s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    queued_action_t *item = &s_queue[s_q_head];
    uint8_t data[WEARABLE_ACTION_PACKET_SIZE];
    size_t n = wearable_encode_action(&item->packet, data, sizeof(data));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, n);
    if (om != NULL) {
        ble_gatts_notify_custom(s_conn_handle, s_action_val_handle, om);
        item->last_send_ms = now_ms();
        if (item->attempts < 255) item->attempts++;
    }
}

static void pop_head_locked(void)
{
    if (s_q_count == 0) return;
    s_q_head = (uint8_t)((s_q_head + 1u) % ACTION_QUEUE_LEN);
    s_q_count--;
}

static bool token_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (uint8_t i = 0; i < WEARABLE_TOKEN_LEN; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static int handle_control_write(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t buf[1 + WEARABLE_TOKEN_LEN];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), NULL) != 0) return BLE_ATT_ERR_UNLIKELY;

    if (buf[0] == CONTROL_CLAIM) {
        if (s_commissioned || !pairing_open()) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        memcpy(s_pair_token, &buf[1], WEARABLE_TOKEN_LEN);
        if (!config_store_save_pairing_token(s_pair_token)) return BLE_ATT_ERR_UNLIKELY;
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

    if (buf[0] == CONTROL_AUTH) {
        if (!s_commissioned || !token_equal(s_pair_token, &buf[1])) {
            s_authenticated = false;
            notify_status();
            app_event(BLE_APP_AUTH_FAILED);
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        s_authenticated = true;
        notify_status();

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_q_count) s_queue[s_q_head].attempts = 0;
        send_head_locked();
        xSemaphoreGive(s_lock);
        return 0;
    }

    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static int handle_ack_write(struct ble_gatt_access_ctxt *ctxt)
{
    if (!s_authenticated) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;

    uint8_t buf[WEARABLE_ACK_PACKET_SIZE];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < WEARABLE_ACK_PACKET_SIZE) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), NULL) != 0) return BLE_ATT_ERR_UNLIKELY;

    wearable_ack_packet_t ack;
    if (!wearable_decode_ack(buf, sizeof(buf), &ack)) return BLE_ATT_ERR_UNLIKELY;

    wearable_action_t action = WEARABLE_ACTION_POINT_A;
    bool matched = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_q_count) {
        queued_action_t *item = &s_queue[s_q_head];
        if (item->packet.session_id == ack.session_id && item->packet.sequence == ack.sequence) {
            action = item->packet.action;
            matched = true;
            pop_head_locked();
            send_head_locked();
        }
    }
    xSemaphoreGive(s_lock);

    if (matched && s_ack_cb) s_ack_cb(action, &ack, s_cb_ctx);
    return 0;
}

static int handle_config_access(struct ble_gatt_access_ctxt *ctxt)
{
    if (!s_authenticated) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t snapshot[256];
        size_t n = wearable_encode_config(s_config, snapshot, sizeof(snapshot));
        if (n == 0) return BLE_ATT_ERR_UNLIKELY;
        return os_mbuf_append(ctxt->om, snapshot, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t cmd[32];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0 || len > sizeof(cmd)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, cmd, sizeof(cmd), NULL) != 0) return BLE_ATT_ERR_UNLIKELY;

        wearable_config_t next = *s_config;
        if (!wearable_apply_config_command(&next, cmd, len)) return BLE_ATT_ERR_UNLIKELY;
        if (!config_store_save(&next)) return BLE_ATT_ERR_UNLIKELY;
        *s_config = next;
        app_event(BLE_APP_CONFIG_CHANGED);
        return 0;
    }

    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    uintptr_t which = (uintptr_t)arg;

    switch (which) {
    case 1:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) return handle_ack_write(ctxt);
        break;
    case 2:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) return handle_control_write(ctxt);
        break;
    case 3:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return append_status(ctxt);
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
            {0}
        },
    },
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    /* 31-byte advertising limit: service UUID in ADV, human-readable name in
       scan response. Keeping the service UUID in ADV is important because Web
       Bluetooth filters on it before showing the device. */
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&SVC_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;

    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&fields) != 0) return;

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
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
            s_action_subscribed = event->subscribe.cur_notify != 0;
            if (s_action_subscribed && s_authenticated) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (s_q_count) s_queue[s_q_head].attempts = 0;
                send_head_locked();
                xSemaphoreGive(s_lock);
            }
        } else if (event->subscribe.attr_handle == s_status_val_handle) {
            s_status_subscribed = event->subscribe.cur_notify != 0;
            if (s_status_subscribed) notify_status();
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
    if (ble_hs_util_ensure_addr(0) != 0) return;
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) return;
    advertise();
}

static void on_reset(int reason)
{
    (void)reason;
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
        bool open = pairing_open();
        if (open != s_last_pairing_open) {
            s_last_pairing_open = open;
            notify_status();
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_q_count && s_authenticated && s_action_subscribed &&
            s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            queued_action_t *item = &s_queue[s_q_head];
            uint32_t interval = item->attempts < RETRY_FAST_COUNT ? RETRY_FAST_MS : RETRY_SLOW_MS;
            if ((uint32_t)(now_ms() - item->last_send_ms) >= interval) {
                send_head_locked();
            }
        }
        xSemaphoreGive(s_lock);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ble_manager_start(wearable_config_t *config, ble_ack_cb_t ack_cb,
                       ble_app_event_cb_t event_cb, void *ctx)
{
    s_config = config;
    s_ack_cb = ack_cb;
    s_event_cb = event_cb;
    s_cb_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();

    s_session_id = esp_random();
    if (s_session_id == 0) s_session_id = 1;

    s_commissioned = config_store_load_pairing_token(s_pair_token);
    if (!s_commissioned) {
        s_pairing_until_ms = (int64_t)now_ms() + CONFIG_WEARABLE_PAIRING_WINDOW_MS;
    }
    s_last_pairing_open = pairing_open();

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_name, sizeof(s_name), "PLAYMAKER_%02X%02X", mac[4], mac[5]);

    if (nimble_port_init() != ESP_OK) {
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_name);

    ble_gatts_count_cfg(GATT_SERVICES);
    ble_gatts_add_svcs(GATT_SERVICES);

    nimble_port_freertos_init(host_task);
    xTaskCreate(retry_task, "ble_retry", 3072, NULL, 5, NULL);
}

bool ble_manager_send_action(wearable_action_t action)
{
    if (action >= WEARABLE_ACTION_ENTER_PAIRING || !s_authenticated) return false;

    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_q_count < ACTION_QUEUE_LEN) {
        uint8_t idx = (uint8_t)((s_q_head + s_q_count) % ACTION_QUEUE_LEN);
        s_queue[idx] = (queued_action_t){
            .packet = {
                .session_id = s_session_id,
                .sequence = s_next_sequence++,
                .action = action,
            },
            .attempts = 0,
            .last_send_ms = 0,
        };
        s_q_count++;
        ok = true;
        if (s_q_count == 1u) send_head_locked();
    }
    xSemaphoreGive(s_lock);
    return ok;
}

void ble_manager_enter_pairing(void)
{
    config_store_clear_pairing_token();
    memset(s_pair_token, 0, sizeof(s_pair_token));
    s_commissioned = false;
    s_authenticated = false;
    s_pairing_until_ms = (int64_t)now_ms() + CONFIG_WEARABLE_PAIRING_WINDOW_MS;
    s_last_pairing_open = true;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_q_head = 0;
    s_q_count = 0;
    xSemaphoreGive(s_lock);

    notify_status();

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

bool ble_manager_is_authenticated(void)
{
    return s_authenticated;
}
