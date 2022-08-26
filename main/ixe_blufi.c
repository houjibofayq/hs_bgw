/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


/****************************************************************************
* This is a demo for bluetooth config wifi connection to ap. You can config ESP32 to connect a softap
* or config ESP32 as a softap to be connected by other device. APP can be downloaded from github 
* android source code: https://github.com/EspressifApp/EspBlufi
* iOS source code: https://github.com/EspressifApp/EspBlufiForiOS
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_blufi.h"


#include <time.h>

#include "lwip/apps/sntp.h"

#include "esp_bt.h"
#include "esp_blufi_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

#include "ixe_blufi.h"

#include "ixe_blufi_command.h"
#include "ixe_blufi_security.h"

#include "ixe_params.h"


extern  IxeBle                    x_ble;
extern  IxeWifi                   x_wifi;
extern  IxeData                   x_datas;


#define  TAG "IXE_BLUFI"


QueueHandle_t ixe_blufi_uart_queue = NULL;
//static uint16_t blufi_mtu_size = 23;

static uint8_t ixe_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
};

static uint8_t ixe_manufacturer_data[8] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    'H', 'S', 'W', 'Z', 'N', '1', '2', '3',
};


//static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
static esp_ble_adv_data_t ixe_ble_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 8,
    .p_manufacturer_data =  ixe_manufacturer_data,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = ixe_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t ixe_ble_adv_params = {
    .adv_int_min        = 0x800,
    .adv_int_max        = 0x800,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define IXE_WIFI_LIST_NUM   10

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t ixe_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int ixe_CONNECTED_BIT = BIT0;
#define WIFI_FAIL_BIT      BIT1
#define ESP_WIFI_MAXIMUM_RETRY  3


/* store the station info for send back to phone */
//static uint8_t ixe_sta_bssid[6];
//static uint8_t ixe_sta_ssid[32];
//static int ixe_sta_ssid_len;

/* connect infor*/
static uint8_t ixe_server_if;
static uint16_t ixe_conn_id;

static uint8_t ixe_blufi_started = 0;

static wifi_on_connected_cb_t wifi_on_connected_cb = NULL;
static wifi_on_disconnected_cb_t wifi_on_disconnected_cb = NULL;


void wifi_set_on_connected_cb(wifi_on_connected_cb_t cb)
{
    wifi_on_connected_cb = cb;
}

void wifi_set_on_disconnected_cb(wifi_on_disconnected_cb_t cb)
{
    wifi_on_disconnected_cb = cb;
}


static void ixe_ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    wifi_mode_t mode;

    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: {
        esp_blufi_extra_info_t info;
        esp_wifi_get_mode(&mode);
        memset(&info, 0, sizeof(esp_blufi_extra_info_t));
        memcpy(&info.sta_bssid, (uint8_t*)&x_wifi.sta_bssid, 6);
        info.sta_bssid_set = true;
        info.sta_ssid = (uint8_t*)&x_wifi.sta_ssid;
        info.sta_ssid_len = x_wifi.ssid_len;
		ESP_LOGI(TAG,"[%d]BLUFI wifi is connected yet, AP:%s \n",__LINE__,x_wifi.sta_ssid);
        if (x_datas.ble_con == true) {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            BLUFI_INFO("BLUFI BLE is not connected yet\n");
        }
		sleep(1);
		if(ixe_blufi_started)
		  ixe_blufi_stop();
		if(wifi_on_connected_cb)
        {
          wifi_on_connected_cb();
		}
        break;
    }
    default:
        break;
    }
    return;
}

static void ixe_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    wifi_event_sta_connected_t *event;
    wifi_mode_t mode;

    switch (event_id) {
    case WIFI_EVENT_STA_START:
		BLUFI_INFO("[%d]esp32 wifi start connect\n",__LINE__);
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        event = (wifi_event_sta_connected_t*) event_data;
        memcpy((char*)&x_wifi.sta_bssid, (char*)event->bssid, 6);
		x_wifi.ssid_len = event->ssid_len;
		memset((char*)&x_wifi.sta_ssid, 0, 32);
        memcpy((char*)&x_wifi.sta_ssid, (char*)event->ssid, event->ssid_len);
        ESP_LOGI(TAG,"[%d] esp32 wifi connected,and save wifi ssid\n",__LINE__);
		ixe_save_wifi_params();
        break; 
    case WIFI_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
		BLUFI_ERROR( "[%d] esp32 wifi disconnect !!!",__LINE__);        
		if(wifi_on_disconnected_cb)
           wifi_on_disconnected_cb();
        esp_wifi_connect();
        break;
    case WIFI_EVENT_AP_START:
        esp_wifi_get_mode(&mode);

        /* TODO: get config or information of softap, then set to report extra_info */
        if (x_datas.ble_con == true) {
            if (x_datas.wifi_con) {  
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, NULL);
            } else {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
            }
        } else {
            BLUFI_INFO("BLUFI BLE is not connected yet\n");
        }
        break;
    case WIFI_EVENT_SCAN_DONE: {
        uint16_t apCount = 0;
        esp_wifi_scan_get_ap_num(&apCount);
        if (apCount == 0) {
            BLUFI_INFO("Nothing AP found");
            break;
        }
		BLUFI_INFO("[%d]BLUFI AP found %d !\n",__LINE__,apCount);
        wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        if (!ap_list) {
            BLUFI_ERROR("malloc error, ap_list is NULL");
            break;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
        esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            if (ap_list) {
                free(ap_list);
            }
            BLUFI_ERROR("malloc error, blufi_ap_list is NULL");
            break;
        }
        for (int i = 0; i < apCount; ++i)
        {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }
        
        if (x_datas.ble_con == true) {
            esp_blufi_send_wifi_list(apCount, blufi_ap_list);
        } else {
            BLUFI_INFO("BLUFI BLE is not connected yet\n");
        }

        esp_wifi_scan_stop();
		esp_wifi_connect();
        free(ap_list);
        free(blufi_ap_list);
        break;
    }
    default:
        break;
    }
    return;
}

