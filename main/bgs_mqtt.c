#include <esp_err.h>
#include <esp_log.h>
#include <mqtt_client.h>
#include <string.h>
#include "cJSON.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"

#include "esp_partition.h"

#include "ixe_params.h"

#include "bgs_mqtt.h"

extern   IxeBle			   x_ble;
extern   IxeWifi		   x_wifi;
extern   IxeData		   x_datas;


/* Constants */
static const char *TAG = "BGS_MQTT";

extern const uint8_t mqtt_server_ca_pem_start[]   asm("_binary_ca_cert_pem_start");
extern const uint8_t mqtt_server_ca_pem_end[]   asm("_binary_ca_cert_pem_end");


/* Types */
typedef struct mqtt_subscription_t {
    struct mqtt_subscription_t *next;
    char *topic;
    mqtt_on_message_received_cb_t cb;
    void *ctx;
    mqtt_free_ctx_cb_t free_cb;
} mqtt_subscription_t;

typedef struct mqtt_publications_t {
    struct mqtt_publications_t *next;
    char *topic;
    uint8_t *payload;
    size_t len;
    int qos;
    uint8_t retained;
} mqtt_publications_t;

/* Internal state */
static esp_mqtt_client_handle_t mqtt_handle = NULL;
static mqtt_subscription_t *subscription_list = NULL;
static mqtt_publications_t *publications_list = NULL;
static uint8_t mqtt_connected = 0;

/* Callback functions */
static mqtt_on_connected_cb_t mqtt_on_connected_cb = NULL;
static mqtt_on_disconnected_cb_t mqtt_on_disconnected_cb = NULL;

void mqtt_set_on_connected_cb(mqtt_on_connected_cb_t cb)
{
    mqtt_on_connected_cb = cb;
}

void mqtt_set_on_disconnected_cb(mqtt_on_disconnected_cb_t cb)
{
    mqtt_on_disconnected_cb = cb;
}

void mqtt_make_upline_buf(char *send_buf,uint8_t type)
{
    cJSON *root = NULL;
	char  *out = NULL;

	struct tm   timeinfo = { 0 };
    char        strftime_buf[64];
    time_t      now = 0;

	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
	
	const esp_partition_t *running = esp_ota_get_running_partition();
	esp_app_desc_t running_app_info;
	if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
		 ESP_LOGI(TAG, "Running firmware version: %s\n", running_app_info.version);
	 }

	root = cJSON_CreateObject();
	if(type)
	{
	  cJSON_AddNumberToObject(root, "upline", 0);
	  cJSON_AddNumberToObject(root, "type", type);
	}else{
      cJSON_AddNumberToObject(root, "upline", 1);
	  cJSON_AddNumberToObject(root, "type", type);
	}
	
	cJSON_AddStringToObject(root, "version", running_app_info.version);
	cJSON_AddNumberToObject(root, "time-ts", now);
	cJSON_AddStringToObject(root, "time-str", strftime_buf);
	
	out = cJSON_Print(root);
	cJSON_Delete(root);
	
	//BLUFI_INFO("ixe upline send json data len = %d :%s\n",strlen(out),out);
    strcpy(send_buf,out);
	free(out);

	return;
}

void mqtt_make_update_buf(char *send_buf,uint8_t num)
{
    cJSON *root = NULL;
	char  *out = NULL;
    
	struct tm	timeinfo = { 0 };
	char		strftime_buf[64];
	time_t		now = 0;
    time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
	
	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "update", 1);
	cJSON_AddNumberToObject(root, "status", num);
	cJSON_AddNumberToObject(root, "time-ts", now);
	cJSON_AddStringToObject(root, "time-str", strftime_buf);
	
	out = cJSON_Print(root);
	cJSON_Delete(root);
	
	//BLUFI_INFO("ixe update send json data len = %d :%s\n",strlen(out),out);
    strcpy(send_buf,out);
	free(out);

	return;
}

