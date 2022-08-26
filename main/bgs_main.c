#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <mdns.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <string.h>

#include "esp_wifi.h"


#include "bgs_main.h"
#include "ixe_blufi.h"
#include "ixe_params.h"
#include "bgs_mqtt.h"
#include "bgs_ble.h"
#include "bgs_ble_utils.h"
#include "bgs_socket_util.h"

#include "ixe_ble_ota.h"


extern   IxeBle			   x_ble;
extern   IxeWifi		   x_wifi;
extern   IxeData		   x_datas;

extern ble_device_t *devices_list;


#define MAX_TOPIC_LEN 256
static const char *TAG = "BGS_NAIN";

typedef struct {
    mac_addr_t mac;
    ble_uuid_t service;
    ble_uuid_t characteristic;
} mqtt_ctx_t;


char *ota_err_to_str(ota_err_t err)
{
    switch (err)
    {
    case OTA_ERR_SUCCESS: return "Success";
    case OTA_ERR_NO_CHANGE: return "No change";
    case OTA_ERR_IN_PROGRESS: return "In progress";
    case OTA_ERR_FAILED_DOWNLOAD: return "Failed downloading file";
    case OTA_ERR_FAILED_BEGIN: return "Failed initializing OTA process";
    case OTA_ERR_FAILED_WRITE: return "Failed writing data";
    case OTA_ERR_FAILED_END: return "Failed finalizing OTA process";
    }

    return "Invalid OTA error";
}


/* Bookkeeping functions */
static void uptime_publish(void)
{
    char topic[MAX_TOPIC_LEN];
    char buf[16];

    /* Only publish uptime when connected, we don't want it to be queued */
    if (!mqtt_is_connected())
        return;
   #if 0
    /* Uptime (in seconds) */
    sprintf(buf, "%lld", esp_timer_get_time() / 1000 / 1000);
    snprintf(topic, MAX_TOPIC_LEN, "%s/Uptime", device_name_get());
    mqtt_publish(topic, (uint8_t *)buf, strlen(buf), config_mqtt_qos_get(),
        config_mqtt_retained_get());

    /* Free memory (in bytes) */
    sprintf(buf, "%u", esp_get_free_heap_size());
    snprintf(topic, MAX_TOPIC_LEN, "%s/FreeMemory", device_name_get());
    mqtt_publish(topic, (uint8_t *)buf, strlen(buf), config_mqtt_qos_get(),
        config_mqtt_retained_get());
	#endif
}

/* OTA functions */
void ota_on_completed(ota_type_t type, ota_err_t err)
{
    ESP_LOGI(TAG, "Update completed: %s", ota_err_to_str(err));

    /* All done, restart */
    if (err == OTA_ERR_SUCCESS)
        abort();
    else{
      //ble_scan_start();
    }
}

static void _ota_on_completed(ota_type_t type, ota_err_t err);

static void ota_on_mqtt(const char *topic, const uint8_t *payload, size_t len,
    void *ctx)
{
    char *url = malloc(len + 1);
    ota_type_t type = (ota_type_t)ctx;
    ota_err_t err;

    memcpy(url, payload, len);
    url[len] = '\0';
    ESP_LOGI(TAG, "Starting %s update from %s",
        type == OTA_TYPE_FIRMWARE ? "firmware" : "configuration", url);

   #if 0
    if ((err = ota_download(type, url, _ota_on_completed)) != OTA_ERR_SUCCESS)
        ESP_LOGE(TAG, "Failed updating: %s", ota_err_to_str(err));
   #endif
    ble_disconnect_all();
    ble_scan_stop();
    free(url);
}

static void _ota_on_mqtt(const char *topic, const uint8_t *payload, size_t len,void *ctx);

static void ota_subscribe(void)
{
    char topic[MAX_TOPIC_LEN];

    /* Register for both a specific topic for this device and a general one */
    snprintf(topic, MAX_TOPIC_LEN, "%s/OTA/Firmware", ixe_ble_name_get());
    mqtt_subscribe(topic, 0, _ota_on_mqtt, (void *)OTA_TYPE_FIRMWARE, NULL);
    mqtt_subscribe("BLE2MQTT/OTA/Firmware", 0, _ota_on_mqtt,
        (void *)OTA_TYPE_FIRMWARE, NULL);

    snprintf(topic, MAX_TOPIC_LEN, "%s/OTA/Config", ixe_ble_name_get());
    mqtt_subscribe(topic, 0, _ota_on_mqtt, (void *)OTA_TYPE_CONFIG, NULL);
    mqtt_subscribe("BLE2MQTT/OTA/Config", 0, _ota_on_mqtt,
        (void *)OTA_TYPE_CONFIG, NULL);
}

