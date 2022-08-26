#ifndef _IXE_PARAMS
#define _IXE_PARAMS


#define  NVS_PARAMS_NAME         "params"

//#define  DEFAULT_BLE_DEVICE_NAME           "IXE010001"
#define  DEFAULT_WIFI_ONOFF                   0
#define  DEFAULT_DEVICE_ZONE                "GMT-8:00"

#define  BLUFI_SEND_LENGTH                    256

#define  PRODUCT_BGS                       1

#ifdef    PRODUCT_BGS
//#define  DEFAULT_BLE_DEVICE_NAME           "BGS010001"
#define  DEFAULT_BLE_DEVICE_NAME           "ISE030007"

#endif

#define  DEFAULT_BLE_DEVICE_KEY            "hs1997"

#define  BLE_KEY_LEN     16
   
typedef struct _IxeBle{ 
	char     ble_name[16];
	char     ble_key[BLE_KEY_LEN];
	char     ble_mac[12];
	char     buf[20];
} IxeBle; 

typedef struct _IxeWifi
{ 
   uint8_t    wifi_onoff;  
   uint8_t    ssid_len;
   uint8_t    sta_bssid[6];
   char       buf[8];
   char       dev_zone[16];
   char       sta_ssid[32];
}IxeWifi; 

typedef struct _IxeData{
	uint8_t    status;  
	uint8_t    dev_status; 
	uint8_t    wifi_con;
	uint8_t    wifi_discon;
	uint8_t    ble_con;
	uint8_t    restart;
}IxeData;


const char *ixe_ble_name_get(void);
const char *ixe_ble_key_get(void);


esp_err_t ixe_save_ble_params(void);
esp_err_t ixe_save_wifi_params(void);

void ixe_init_params();



#endif