void mqtt_make_notify_buf(char *send_buf,uint8_t num)
{
    cJSON *root = NULL;
	char  *out = NULL;

    struct tm	timeinfo = { 0 };
	char		strftime_buf[64];
	time_t		now = 0;
    time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
	
	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "notify", 1);
	cJSON_AddNumberToObject(root, "dev_status", num);
	cJSON_AddNumberToObject(root, "time-ts", now);
	cJSON_AddStringToObject(root, "time-str", strftime_buf);
	
	out = cJSON_Print(root);
	cJSON_Delete(root);
	
	//BLUFI_INFO("ixe update send json data len = %d :%s\n",strlen(out),out);
    strcpy(send_buf,out);
	free(out);

	return;
}

void mqtt_make_heart_buf(char *send_buf,uint8_t num)
{
    cJSON *root = NULL;
	cJSON *array = NULL;
	char  *out = NULL;
	int   data[5] = {x_datas.status,x_wifi.wifi_onoff,x_datas.wifi_con,x_datas.ble_con,0};

	struct tm   timeinfo = { 0 };
    char        strftime_buf[64];
    time_t      now = 0;

	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

	root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "heart", num);
	
	array = cJSON_CreateIntArray(data, 5);
	cJSON_AddItemToObject(root, "data",array);

	cJSON_AddNumberToObject(root, "time-ts", now);
	cJSON_AddStringToObject(root, "time-str", strftime_buf);
   
	out = cJSON_Print(root);
	cJSON_Delete(root);
	
	//BLUFI_INFO("ixe heart send json data len = %d :%s\n",strlen(out),out);
    strcpy(send_buf,out);
	free(out);

	return;
}

void xmqtt_inform_online(uint8_t type)
{
  char topic[MQTT_TOPIC_LEN] = {0};
  char buf[MQTT_PUBBUF_LEN] = {0};

  /* Only publish uptime when connected, we don't want it to be queued */
  if (!mqtt_is_connected())
        return;
  
  mqtt_make_upline_buf(buf,type);
  //snprintf(topic, MAX_TOPIC_LEN, "%s/inform", device_name_get());
  mqtt_publish(topic, (uint8_t *)buf, strlen(buf), 1, 0);
 // ESP_LOGI(TAG,"[%d]mqtt publish, topic:%s,payload:%s", __LINE__,xm_params.mqtt_inform_topic,xm_params.mqtt_pub_buf);   
}


static mqtt_subscription_t *mqtt_subscription_add(mqtt_subscription_t **list,
    const char *topic, mqtt_on_message_received_cb_t cb, void *ctx,
    mqtt_free_ctx_cb_t free_cb)
{
    mqtt_subscription_t *sub, **cur;

    sub = malloc(sizeof(*sub));
    sub->next = NULL;
    sub->topic = strdup(topic);
    sub->cb = cb;
    sub->ctx = ctx;
    sub->free_cb = free_cb;

    for (cur = list; *cur; cur = &(*cur)->next);
    *cur = sub;

    return sub;
}

static void mqtt_subscription_free(mqtt_subscription_t *mqtt_subscription)
{
    if (mqtt_subscription->ctx && mqtt_subscription->free_cb)
        mqtt_subscription->free_cb(mqtt_subscription->ctx);
    free(mqtt_subscription->topic);
    free(mqtt_subscription);
}

static void mqtt_subscriptions_free(mqtt_subscription_t **list)
{
    mqtt_subscription_t *cur, **head = list;

    while (*list)
    {
        cur = *list;
        *list = cur->next;
        mqtt_subscription_free(cur);
    }
    *head = NULL;
}

static void mqtt_subscription_remove(mqtt_subscription_t **list,
    const char *topic)
{
    mqtt_subscription_t **cur, *tmp;

    for (cur = list; *cur; cur = &(*cur)->next)
    {
        if (!strcmp((*cur)->topic, topic))
            break;
    }

    if (!*cur)
        return;

    tmp = *cur;
    *cur = (*cur)->next;
    mqtt_subscription_free(tmp);
}