static void ota_unsubscribe(void)
{
    char topic[27];

    sprintf(topic, "%s/OTA/Firmware", ixe_ble_name_get());
    mqtt_unsubscribe(topic);
    mqtt_unsubscribe("BLE2MQTT/OTA/Firmware");

    sprintf(topic, "%s/OTA/Config", ixe_ble_name_get());
    mqtt_unsubscribe(topic);
    mqtt_unsubscribe("BLE2MQTT/OTA/Config");
}

/* Management functions */
static void management_on_restart_mqtt(const char *topic,
    const uint8_t *payload, size_t len, void *ctx)
{
    if (len != 4 || strncmp((char *)payload, "true", len))
        return;

    abort();
}

static void _management_on_restart_mqtt(const char *topic,
    const uint8_t *payload, size_t len, void *ctx);

static void management_subscribe(void)
{
    char topic[MAX_TOPIC_LEN];

    snprintf(topic, MAX_TOPIC_LEN, "%s/Restart", ixe_ble_name_get());
    mqtt_subscribe(topic, 0, _management_on_restart_mqtt, NULL, NULL);
    mqtt_subscribe("BLE2MQTT/Restart", 0, _management_on_restart_mqtt, NULL,
        NULL);
}

static void management_unsubscribe(void)
{
    char topic[MAX_TOPIC_LEN];

    snprintf(topic, MAX_TOPIC_LEN, "%s/Restart", ixe_ble_name_get());
    mqtt_unsubscribe(topic);
    mqtt_unsubscribe("BLE2MQTT/Restart");
}

static void cleanup(void)
{
    ble_disconnect_all();
    ble_scan_stop();
    ota_unsubscribe();
    management_unsubscribe();
}

/* Network callback functions */
static void network_on_connected(void)
{
    char status_topic[MAX_TOPIC_LEN];

    ESP_LOGI(TAG, "Connected to the network, connecting to MQTT");
    snprintf(status_topic, MAX_TOPIC_LEN, "%s/Status", ixe_ble_name_get());

    mqtt_connect();
}

static void network_on_disconnected(void)
{
    ESP_LOGI(TAG, "Disconnected from the network, stopping MQTT");
    mqtt_disconnect();
    /* We don't get notified when manually stopping MQTT */
    cleanup();
}

/* MQTT callback functions */
static void mqtt_on_connected(void)
{
    ESP_LOGI(TAG, "Connected to MQTT, scanning for BLE devices");
    //ota_subscribe();
    management_subscribe();
    ble_scan_start();
}

static void mqtt_on_disconnected(void)
{
    static uint8_t num_disconnections = 0;

    ESP_LOGI(TAG, "Disconnected from MQTT, stopping BLE");
    cleanup();

    if (++num_disconnections % 3 == 0)
    {
        ESP_LOGI(TAG,
            "Failed connecting to MQTT 3 times, reconnecting to the network");
        //wifi_reconnect();
    }
}

/* BLE functions */
static void ble_on_mqtt_connected_cb(const char *topic, const uint8_t *payload,
    size_t len, void *ctx)
{
    char new_topic[MAX_TOPIC_LEN];

    if (len == 4 && !strncmp((char *)payload, "true", len))
        return;

    /* Someone published our device is disconnected, set them straight */
    snprintf(new_topic, MAX_TOPIC_LEN, "%s/Connected",(char *)ctx);
    mqtt_publish(new_topic, (uint8_t *)"true", 4, 1,0);
}

static void _ble_on_mqtt_connected_cb(const char *topic, const uint8_t *payload,
    size_t len, void *ctx);

