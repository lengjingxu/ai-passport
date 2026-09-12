#include "passport_ble.h"
#include "passport_protocol.h"
#include "demo_radio.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/ble_att.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "passport_ble";

// Cindy-owned service; distinct from Nordic UART and reference firmware.
#define UUID(last) BLE_UUID128_INIT(last,0x83,0x93,0xa4,0x21,0x46,0x86,0xa1,0x9d,0x49,0xc4,0x51,0x01,0x00,0xdc,0xc1)
static const ble_uuid128_t service_uuid = UUID(0x01), rx_uuid = UUID(0x02), tx_uuid = UUID(0x03), voice_uuid = UUID(0x04);
static uint16_t tx_handle, voice_handle;
static atomic_bool voice_subscribed;
static atomic_int voice_ack_status;
static SemaphoreHandle_t voice_ack;
static uint8_t addr_type;
static atomic_int connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint connection_generation;
static atomic_bool wanted, secure, subscribed;
static atomic_int passkey = -1, failure;
static bool initialized;
static SemaphoreHandle_t stopped;
typedef struct { int count; task_item_t items[TASKS_MODEL_MAX]; } snapshot_t;
static QueueHandle_t snapshots;
static passport_decoder_t decoder;
static snapshot_t incoming;
static portMUX_TYPE action_lock = portMUX_INITIALIZER_UNLOCKED;
static char action_id[TASK_ID_LEN];
void ble_store_config_init(void);
static int advertise(void);

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr;
    if (!secure) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (arg == (void *)2) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    if (arg) {
        char id[TASK_ID_LEN];
        taskENTER_CRITICAL(&action_lock);
        memcpy(id, action_id, sizeof(id));
        taskEXIT_CRITICAL(&action_lock);
        return os_mbuf_append(ctxt->om, id, sizeof(id)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    unsigned char chunk[512];
    uint16_t n = OS_MBUF_PKTLEN(ctxt->om);
    if (n > sizeof(chunk) || ble_hs_mbuf_to_flat(ctxt->om, chunk, sizeof(chunk), NULL))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    int result = passport_decode(&decoder, chunk, n, incoming.items, &incoming.count);
    if (result < 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (result == 1) xQueueOverwrite(snapshots, &incoming);
    return 0;
}
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {{
        .uuid = &rx_uuid.u, .access_cb = access_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
    }, {
        .uuid = &tx_uuid.u, .access_cb = access_cb, .arg = (void *)1,
        .val_handle = &tx_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_NOTIFY,
    }, {
        .uuid = &voice_uuid.u, .access_cb = access_cb, .arg = (void *)2,
        .val_handle = &voice_handle, .flags = BLE_GATT_CHR_F_INDICATE,
    }, {0}},
}, {0}};

static int gap_event(struct ble_gap_event *e, void *arg)
{
    (void)arg;
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!e->connect.status) {
            connection = e->connect.conn_handle;
            ESP_LOGI(TAG, "connected handle=%d", e->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", e->connect.status);
            if (wanted) failure = advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection_generation++;
        secure = false; subscribed = false; voice_subscribed = false; passkey = -1;
        connection = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "disconnected reason=%d", e->disconnect.reason);
        decoder.used = 0;
        incoming.count = 0;
        xQueueOverwrite(snapshots, &incoming);
        if (wanted) failure = advertise();
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        if (e->notify_tx.attr_handle == voice_handle && e->notify_tx.indication && e->notify_tx.status != 0) {
            voice_ack_status = e->notify_tx.status;
            if (voice_ack) xSemaphoreGive(voice_ack);
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (e->subscribe.attr_handle == voice_handle) voice_subscribed = e->subscribe.cur_indicate;
        if (e->subscribe.attr_handle == tx_handle) subscribed = e->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        bool found = ble_gap_conn_find(e->enc_change.conn_handle, &desc) == 0;
        secure = !e->enc_change.status && found && desc.sec_state.encrypted
                 && desc.sec_state.authenticated && desc.sec_state.key_size == 16;
        passkey = -1;
        if (!secure) {
            // A bond from an earlier just-works pairing encrypts but stays
            // unauthenticated, so the central would reconnect with the same key
            // forever. Drop only that bond: a failed or timed-out encryption
            // must keep the stored key, otherwise the central keeps its copy
            // and macOS refuses to reconnect with "peer removed pairing
            // information".
            bool justworks = !e->enc_change.status && found && desc.sec_state.encrypted
                             && !desc.sec_state.authenticated;
            int rc = justworks ? ble_store_util_delete_peer(&desc.peer_id_addr) : BLE_HS_ENOENT;
            ESP_LOGW(TAG, "insecure link status=%d encrypted=%d authenticated=%d key=%u drop=%d",
                     e->enc_change.status, found && desc.sec_state.encrypted,
                     found && desc.sec_state.authenticated, found ? desc.sec_state.key_size : 0, rc);
            ble_gap_terminate(e->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
        } else {
            ESP_LOGI(TAG, "secure link established");
        }
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (e->passkey.params.action != BLE_SM_IOACT_DISP) {
            ble_gap_terminate(e->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
            break;
        }
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
        passkey = io.passkey;
        failure = ble_sm_inject_io(e->passkey.conn_handle, &io);
        break;
    }
    default: break;
    }
    return 0;
}
static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) return rc;
    struct ble_hs_adv_fields scan = {0};
    scan.name = (const uint8_t *)"Cindy Passport"; scan.name_len = 14; scan.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&scan);
    if (rc) return rc;
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    return ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}
static void sync_host(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &addr_type);
    if (!rc && wanted) rc = advertise();
    failure = rc;
}
static void reset_host(int reason)
{
    connection_generation++;
    secure = false; subscribed = false; voice_subscribed = false; passkey = -1; failure = reason;
    connection = BLE_HS_CONN_HANDLE_NONE; decoder.used = 0;
    incoming.count = 0;
    if (snapshots) xQueueOverwrite(snapshots, &incoming);
}
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(stopped);
    nimble_port_freertos_deinit();
}
esp_err_t passport_ble_start(void)
{
    // The link outlives the Tasks page: callers may start it repeatedly.
    if (initialized) return ESP_OK;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    snapshots = xQueueCreate(1, sizeof(snapshot_t));
    stopped = xSemaphoreCreateBinary();
    voice_ack = xSemaphoreCreateBinary();
    if (!snapshots || !stopped || !voice_ack) { err = ESP_ERR_NO_MEM; goto fail; }
    err = nimble_port_init();
    if (err != ESP_OK) goto fail;
    initialized = true;
    ble_svc_gap_init(); ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set("Cindy Passport") || ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) {
        nimble_port_deinit(); initialized = false; err = ESP_FAIL; goto fail;
    }
    ble_hs_cfg.sync_cb = sync_host; ble_hs_cfg.reset_cb = reset_host;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_mitm = 1; ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    decoder.used = 0; secure = false; subscribed = false; voice_subscribed = false; passkey = -1; failure = 0;
    connection = BLE_HS_CONN_HANDLE_NONE; wanted = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