static mqtt_publications_t *mqtt_publication_add(mqtt_publications_t **list,
    const char *topic, uint8_t *payload, size_t len, int qos, uint8_t retained)
{
    mqtt_publications_t *pub = malloc(sizeof(*pub));

    pub->topic = strdup(topic);
    pub->payload = malloc(len);
    memcpy(pub->payload, payload, len);
    pub->len = len;
    pub->qos = qos;
    pub->retained = retained;

    pub->next = *list;
    *list = pub;

    return pub;
}

static void mqtt_publication_free(mqtt_publications_t *mqtt_publication)
{
    free(mqtt_publication->topic);
    free(mqtt_publication->payload);
    free(mqtt_publication);
}

static void mqtt_publications_free(mqtt_publications_t **list)
{
    mqtt_publications_t *cur, **head = list;

    while (*list)
    {
        cur = *list;
        *list = cur->next;
        mqtt_publication_free(cur);
    }
    *head = NULL;
}

static void mqtt_publications_publish(mqtt_publications_t *list)
{
    for (; list; list = list->next)
    {
        ESP_LOGI(TAG, "Publishing from queue: %s = %.*s", list->topic,
            list->len, list->payload);

        mqtt_publish(list->topic, list->payload, list->len, list->qos,
            list->retained);
    }
}

int mqtt_subscribe(const char *topic, int qos, mqtt_on_message_received_cb_t cb,
    void *ctx, mqtt_free_ctx_cb_t free_cb)
{
    if (!mqtt_connected)
        return -1;

    ESP_LOGD(TAG, "Subscribing to %s", topic);
    if (esp_mqtt_client_subscribe(mqtt_handle, topic, qos) < 0)
    {
        ESP_LOGE(TAG, "Failed subscribing to %s", topic);
        return -1;
    }

    mqtt_subscription_add(&subscription_list, topic, cb, ctx, free_cb);
    return 0;
}

int mqtt_unsubscribe_topic_prefix(const char *topic_prefix)
{
    mqtt_subscription_t *tmp, **cur = &subscription_list;
    size_t prefix_len = strlen(topic_prefix);

    ESP_LOGD(TAG, "Unsubscribing topics with %s prefix", topic_prefix);

    while (*cur)
    {
        tmp = *cur;
        if (strncmp(topic_prefix, (*cur)->topic, prefix_len))
        {
            cur = &(*cur)->next;
            continue;
        }
        *cur = (*cur)->next;

        ESP_LOGD(TAG, "Unsubscribing from %s", tmp->topic);
        if (mqtt_connected)
            esp_mqtt_client_unsubscribe(mqtt_handle, tmp->topic);
        mqtt_subscription_free(tmp);
    }

    return 0;
}

int mqtt_unsubscribe(const char *topic)
{
    ESP_LOGD(TAG, "Unsubscribing from %s", topic);
    mqtt_subscription_remove(&subscription_list, topic);

    if (!mqtt_connected)
        return 0;

    return esp_mqtt_client_unsubscribe(mqtt_handle, topic);
}

int mqtt_publish(const char *topic, uint8_t *payload, size_t len, int qos,
    uint8_t retained)
{
    if (mqtt_connected)
    {
        return esp_mqtt_client_publish(mqtt_handle, (char *)topic,
            (char *)payload, len, qos, retained) < 0;
    }

    /* If we're currently not connected, queue publication */
    ESP_LOGD(TAG, "MQTT is disconnected, adding publication to queue...");
    mqtt_publication_add(&publications_list, topic, payload, len, qos,
        retained);

    return 0;
}