static void ble_publish_connected(mac_addr_t mac, uint8_t is_connected)
{
    char topic[MQTT_TOPIC_LEN];
	char buf[MQTT_PUBBUF_LEN];
    ble_device_t *ble_device;

    const char *device_name = ixe_ble_name_get();
	ble_device = ble_device_find_by_mac(devices_list, mac);
	
    snprintf(topic, MAX_TOPIC_LEN, "%s/Connect",ixe_ble_name_get());

    if (!is_connected)
        mqtt_unsubscribe(topic);
	
    snprintf(buf, MQTT_PUBBUF_LEN, "%s Connected",ble_device->name);
    mqtt_publish(topic, (uint8_t *)buf,MQTT_PUBBUF_LEN, 1,0);

    if (is_connected)
    {  
        /* Subscribe for other devices claiming this device is disconnected */
       #if 0
		mqtt_subscribe(topic, config_mqtt_qos_get(), _ble_on_mqtt_connected_cb,
            strdup(mactoa(mac)), free);
	   #endif
        /* We are now the owner of this device */
        snprintf(topic, MAX_TOPIC_LEN, "%s" MAC_FMT "/Owner",device_name, MAC_PARAM(mac));
        mqtt_publish(topic, (uint8_t *)device_name, strlen(device_name),1, 0);
    }
}

static mqtt_ctx_t *ble_ctx_gen(mac_addr_t mac, ble_uuid_t service,
    ble_uuid_t characteristic)
{
    mqtt_ctx_t *ctx = malloc(sizeof(mqtt_ctx_t));

    memcpy(ctx->mac, mac, sizeof(mac_addr_t));
    memcpy(ctx->service, service, sizeof(ble_uuid_t));
    memcpy(ctx->characteristic, characteristic, sizeof(ble_uuid_t));

    return ctx;
}

/* BLE callback functions */

static char *ble_topic(mac_addr_t mac, ble_uuid_t service_uuid,
    ble_uuid_t characteristic_uuid)
{
    static char topic[MAX_TOPIC_LEN];
    int i;

    i = snprintf(topic, MAX_TOPIC_LEN, MAC_FMT ,MAC_PARAM(mac));

    return topic;
}

static void ble_on_device_disconnected(mac_addr_t mac)
{
    char topic[MAX_TOPIC_LEN];

    ESP_LOGI(TAG, "Disconnected from device: " MAC_FMT, MAC_PARAM(mac));
    ble_publish_connected(mac, 0);
    snprintf(topic, MAX_TOPIC_LEN, MAC_FMT "/",MAC_PARAM(mac));
    mqtt_unsubscribe_topic_prefix(topic);
}

static void ble_on_mqtt_get(const char *topic, const uint8_t *payload,
    size_t len, void *ctx)
{
    ESP_LOGD(TAG, "Got read request: %s", topic);
    mqtt_ctx_t *data = (mqtt_ctx_t *)ctx;

    ble_characteristic_read(data->mac, data->service, data->characteristic);
}

static void ble_on_mqtt_set(const char *topic, const uint8_t *payload,
    size_t len, void *ctx)
{
    ESP_LOGD(TAG, "Got write request: %s, len: %u", topic, len);
    mqtt_ctx_t *data = (mqtt_ctx_t *)ctx;
    size_t buf_len = 32;
    uint8_t buf[32] = {0};

    ble_characteristic_write(data->mac, data->service, data->characteristic,
        buf, buf_len);

    /* Issue a read request to get latest value */
    ble_characteristic_read(data->mac, data->service, data->characteristic);
}

static void _ble_on_mqtt_get(const char *topic, const uint8_t *payload,
    size_t len, void *ctx);
static void _ble_on_mqtt_set(const char *topic, const uint8_t *payload,
    size_t len, void *ctx);

static void ble_on_device_discovered(mac_addr_t mac, int rssi)
{
   #if 0
    uint8_t connect = config_ble_should_connect(mactoa(mac));

    ESP_LOGI(TAG, "-%d- Discovered BLE device: " MAC_FMT " (RSSI: %d), %sconnecting",
        __LINE__,MAC_PARAM(mac), rssi, connect ? "" : "not ");

    if (!connect)
        return;
    #endif
    //ble_connect(mac);
}

static void ble_on_device_connected(mac_addr_t mac)
{
    ESP_LOGI(TAG, "-%d-Connected to device: " MAC_FMT ", scanning",
        __LINE__,MAC_PARAM(mac));
    ble_publish_connected(mac, 1);
    //ble_services_scan(mac);
}


