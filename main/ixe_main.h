#ifndef _IXE_MAIN
#define _IXE_MAIN

#define  NVS_PARAMS_NAME         "params"

//#define  DEFAULT_BLE_DEVICE_NAME           "IXE010001"
#define  DEFAULT_WIFI_ONOFF                   0
#define  DEFAULT_DEVICE_ZONE                "GMT-8:00"

#define  BLUFI_SEND_LENGTH                    256

//#define  PRODUCT_ISE                         1
//#define  PRODUCT_ILE                         1
//#define  PRODUCT_IPE                         1

#ifdef    PRODUCT_ISE
#define  DEFAULT_BLE_DEVICE_NAME           "ISE030001"
#endif

#ifdef    PRODUCT_IPE
#define  DEFAULT_BLE_DEVICE_NAME           "IPE010001"
#endif

#define  DEFAULT_BLE_DEVICE_KEY           "hs1997"

#define  BLE_KEY_LEN     16
   
typedef struct _IxeBle{ 
	char     ble_name[16];
	char     ble_key[BLE_KEY_LEN];
	char     ble_mac[12];
	char     buf[20];
} IxeBle; 

typedef struct _IxeParam
{ 
   uint8_t    wifi_onoff;  
   uint8_t    ssid_len;
   uint8_t    sta_bssid[6];
   char       buf[8];
   char       dev_zone[16];
   char       sta_ssid[32];
}IxeParam; 


typedef struct _IxeData{
	uint8_t    status;  
	uint8_t    dev_status; 
	uint8_t    wifi_con;
	uint8_t    wifi_discon;
	uint8_t    ble_con;
	uint8_t    restart;
}IxeData;

enum ixe_mqtt_status{
	XMQTT_NOT_INIT = 0,
    XMQTT_INIT_OK,
    XMQTT_PUBLISH_OK,
    XMQTT_DISCONNECT,
    XMQTT_RESTART
};

enum ixe_mqtt_offline{
	OFFLINE_NO_TYPE = 0,
    OFFLINE_RESET_KEY,
    OFFLINE_RECOERTY_KEY,
    OFFLINE_CLOSE_WIFI,
    OFFLINE_OTA_OK,
    OFFLINE_OTA_FAULT,
};

int ixe_get_time(uint8_t* hour,uint8_t* min,uint8_t* sec);
int ixe_set_time(uint8_t hour,uint8_t min,uint8_t sec);



uint8_t ixe_reset_factory(void);
esp_err_t ixe_wifi_connect();
void ixe_ble_start();
esp_err_t ixe_ble_stop(void);



#endif