fail:
    if (snapshots) vQueueDelete(snapshots);
    if (stopped) vSemaphoreDelete(stopped);
    if (voice_ack) vSemaphoreDelete(voice_ack);
    voice_ack = NULL;
    snapshots = NULL; stopped = NULL;
    return err;
}
esp_err_t passport_ble_stop(void)
{
    wanted = false;
    if (!initialized) return ESP_OK;
    ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc) return ESP_FAIL;
    xSemaphoreTake(stopped, portMAX_DELAY);
    esp_err_t err = nimble_port_deinit();
    if (err != ESP_OK) return err;
    initialized = false; secure = false;
    vSemaphoreDelete(stopped); stopped = NULL;
    vSemaphoreDelete(voice_ack); voice_ack = NULL;
    vQueueDelete(snapshots); snapshots = NULL;
    return ESP_OK;
}
bool passport_ble_snapshot(task_item_t *items, int *count)
{
    snapshot_t value;
    if (!snapshots || !xQueueReceive(snapshots, &value, 0)) return false;
    memcpy(items, value.items, sizeof(value.items)); *count = value.count;
    return true;
}
void passport_ble_status(char *out, size_t cap)
{
    int pin = passkey, err = failure;
    if (pin >= 0) snprintf(out, cap, "配对码: %06d", pin);
    else if (err) snprintf(out, cap, "BLE error: %d", err);
    else snprintf(out, cap, "%s", secure ? "Cindy 已连接" : connection != BLE_HS_CONN_HANDLE_NONE ? "请在 Mac 上完成配对" : "等待连接 Cindy");
}
esp_err_t passport_ble_open_task(const char *id)
{
    if (!secure || !subscribed) return ESP_ERR_INVALID_STATE;
    if (!id || !id[0] || strlen(id) >= sizeof(action_id)) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&action_lock);
    memset(action_id, 0, sizeof(action_id)); strcpy(action_id, id);
    taskEXIT_CRITICAL(&action_lock);
    // Notify a one-byte wakeup; the central reads the authenticated 40-byte ID.
    // This works even with the minimum ATT MTU of 23.
    uint8_t wake = 1;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&wake, 1);
    if (!om) return ESP_ERR_NO_MEM;
    return ble_gatts_notify_custom(connection, tx_handle, om) == 0 ? ESP_OK : ESP_FAIL;
}

bool passport_ble_connected(void) { return secure; }

esp_err_t passport_ble_action(passport_action_t action, const char *id, uint32_t token)
{
    int conn = connection;
    if (!secure || !subscribed) return ESP_ERR_INVALID_STATE;
    unsigned char packet[PASSPORT_ACTION_BYTES];
    if (passport_encode_action(action, id, token, packet) < 0) return ESP_ERR_INVALID_ARG;
    if (sizeof(packet) + 3 > ble_att_mtu(conn)) return ESP_ERR_INVALID_SIZE;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, sizeof(packet));
    if (!om) return ESP_ERR_NO_MEM;
    return ble_gatts_notify_custom(conn, tx_handle, om) == 0 ? ESP_OK : ESP_FAIL;
}

// Only the recording sender calls this blocking function. The NimBLE task
// signals acknowledged indications; disconnect/timeout aborts the recording.
esp_err_t passport_ble_voice_send(const void *data, size_t size)
{
    int conn = connection;
    unsigned generation = connection_generation;
    if (!secure || !voice_subscribed || !voice_ack) return ESP_ERR_INVALID_STATE;
    if (!size || size > 200 || size + 3 > ble_att_mtu(conn)) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(voice_ack, 0);
    voice_ack_status = 0;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, size);
    if (!om) return ESP_ERR_NO_MEM;
    if (ble_gatts_indicate_custom(conn, voice_handle, om)) return ESP_FAIL;
    if (!xSemaphoreTake(voice_ack, pdMS_TO_TICKS(1500))) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return ESP_ERR_TIMEOUT;
    }
    return secure && connection == conn && generation == connection_generation && voice_ack_status == BLE_HS_EDONE ? ESP_OK : ESP_FAIL;
}