static void ble_on_characteristic_found(mac_addr_t mac, ble_uuid_t service_uuid,
    ble_uuid_t characteristic_uuid, uint8_t properties)
{
    ESP_LOGD(TAG, "Found new characteristic: service: " UUID_FMT
      ", characteristic: " UUID_FMT ", properties: 0x%x",
      UUID_PARAM(service_uuid), UUID_PARAM(characteristic_uuid), properties);
    char *topic;

	#if 0
    topic = ble_topic(mac, service_uuid, characteristic_uuid);
	
    /* Characteristic is readable */
    if (properties & CHAR_PROP_READ)
    {
        mqtt_subscribe(ble_topic_suffix(topic, 1), config_mqtt_qos_get(),
            _ble_on_mqtt_get, ble_ctx_gen(mac, service_uuid,
            characteristic_uuid), free);
        ble_characteristic_read(mac, service_uuid, characteristic_uuid);
    }

    /* Characteristic is writable */
    if (properties & (CHAR_PROP_WRITE | CHAR_PROP_WRITE_NR))
    {
        mqtt_subscribe(ble_topic_suffix(topic, 0), config_mqtt_qos_get(),
            _ble_on_mqtt_set, ble_ctx_gen(mac, service_uuid,
            characteristic_uuid), free);
    }

    /* Characteristic can notify / indicate on changes */
    if (properties & (CHAR_PROP_NOTIFY | CHAR_PROP_INDICATE))
    {
        ble_characteristic_notify_register(mac, service_uuid,
            characteristic_uuid);
    }
	#endif
}

static void ble_on_device_services_discovered(mac_addr_t mac)
{
    ESP_LOGD(TAG, "-%d-Services discovered on device: " MAC_FMT, __LINE__,MAC_PARAM(mac));
    ble_foreach_characteristic(mac, ble_on_characteristic_found);
}

static void ble_on_device_characteristic_value(mac_addr_t mac,
    ble_uuid_t service, ble_uuid_t characteristic, uint8_t *value,
    size_t value_len)
{
    char *topic = ble_topic(mac, service, characteristic);
    char *payload = "hello";
    size_t payload_len = strlen(payload);

    ESP_LOGI(TAG, "Publishing: %s = %s", topic, payload);
    mqtt_publish(topic, (uint8_t *)payload, payload_len, 1,0);
}

/* BLE2MQTT Task and event callbacks */
typedef enum {
    EVENT_TYPE_HEARTBEAT_TIMER,
    EVENT_TYPE_NETWORK_CONNECTED,
    EVENT_TYPE_NETWORK_DISCONNECTED,
    EVENT_TYPE_OTA_MQTT,
    EVENT_TYPE_OTA_COMPLETED,
    EVENT_TYPE_MANAGEMENT_RESTART_MQTT,
    EVENT_TYPE_MQTT_CONNECTED,
    EVENT_TYPE_MQTT_DISCONNECTED,
    EVENT_TYPE_BLE_DEVICE_DISCOVERED,
    EVENT_TYPE_BLE_DEVICE_CONNECTED,
    EVENT_TYPE_BLE_DEVICE_DISCONNECTED,
    EVENT_TYPE_BLE_DEVICE_SERVICES_DISCOVERED,
    EVENT_TYPE_BLE_DEVICE_CHARACTERISTIC_VALUE,
    EVENT_TYPE_BLE_MQTT_CONNECTED,
    EVENT_TYPE_BLE_MQTT_GET,
    EVENT_TYPE_BLE_MQTT_SET,
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        struct {
            ota_type_t type;
            ota_err_t err;
        } ota_completed;
        struct {
            char *topic;
            uint8_t *payload;
            size_t len;
            void *ctx;
        } mqtt_message;
        struct {
            mac_addr_t mac;
            int rssi;
        } ble_device_discovered;
        struct {
            mac_addr_t mac;
        } ble_device_connected;
        struct {
            mac_addr_t mac;
        } ble_device_disconnected;
        struct {
            mac_addr_t mac;
        } ble_device_services_discovered;
        struct {
            mac_addr_t mac;
            ble_uuid_t service;
            ble_uuid_t characteristic;
            uint8_t *value;
            size_t value_len;
        } ble_device_characteristic_value;
    };
} event_t;