static void mqtt_message_cb(const char *topic, size_t topic_len,
    uint8_t *payload, size_t len)
{
    mqtt_subscription_t *cur;

    ESP_LOGD(TAG, "Received: %.*s => %.*s (%d)\n", topic_len, topic, len,
        payload, (int)len);

    for (cur = subscription_list; cur; cur = cur->next)
    {
        /* TODO: Correctly match MQTT topics (i.e. support wildcards) */
        if (strncmp(cur->topic, topic, topic_len) ||
            cur->topic[topic_len] != '\0')
        {
            continue;
        }

        cur->cb(cur->topic, payload, len, cur->ctx);
    }
}

static esp_err_t mqtt_event_cb(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT client connected");
        mqtt_connected = 1;
        mqtt_publications_publish(publications_list);
        mqtt_publications_free(&publications_list);
        if (mqtt_on_connected_cb)
            mqtt_on_connected_cb();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT client disconnected");
        mqtt_connected = 0;
        mqtt_subscriptions_free(&subscription_list);
        if (mqtt_on_disconnected_cb)
            mqtt_on_disconnected_cb();
        break;
	case MQTT_EVENT_SUBSCRIBED://订阅成功
        	 //BLUFI_INFO("_---------订阅--------\n");
        ESP_LOGI(TAG,"MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED://取消订阅
        ESP_LOGI(TAG,"MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED://发布成功
        	 //BLUFI_INFO("_--------发布----------\n");
        ESP_LOGI(TAG,"MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        mqtt_message_cb(event->topic, event->topic_len, (uint8_t *)event->data,
            event->data_len);
        break;
    default:
        break;
    }

    return ESP_OK;
}

#if 0
int mqtt_connect(const char *host, uint16_t port, const char *client_id,
    const char *username, const char *password, uint8_t ssl,
    const char *server_cert, const char *client_cert, const char *client_key,
    const char *lwt_topic, const char *lwt_msg, uint8_t lwt_qos,
    uint8_t lwt_retain)
{
    esp_mqtt_client_config_t config = {
        .event_handle = mqtt_event_cb,
        .host = resolve_host(host),
        .port = port,
        .client_id = client_id,
        .username = username,
        .password = password,
        .transport = ssl ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP,
        .cert_pem = server_cert,
        .client_cert_pem = client_cert,
        .client_key_pem = client_key,
        .lwt_topic = lwt_topic,
        .lwt_msg = lwt_msg,
        .lwt_qos = lwt_qos,
        .lwt_retain = lwt_retain,
    };

    ESP_LOGI(TAG, "Connecting MQTT client");
    if (mqtt_handle)
        esp_mqtt_client_destroy(mqtt_handle);
    if (!(mqtt_handle = esp_mqtt_client_init(&config)))
        return -1;
    esp_mqtt_client_start(mqtt_handle);
    return 0;
}
#endif
int mqtt_connect(void)
{
    esp_mqtt_client_config_t config = {
        .event_handle = mqtt_event_cb,
        .uri = MQTT_URI, //MQTT 地址
        .port = MQTT_PORT,	 //MQTT端口
        .keepalive = 65,
        .username = (char*)&x_ble.ble_name,//用户名
        .password = (char*)&x_ble.ble_key,//密码
        .client_id = (char*)&x_ble.ble_name,
        .cert_pem = (const char *)mqtt_server_ca_pem_start,
    };
	
    ESP_LOGI(TAG, "Connecting MQTT client");
    if (mqtt_handle)
        esp_mqtt_client_destroy(mqtt_handle);
    if (!(mqtt_handle = esp_mqtt_client_init(&config)))
        return -1;
    esp_mqtt_client_start(mqtt_handle);
    return 0;
}

int mqtt_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting MQTT client");
    mqtt_connected = 0;
    mqtt_subscriptions_free(&subscription_list);
    if (mqtt_handle)
        esp_mqtt_client_destroy(mqtt_handle);
    mqtt_handle = NULL;

    return 0;
}

uint8_t mqtt_is_connected(void)
{
    return mqtt_connected;
}

int mqtt_initialize(void)
{
    ESP_LOGD(TAG, "Initializing MQTT client");
    return 0;
}