void ixe_initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ixe_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ixe_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ixe_ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_MIN_MODEM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void ixe_blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
   
    static wifi_config_t   sta_config;
    static wifi_config_t   ap_config;
	
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        BLUFI_INFO("BLUFI init finish\n");
        esp_ble_gap_set_device_name(x_ble.ble_name);
        esp_ble_gap_config_adv_data(&ixe_ble_adv_data);
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        BLUFI_INFO("BLUFI deinit finish\n");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        x_datas.ble_con = true;
        ixe_server_if = param->connect.server_if;
        ixe_conn_id = param->connect.conn_id;
        esp_ble_gap_stop_advertising();
        blufi_security_init();
		BLUFI_INFO( "[%d]---blufi ble connected!---\r\n",__LINE__);
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        x_datas.ble_con = false;
        blufi_security_deinit();
        esp_ble_gap_start_advertising(&ixe_ble_adv_params);
        BLUFI_INFO( "[%d]---blufi ble disconnect!---\r\n",__LINE__);
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        BLUFI_INFO("BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        BLUFI_INFO("BLUFI requset wifi connect to AP,[%d]\n",__LINE__);
        /* there is no wifi callback when the device has already connected to this wifi
        so disconnect wifi before connection.
        */
        esp_wifi_disconnect();
        esp_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        BLUFI_INFO("BLUFI requset wifi disconnect from AP,[%d]\n",__LINE__);
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        BLUFI_ERROR("[%d]BLUFI report error, error code %d\n",__LINE__,param->report_error.state);
        //esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;
        esp_wifi_get_mode(&mode);
        if (x_datas.wifi_con) {  
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(&info.sta_bssid, (uint8_t*)&x_wifi.sta_bssid, 6);
            info.sta_bssid_set = true;
            memcpy(&info.sta_ssid, (char*)&x_wifi.sta_ssid, x_wifi.ssid_len);
            info.sta_ssid_len = x_wifi.ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        BLUFI_INFO("BLUFI get wifi status from AP,[%d]\n",__LINE__);

        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        BLUFI_INFO("blufi close a gatt connection");
        esp_blufi_close(ixe_server_if, ixe_conn_id);
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA BSSID %s,[%d]\n", sta_config.sta.bssid,__LINE__);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA SSID %s,[%d]\n", sta_config.sta.ssid,__LINE__);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA PASSWORD %s,[%d]\n", sta_config.sta.password,__LINE__);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        strncpy((char *)ap_config.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        ap_config.ap.ssid[param->softap_ssid.ssid_len] = '\0';
        ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP SSID %s, ssid len %d\n", ap_config.ap.ssid, ap_config.ap.ssid_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        strncpy((char *)ap_config.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        ap_config.ap.password[param->softap_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP PASSWORD %s len = %d\n", ap_config.ap.password, param->softap_passwd.passwd_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        if (param->softap_max_conn_num.max_conn_num > 4) {
            return;
        }
        ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP MAX CONN NUM %d\n", ap_config.ap.max_connection);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
            return;
        }
        ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP AUTH MODE %d\n", ap_config.ap.authmode);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        if (param->softap_channel.channel > 13) {
            return;
        }
        ap_config.ap.channel = param->softap_channel.channel;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP CHANNEL %d\n", ap_config.ap.channel);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
		BLUFI_INFO("[%d]Get esp32 wifi list,disconnect ap\n",__LINE__);
		x_datas.wifi_discon = 1;
        esp_wifi_disconnect();
		sleep(1);
        wifi_scan_config_t scanConf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
		ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        //BLUFI_INFO("[%d] Recv blufi Custom Data %d,\n",__LINE__,param->custom_data.data_len);
        esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
		uint8_t  send_buf[BLUFI_SEND_LENGTH] = {0};
        uint8_t  send_len = 0;
		ixe_recv_command_handler(param->custom_data.data,param->custom_data.data_len,send_buf,&send_len);
		esp_blufi_send_custom_data(send_buf, send_len);
        break;
	case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;;
	case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