static QueueHandle_t event_queue;

static void blegw_handle_event(event_t *event)
{
    switch (event->type)
    {
    case EVENT_TYPE_HEARTBEAT_TIMER:
        uptime_publish();
        break;
    case EVENT_TYPE_NETWORK_CONNECTED:
		ESP_ERROR_CHECK(ble_initialize());
        network_on_connected();
        break;
    case EVENT_TYPE_NETWORK_DISCONNECTED:
        network_on_disconnected();
        break;
    case EVENT_TYPE_OTA_MQTT:
        ota_on_mqtt(event->mqtt_message.topic, event->mqtt_message.payload,
            event->mqtt_message.len, event->mqtt_message.ctx);
        free(event->mqtt_message.topic);
        free(event->mqtt_message.payload);
        break;
    case EVENT_TYPE_OTA_COMPLETED:
        ota_on_completed(event->ota_completed.type, event->ota_completed.err);
        break;
    case EVENT_TYPE_MANAGEMENT_RESTART_MQTT:
        management_on_restart_mqtt(event->mqtt_message.topic,
            event->mqtt_message.payload, event->mqtt_message.len,
            event->mqtt_message.ctx);
        free(event->mqtt_message.topic);
        free(event->mqtt_message.payload);
        break;
    case EVENT_TYPE_MQTT_CONNECTED:
        mqtt_on_connected();
        break;
    case EVENT_TYPE_MQTT_DISCONNECTED:
        mqtt_on_disconnected();
        break;
    case EVENT_TYPE_BLE_DEVICE_DISCOVERED:
        ble_on_device_discovered(event->ble_device_discovered.mac,
            event->ble_device_discovered.rssi);
        break;
    case EVENT_TYPE_BLE_DEVICE_CONNECTED:
        ble_on_device_connected(event->ble_device_connected.mac);
        break;
    case EVENT_TYPE_BLE_DEVICE_DISCONNECTED:
        ble_on_device_disconnected(event->ble_device_disconnected.mac);
        break;
    case EVENT_TYPE_BLE_DEVICE_SERVICES_DISCOVERED:
        ble_on_device_services_discovered(
            event->ble_device_services_discovered.mac);
        break;
    case EVENT_TYPE_BLE_DEVICE_CHARACTERISTIC_VALUE:
        ble_on_device_characteristic_value(
            event->ble_device_characteristic_value.mac,
            event->ble_device_characteristic_value.service,
            event->ble_device_characteristic_value.characteristic,
            event->ble_device_characteristic_value.value,
            event->ble_device_characteristic_value.value_len);
        free(event->ble_device_characteristic_value.value);
        break;
    case EVENT_TYPE_BLE_MQTT_CONNECTED:
        ble_on_mqtt_connected_cb(event->mqtt_message.topic, event->mqtt_message.payload,
            event->mqtt_message.len, event->mqtt_message.ctx);
        free(event->mqtt_message.topic);
        free(event->mqtt_message.payload);
        break;
    case EVENT_TYPE_BLE_MQTT_GET:
        ble_on_mqtt_get(event->mqtt_message.topic, event->mqtt_message.payload,
            event->mqtt_message.len, event->mqtt_message.ctx);
        free(event->mqtt_message.topic);
        free(event->mqtt_message.payload);
        break;
    case EVENT_TYPE_BLE_MQTT_SET:
        ble_on_mqtt_set(event->mqtt_message.topic, event->mqtt_message.payload,
            event->mqtt_message.len, event->mqtt_message.ctx);
        free(event->mqtt_message.topic);
        free(event->mqtt_message.payload);
        break;
    }

    free(event);
}

static void blegw_task(void *pvParameter)
{
    event_t *event;

    while (1)
    {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) != pdTRUE)
            continue;

        blegw_handle_event(event);
    }

    vTaskDelete(NULL);
}

