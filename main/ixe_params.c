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

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "ixe_params.h"
#include "ixe_blufi_security.h"

#define  TAG "IXE_PARAMS"

   IxeBle           x_ble;
   IxeWifi          x_wifi;
   IxeData          x_datas;


const char *ixe_ble_name_get(void)
{
    static const char *name = NULL;
    
    name = x_ble.ble_name;
    return name;
}
const char *ixe_ble_key_get(void)
{
    static const char *key = NULL;
    
    key = x_ble.ble_key;
    return key;
}


uint8_t ixe_reset_factory(void)
{
   nvs_handle_t    my_handle;
   esp_err_t       err;

   BLUFI_ERROR("[%d]...ixe reset factory ....\n",__LINE__);
   err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
   if (err != ESP_OK) return 2;
   nvs_erase_key(my_handle,"ixe_wifi");
   nvs_erase_key(my_handle,"user_params");
   if(x_wifi.wifi_onoff == 1)
   {
     BLUFI_ERROR("[%d]...ixe wifi restore ....\n",__LINE__);
     //esp_wifi_disconnect();
     esp_wifi_restore();
   }
   x_datas.restart = 0x01; 
  
  return 1;
}


void ixe_set_default_ble_params()
{
  memset(&x_ble,0x00,sizeof(IxeBle));
  #if (defined PRODUCT_BGS)
     strcpy(x_ble.ble_name,DEFAULT_BLE_DEVICE_NAME);
  #endif
  strcpy(x_ble.ble_key,DEFAULT_BLE_DEVICE_KEY);
}

void ixe_set_default_wifi_params()
{
  memset((void*)&x_wifi,0x00,sizeof(IxeWifi));
  x_wifi.wifi_onoff = DEFAULT_WIFI_ONOFF;
  x_wifi.ssid_len= 0;
  strcpy(x_wifi.dev_zone,DEFAULT_DEVICE_ZONE);
}

esp_err_t ixe_save_ble_params(void)
{
	nvs_handle_t	my_handle;
	esp_err_t	   err;
	
	
	err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
	if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle, "ixe_ble", &x_ble,sizeof(IxeBle));
    ESP_LOGI(TAG,"%s",(err != ESP_OK) ? "***save ble param Failed!\n" : "***save ble param Done\n");

    err = nvs_commit(my_handle);
    ESP_LOGI(TAG,"%s",(err != ESP_OK) ? "NVS commit Failed!\n" : "NVS commit Done\n");

    nvs_close(my_handle);

	return err;
}

esp_err_t ixe_save_wifi_params(void)
{
	nvs_handle_t	my_handle;
	esp_err_t	    err;
	
	
	err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
	if (err != ESP_OK) return err;
	
    err = nvs_set_blob(my_handle, "ixe_wifi", &x_wifi,sizeof(IxeWifi));
    ESP_LOGI(TAG,"%s",(err != ESP_OK) ? "***save ixe wifi param Failed!\n" : "***save ixe wifi param Done\n");

    err = nvs_commit(my_handle);
    ESP_LOGI(TAG,"%s",(err != ESP_OK) ? "NVS commit Failed!\n" : "NVS commit Done\n");

    nvs_close(my_handle);

	return err;
}


static esp_err_t ixe_read_storage_params(void)
{
   nvs_handle_t    my_handle;
   esp_err_t       err;
   size_t        length;
   
   
   err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
   if (err != ESP_OK) return err;

   length = sizeof(IxeBle);
   err = nvs_get_blob(my_handle, "ixe_ble", &x_ble, &length);
   nvs_close(my_handle);
   switch (err) {
      case ESP_OK:   
				BLUFI_INFO("read ixe_ble length = %d ,ixe_ble length = %d\n",length,sizeof(IxeBle));
				if(length != sizeof(IxeBle))
				{
				  ESP_ERROR_CHECK(nvs_flash_erase());
				  esp_restart();
				}
				BLUFI_INFO("[%d]The ble adv name :%s, ble key:%s!\n",__LINE__,x_ble.ble_name,x_ble.ble_key);
                break;
      case ESP_ERR_NVS_NOT_FOUND:
	  	        BLUFI_INFO("[%d]The factory param is not initialized yet!\n",__LINE__);
				ixe_set_default_ble_params();
				ixe_save_ble_params();
                break;
      default :
                BLUFI_ERROR("[%d]Error (%s) reading!\n",__LINE__,esp_err_to_name(err));
   }	

   
   err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
   if (err != ESP_OK) return err;   
   length = sizeof(IxeWifi);
   err = nvs_get_blob(my_handle, "ixe_wifi",(void*)&x_wifi, &length);
   nvs_close(my_handle);
   switch (err) {
      case ESP_OK:   
				BLUFI_INFO("read params length = %d ,s_params length = %d\n",length,sizeof(IxeWifi));
				BLUFI_INFO("[%d]The s_params,  wifi_onoff = %d\n",__LINE__,x_wifi.wifi_onoff);
                break;
      case ESP_ERR_NVS_NOT_FOUND:
	  	        BLUFI_INFO("[%d]The system param is not initialized yet!\n",__LINE__);
				ixe_set_default_wifi_params();
				ixe_save_wifi_params();
                break;
      default :
                BLUFI_ERROR("[%d]Error (%s) reading!\n",__LINE__,esp_err_to_name(err));
   }
   
   return ESP_OK;
}

void ixe_init_params()
{
   memset(&x_ble,0x00,sizeof(IxeBle));
   memset((void*)&x_wifi,0x00,sizeof(IxeWifi));
   memset((void*)&x_datas,0x00,sizeof(IxeData));
   
   ixe_read_storage_params();
}