static void ixe_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&ixe_ble_adv_params);
        break;
    default:
        break;
    }
}


static esp_blufi_callbacks_t ixe_blufi_callbacks = {
    .event_cb = ixe_blufi_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

void ixe_blufi_start()
{
    esp_err_t ret;
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        BLUFI_ERROR("%s initialize bt controller failed: %s\n", __func__, esp_err_to_name(ret));
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        BLUFI_ERROR("%s enable bt controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        BLUFI_ERROR("%s init bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        BLUFI_ERROR("%s init bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    BLUFI_INFO("BD ADDR: "ESP_BD_ADDR_STR"\n", ESP_BD_ADDR_HEX(esp_bt_dev_get_address()));

    //BLUFI_INFO("BLUFI VERSION %04x\n", esp_blufi_get_version());

    ret = esp_ble_gap_register_callback(ixe_gap_event_handler);
    if(ret){
        BLUFI_ERROR("%s gap register failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_blufi_register_callbacks(&ixe_blufi_callbacks);
    if(ret){
        BLUFI_ERROR("%s blufi register failed, error code = %x\n", __func__, ret);
        return;
    }

    esp_blufi_profile_init();

	ixe_blufi_started = 1;

}

esp_err_t ixe_blufi_stop(void)
{
    esp_err_t err;
    BLUFI_INFO("Free mem at start of simple_ble_stop %d", esp_get_free_heap_size());
	err = esp_blufi_profile_deinit();
	if (err != ESP_OK) {
        return ESP_FAIL;
    }
    err = esp_bluedroid_disable();
    if (err != ESP_OK) {
        return ESP_FAIL;
    }
    BLUFI_INFO("esp_bluedroid_disable called successfully");
    err = esp_bluedroid_deinit();
    if (err != ESP_OK) {
        return err;
    }
    BLUFI_INFO("esp_bluedroid_deinit called successfully");
    err = esp_bt_controller_disable();
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    /* The API `esp_bt_controller_deinit` will have to be removed when we add support for
     * `reset to provisioning`
     */
    BLUFI_INFO("esp_bt_controller_disable called successfully");
    err = esp_bt_controller_deinit();
    if (err != ESP_OK) {
        return ESP_FAIL;
    }
    BLUFI_INFO("esp_bt_controller_deinit called successfully");
    ixe_blufi_started = 0;
    BLUFI_INFO("Free mem at end of simple_ble_stop %d", esp_get_free_heap_size());
    return ESP_OK;
}


static void ixe_initialize_sntp(void)
{
	BLUFI_INFO( "[%s:%d]Initializing SNTP", __FILE__,__LINE__);
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp1.aliyun.com");
	sntp_setservername(1, "210.72.145.44");		// 国家授时中心服务器 IP 地址
    sntp_setservername(2, "1.cn.pool.ntp.org");        

	sntp_init();
}

void ixe_sntp_task(void *param) 
{
  
  struct tm   timeinfo = { 0 };
  char        strftime_buf[64];
  time_t      now = 0;
  int         i = 0;
  int         retry = 0;
  
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  ixe_initialize_sntp();
  
  // set timezone to China Standard Time
  //setenv("TZ", "GMT-8", 1);
  setenv("TZ", &x_wifi.dev_zone[0], 1);
  tzset();
  BLUFI_INFO("Device time zone : %s\n",x_wifi.dev_zone);

  while(1)
 { 
	 if(x_datas.wifi_con == false)
	 {
	   vTaskDelay(1000 / portTICK_PERIOD_MS);
	   continue;
	 }
     // wait for time to be set    
     while (timeinfo.tm_year < (2021 - 1900))
    {
       vTaskDelay(3000 / portTICK_PERIOD_MS);
       if(x_datas.wifi_con == false)
	     continue;
	   if(++retry >= 20)
	  {
		 BLUFI_ERROR("[%d]...wifi connect enthenet failed!....\n",__LINE__);
		 //break;
	   }
	   //BLUFI_INFO("[%s:%d]Waiting for system time to be set... (%d) %d", __FILE__,__LINE__,retry,timeinfo.tm_year);
	   time(&now);
	   localtime_r(&now, &timeinfo);
	   //BLUFI_INFO("The number of seconds since January 1, 1970 is %ld",now);
     }
	 
     //2021-01-07 12:00:00 
     //Thu Jan 14 07:44:10 2021
     strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
     BLUFI_INFO("The current date/time in Shanghai is: %s", strftime_buf);
     //sntp_stop();
     for(i=0;i<3600;i++)
     {
       vTaskDelay(1000 / portTICK_PERIOD_MS);
	   if(x_datas.wifi_con == false)
	   	 break;
	 }
   }
  
}