static void heartbeat_timer_cb(TimerHandle_t xTimer)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_HEARTBEAT_TIMER;

    ESP_LOGD(TAG, "Queuing event HEARTBEAT_TIMER");
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static int start_blegw_task(void)
{
    TimerHandle_t hb_timer;

    if (!(event_queue = xQueueCreate(10, sizeof(event_t *))))
        return -1;

    if (xTaskCreatePinnedToCore(blegw_task, "ble2mqtt_task", 4096, NULL, 5,
        NULL, 1) != pdPASS)
    {
        return -1;
    }


    hb_timer = xTimerCreate("heartbeat", pdMS_TO_TICKS(60 * 1000), pdTRUE,
        NULL, heartbeat_timer_cb);
    xTimerStart(hb_timer, 0);

    return 0;
}

static void _mqtt_on_message(event_type_t type, const char *topic,
    const uint8_t *payload, size_t len, void *ctx)
{
    event_t *event = malloc(sizeof(*event));

    event->type = type;
    event->mqtt_message.topic = strdup(topic);
    event->mqtt_message.payload = malloc(len);
    memcpy(event->mqtt_message.payload, payload, len);
    event->mqtt_message.len = len;
    event->mqtt_message.ctx = ctx;

    ESP_LOGD(TAG, "-%d-Queuing event MQTT message %d (%s, %p, %u, %p)",__LINE__, type, topic,
        payload, len, ctx);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _network_on_connected(void)
{
    event_t *event = malloc(sizeof(*event));
    event->type = EVENT_TYPE_NETWORK_CONNECTED;

    ESP_LOGD(TAG, "-%d- Queuing event NETWORK_CONNECTED",__LINE__);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _network_on_disconnected(void)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_NETWORK_DISCONNECTED;

    ESP_LOGD(TAG, "-%d- Queuing event NETWORK_DISCONNECTED",__LINE__);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ota_on_mqtt(const char *topic, const uint8_t *payload, size_t len,
    void *ctx)
{
    _mqtt_on_message(EVENT_TYPE_OTA_MQTT, topic, payload, len, ctx);
}

#if 1
static void _ota_on_completed(ota_type_t type, ota_err_t err)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_OTA_COMPLETED;
    event->ota_completed.type = type;
    event->ota_completed.err = err;

    ESP_LOGD(TAG, "Queuing event HEARTBEAT_TIMER (%d, %d)", type, err);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}
#endif
static void _management_on_restart_mqtt(const char *topic,
    const uint8_t *payload, size_t len, void *ctx)
{
    _mqtt_on_message(EVENT_TYPE_MANAGEMENT_RESTART_MQTT, topic, payload, len,
        ctx);
}

static void _mqtt_on_connected(void)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_MQTT_CONNECTED;

    ESP_LOGI(TAG, "-%d- Queuing event MQTT_CONNECTED",__LINE__);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _mqtt_on_disconnected(void)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_MQTT_DISCONNECTED;

    ESP_LOGD(TAG, "-%d- Queuing event MQTT_DISCONNECTED",__LINE__);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ble_on_device_discovered(mac_addr_t mac, int rssi)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_BLE_DEVICE_DISCOVERED;
    memcpy(event->ble_device_discovered.mac, mac, sizeof(mac_addr_t));
    event->ble_device_discovered.rssi = rssi;

    ESP_LOGD(TAG, "-%d- Queuing event BLE_DEVICE_DISCOVERED (" MAC_FMT ", %d)",
        __LINE__,MAC_PARAM(mac), rssi);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ble_on_device_connected(mac_addr_t mac)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_BLE_DEVICE_CONNECTED;
    memcpy(event->ble_device_connected.mac, mac, sizeof(mac_addr_t));

    ESP_LOGD(TAG, "-%d- Queuing event BLE_DEVICE_CONNECTED (" MAC_FMT ")",
        __LINE__,MAC_PARAM(mac));
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ble_on_device_disconnected(mac_addr_t mac)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_BLE_DEVICE_DISCONNECTED;
    memcpy(event->ble_device_disconnected.mac, mac, sizeof(mac_addr_t));

    ESP_LOGD(TAG, "Queuing event BLE_DEVICE_DISCONNECTED (" MAC_FMT ")",
        MAC_PARAM(mac));
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ble_on_device_services_discovered(mac_addr_t mac)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_BLE_DEVICE_SERVICES_DISCOVERED;
    memcpy(event->ble_device_services_discovered.mac, mac, sizeof(mac_addr_t));

    ESP_LOGD(TAG, "-%d-Queuing event BLE_DEVICE_SERVICES_DISCOVERED (" MAC_FMT ")",
        __LINE__,MAC_PARAM(mac));
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ble_on_device_characteristic_value(mac_addr_t mac,
    ble_uuid_t service, ble_uuid_t characteristic, uint8_t *value,
    size_t value_len)
{
    event_t *event = malloc(sizeof(*event));

    event->type = EVENT_TYPE_BLE_DEVICE_CHARACTERISTIC_VALUE;
    memcpy(event->ble_device_characteristic_value.mac, mac, sizeof(mac_addr_t));
    memcpy(event->ble_device_characteristic_value.service, service,
        sizeof(ble_uuid_t));
    memcpy(event->ble_device_characteristic_value.characteristic,
        characteristic, sizeof(ble_uuid_t));
    event->ble_device_characteristic_value.value = malloc(value_len);
    memcpy(event->ble_device_characteristic_value.value, value, value_len);
    event->ble_device_characteristic_value.value_len = value_len;

    ESP_LOGD(TAG, "-%d-Queuing event BLE_DEVICE_CHARACTERISTIC_VALUE (" MAC_FMT ", "
        UUID_FMT ", %p, %u)",__LINE__, MAC_PARAM(mac), UUID_PARAM(characteristic), value,
        value_len);
    xQueueSend(event_queue, &event, portMAX_DELAY);
}

static void _ble_on_mqtt_connected_cb(const char *topic, const uint8_t *payload,
    size_t len, void *ctx)
{
    _mqtt_on_message(EVENT_TYPE_BLE_MQTT_CONNECTED, topic, payload, len, ctx);
}

static void _ble_on_mqtt_get(const char *topic, const uint8_t *payload,
    size_t len, void *ctx)
{
    _mqtt_on_message(EVENT_TYPE_BLE_MQTT_GET, topic, payload, len, ctx);
}

static void _ble_on_mqtt_set(const char *topic, const uint8_t *payload,
    size_t len, void *ctx)
{
    _mqtt_on_message(EVENT_TYPE_BLE_MQTT_SET, topic, payload, len, ctx);
}


void app_main()
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Init bgs params */
	ixe_init_params();

    /* Init WIFI cb */
    wifi_set_on_connected_cb(_network_on_connected);
    wifi_set_on_disconnected_cb(_network_on_disconnected);

	/* Init MQTT */
    ESP_ERROR_CHECK(mqtt_initialize());
    mqtt_set_on_connected_cb(_mqtt_on_connected);
    mqtt_set_on_disconnected_cb(_mqtt_on_disconnected);

    #if 1
    /* Init BLE */
    ble_set_on_device_discovered_cb(_ble_on_device_discovered);
    ble_set_on_device_connected_cb(_ble_on_device_connected);
    ble_set_on_device_disconnected_cb(_ble_on_device_disconnected);
    ble_set_on_device_services_discovered_cb(_ble_on_device_services_discovered);
    ble_set_on_device_characteristic_value_cb(_ble_on_device_characteristic_value);
    #endif

	ixe_initialise_wifi();
    ESP_LOGI(TAG,"[%d]...init wifi ok ....\n",__LINE__);
    
	
    if(x_wifi.wifi_onoff == 0)
	{
	  /* Start blufi */
	  ixe_blufi_start();
    }
	
    ESP_ERROR_CHECK(start_blegw_task());
	
	xTaskCreate(ixe_sntp_task, "ixe_sntp_task", 4096, NULL, 4, NULL); 
	//xTaskCreate(ixe_ota_task, "ixe_ota_task", 4096, NULL, 3, NULL);
	xTaskCreate(bgs_led_key_task, "bgs_led_key_task",2048, NULL, 10, NULL);
	//xTaskCreate(ise_util_pump_task, "ise_util_pump_task",2048, NULL, 10, NULL);

	while(1)
	{
      sleep(1);
	  if(ixe_get_ble_ota())
	  { 
	    ESP_LOGI(TAG,"-%d-Start ble ota...",__LINE__);
		ixe_set_ble_ota(0);
	    ixe_ble_ota();
	  }
	}
}
