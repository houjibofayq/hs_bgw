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

#include "esp_ota_ops.h"
#include "esp_partition.h"


#include "bgs_main.h"
#include "ixe_blufi_command.h"
#include "ixe_blufi_security.h"

#include "ixe_params.h"
#include "ixe_ble_ota.h"


extern  IxeBle                    x_ble;
extern  IxeWifi                   x_wifi;
extern  IxeData                   x_datas;

static const char *TAG = "IXE_BLUFY_COMMAND";

int ixe_get_time(uint8_t* hour,uint8_t* min,uint8_t* sec)
{
  time_t now; 
  time(&now);
  BLUFI_INFO("The number of seconds since January 1, 1970 is %ld",now);
  struct tm tms = { 0 };
  localtime_r(&now, &tms);
  *hour = tms.tm_hour;
  *min = tms.tm_min;
  *sec = tms.tm_sec;
  BLUFI_INFO("Get system time:%4d-%02d-%02d,%02d:%02d:%02d",tms.tm_year+1900,tms.tm_mon+1,tms.tm_mday,tms.tm_hour,tms.tm_min,tms.tm_sec);
  return 0;
}

int ixe_set_time(uint8_t hour,uint8_t min,uint8_t sec)
{
  time_t now; 
  time(&now);
  //BLUFI_INFO("The number of seconds since January 1, 1970 is %ld",now);
  struct tm tms = { 0 };
  localtime_r(&now, &tms);
  //BLUFI_INFO("Get system time:%4d-%2d-%2d,%2d:%2d:%2d",tms.tm_year+1900,tms.tm_mon+1,tms.tm_mday,tms.tm_hour,tms.tm_min,tms.tm_sec);
  tms.tm_hour = hour;
  tms.tm_min = min;
  tms.tm_sec = sec;

  time_t timep;
  timep = mktime(&tms);
  struct timeval tv;
  tv.tv_sec = timep; 
  //printf("tv_sec; %ld\n", tv.tv_sec);
  settimeofday(&tv,NULL);
  time(&now);
  //BLUFI_INFO("The number of seconds since January 1, 1970 is %ld",now);
  return 0;
}


static void ixe_resp_status(uint8_t *send_buf,uint8_t *send_len)
{

  send_buf[0] = IXE_QUERY_RESP;
  send_buf[1] = IXE_QUERY_STATUS;
  send_buf[2] = x_wifi.wifi_onoff;
  send_buf[3] = x_datas.wifi_con;
  send_buf[4] = x_datas.ble_con;
  send_buf[5] = x_datas.status;
  send_buf[6] = 0x01;//new,have ble_key
  *send_len = 7;
  
  return;
}

static void ixe_resp_AP(uint8_t *send_buf,uint8_t *send_len)
{
 
  send_buf[0] = IXE_QUERY_RESP;
  send_buf[1] = IXE_QUERY_AP;
  
  ESP_LOGI(TAG,"[%d]AP ssid:%s ,len = %d\n",__LINE__,x_wifi.sta_ssid,x_wifi.ssid_len);
  if(x_wifi.ssid_len > 0)
  {
    memcpy(&send_buf[2],(char*)&x_wifi.sta_ssid,x_wifi.ssid_len);
    *send_len = x_wifi.ssid_len + 2;
  }else{
    send_buf[2] = 0x00;
	*send_len = 3;
  }

  return;
}

static void ixe_resp_time(uint8_t *send_buf,uint8_t *send_len)
{
 
  send_buf[0] = IXE_QUERY_RESP;
  send_buf[1] = IXE_QUERY_TIME;
   uint8_t  hour;
   uint8_t  min;
   uint8_t  sec;

  ixe_get_time(&hour,&min,&sec);
  
  ESP_LOGI(TAG,"[%d]APP get time: %02d:%02d:%02d \n",__LINE__,hour,min,sec);
  send_buf[2] = hour;
  send_buf[3] = min;
  send_buf[4] = sec;

  *send_len = 5;

  return;
}

static void ixe_resp_key(uint8_t *send_buf,uint8_t *send_len)
{
 
  send_buf[0] = IXE_QUERY_RESP;
  send_buf[1] = IXE_QUERY_BLE_KEY;
  
  ESP_LOGI(TAG,"[%d]device key:%s ,len = %d\n",__LINE__,x_ble.ble_key,BLE_KEY_LEN);
  
  memcpy(&send_buf[2],(char*)&x_ble.ble_key,BLE_KEY_LEN);
  *send_len = BLE_KEY_LEN + 2;
  
  return;
}

static void ixe_resp_firmware(uint8_t *send_buf,uint8_t *send_len)
{
  uint8_t  ver_len = 0;
  send_buf[0] = IXE_QUERY_RESP;
  send_buf[1] = IXE_QUERY_FIRMWARE;
  
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_app_desc_t running_app_info;
  if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
  	  ver_len = strlen(running_app_info.version);  
	  ESP_LOGI(TAG, "Running firmware version: %s,length = %d\n", running_app_info.version,ver_len);
  }
  
  memcpy(&send_buf[2],running_app_info.version,ver_len);
  *send_len = ver_len + 2;
  
  return;
}


static void ixe_resp_set(uint8_t set_type,uint8_t set_result,uint8_t *send_buf,uint8_t *send_len)
{
  send_buf[0] = IXE_SET_ACK;
  send_buf[1] = set_type;
  send_buf[2] = set_result;
  *send_len = 3;
  ESP_LOGI(TAG,"[%s:%d]set_type:%d ,result = %d\n",__FILE__,__LINE__,set_type,set_result);
  
  return;
}

static uint8_t ixe_handle_set_wifi(uint8_t *data)
{
  uint8_t OnOff;
  uint8_t change_flag = 0;

  
  OnOff = data[0];
  ESP_LOGI(TAG,"Ble set, wifi onoff = %d\r\n",OnOff);
  if(x_wifi.wifi_onoff == OnOff)
  	return 1;

  switch(OnOff){

        case 0x00:
			      //x_datas.restart = 0x01;
				  //xmqtt_set_offline(OFFLINE_CLOSE_WIFI);
	              change_flag = 1;
				  break;
		case 0x01:
				  ESP_ERROR_CHECK( esp_wifi_start() );
				  break;
		case 0x02:
				  esp_wifi_connect();
	              change_flag = 1;
				  OnOff = 1;
				  break;
	    case 0x03:
	              change_flag = 1;
				  OnOff = 1;
				  break;
		  default:
		  	      return 2;
  }
  
  if(change_flag)
  {
	 x_wifi.wifi_onoff = OnOff;
	 ixe_save_wifi_params();
  }
  
  return 1;
}

static uint8_t ixe_handle_reset_factory(uint8_t *data)
{
   uint8_t         reset_flag;
   nvs_handle_t    my_handle;
   esp_err_t       err;

  reset_flag = data[0];
  ESP_LOGI(TAG,"reset_factory =%d\r\n",reset_flag);
  if(reset_flag == 0x01)
  {	 
     err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
     if (err != ESP_OK) return 2;
	 nvs_erase_key(my_handle,"ixe_wifi");
	 nvs_erase_key(my_handle,"user_params");
	 x_datas.restart = 0x01; 
	 esp_wifi_stop();
	 esp_wifi_restore();
  }else
     return 2;
  
  return 1;
}

static uint8_t ixe_handle_set_ota(uint8_t *data)
{
   uint8_t         set_flag;

  set_flag = data[0];
  ESP_LOGI(TAG,"Ble set, set_ota =%d\r\n",set_flag);
  if(set_flag == 1)
  {
    //xota_set_status(UPDATE_START);
  }else
    return 2;
  
  return 1;
}

static uint8_t ixe_handle_set_time(uint8_t *data)
{
   uint8_t    hour;
   uint8_t    min;
   uint8_t    sec;
   
  hour = data[0];
  min  = data[1];
  sec = data[2];
  ESP_LOGI(TAG,"app set time, %d:%d:%d\r\n",hour,min,sec);
  if(hour>= 24 || min>= 60 || sec >= 60)
    return 2;
  ixe_set_time(hour,min,sec);
  
  return 1;
}

static uint8_t ixe_handle_set_zone(uint8_t *data)
{
  uint8_t    hour;
  uint8_t    min;
  uint8_t    sign;

  sign = data[0];
  hour = data[1];
  min  = data[2];
  if(hour> 12 && min >59)
  {
    return 2;
  }
  memset(x_wifi.dev_zone,0x00,16);
  if(sign)
  {
    sprintf(x_wifi.dev_zone,"GMT-%d:%02d",hour,min);
  }else{
    sprintf(x_wifi.dev_zone,"GMT+%d:%02d",hour,min);
  }
  ESP_LOGI(TAG,"app set zone, %s\n",x_wifi.dev_zone);

  ixe_save_wifi_params();
  x_datas.restart = 0x01; 
  return 1;
}

static uint8_t ixe_handle_set_rallback(uint8_t *data)
{
   uint8_t         set_flag;

  set_flag = data[0];
  ESP_LOGI(TAG,"Ble set, set_rallback =%d\r\n",set_flag);
  if(set_flag == 1)
  {
    //xota_set_status(UPDATE_RALLBACK);
  }else
    return 2;
  
  return 1;
}


static uint8_t ixe_handle_reset_ixe(uint8_t *data)
{
   uint8_t         reset_flag;
   nvs_handle_t    my_handle;
   esp_err_t       err;

  reset_flag = data[0];
  ESP_LOGI(TAG,"Ble set, reset_ixe =%d\r\n",reset_flag);
  if(reset_flag == 0x01)
  {	 
     err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);
     if (err != ESP_OK) return 2;
     nvs_erase_key(my_handle,"ixe_ble");
	 nvs_erase_key(my_handle,"ixe_wifi");
	 nvs_erase_key(my_handle,"user_params");
	 x_datas.restart = 0x01; 
  }else
     return 2;
  
  return 1;
}

static int ixe_handle_set_ble_name(uint8_t *data,uint16_t len)
{
  ESP_LOGI(TAG,"Ble set, ble name = %s, len=%d\r\n",(char*)data,len);	  
  if(len > 0 && len < 16)
  {
    memset(x_ble.ble_name,0x00,16);
    memcpy(x_ble.ble_name,(char*)data,len);
  }else
    return 2;
  
  return 1;
}

static int ixe_handle_set_ble_key(uint8_t *data,uint16_t len)
{
  ESP_LOGI(TAG,"Ble set, ble key = %s, len=%d\r\n",(char*)data,len);	  
  if((len == BLE_KEY_LEN) || (len == 6))
  {
    memset(x_ble.ble_key,0x00,BLE_KEY_LEN);
    memcpy(x_ble.ble_key,(char*)data,len);
	ixe_save_ble_params();
	x_datas.restart = 0x01;  
  }else
    return 2;
  
  return 1;
}


static int ixe_handle_set_ble_ota(uint8_t *data,uint16_t len)
{
  uint8_t         ota_flag;
  
  ota_flag = data[0];
  ESP_LOGI(TAG,"Ble set, ota_ixe =%d\r\n",ota_flag);
  if(ota_flag == 0x01)
  {	 
	ixe_set_ble_ota(1);
  }else
     return 2;
  
  return 1;
}


void ixe_recv_command_handler(uint8_t *recv_data,uint32_t recv_len,uint8_t *send_buf,uint8_t *send_len)
{
  uint8_t  main_type;
  uint8_t  sub_type;
  uint8_t  *data;
  uint8_t  len;
  uint8_t  ret = 0;

  
  if(NULL == recv_data || recv_len<2)
  {
    return;
  }
  main_type = recv_data[0];
  sub_type  = recv_data[1];
  data = &recv_data[2];
  len = recv_len -2;
  ESP_LOGI(TAG,"[%d]ixe_recv_app_data,len = %d ,cmd type:%02x %02x\n",__LINE__,recv_len,main_type,sub_type);

  memset(send_buf,0x00,BLUFI_SEND_LENGTH);
  //query command
  if(main_type == IXE_QUERY_CMD)
  {
    switch(sub_type)
    {
      case IXE_QUERY_STATUS:
	  	     ixe_resp_status(send_buf,send_len);
	  	     break;
	  case IXE_QUERY_AP:
	  	     ixe_resp_AP(send_buf,send_len);
	  	     break;
	  case IXE_QUERY_TIME:
	  	     ixe_resp_time(send_buf,send_len);
	  	     break;
	  case IXE_QUERY_BLE_KEY:
	  	     ixe_resp_key(send_buf,send_len);
	  	     break;
	  case IXE_QUERY_FIRMWARE:
	  	     ixe_resp_firmware(send_buf,send_len);
	  	     break;
	  default:
	  	     break;
	}
	return;
  }
  //set command
  if(main_type == IXE_SET_CMD)
  {
    switch(sub_type)
    {
	  case IXE_SET_WIFI_OnOff:
			 ret = ixe_handle_set_wifi(data);
	  	     break;
	  case IXE_RESET_PARAM:
			 ret = ixe_handle_reset_factory(data);
	  	     break;
	  case IXE_SET_OTA:
			 ret = ixe_handle_set_ota(data);
	  	     break;
	  case IXE_SET_TIME:
			 ret = ixe_handle_set_time(data);
	  	     break;
	  case IXE_SET_ZONE:
			 ret = ixe_handle_set_zone(data);
	  	     break;
	  case IXE_SET_RALLBACK:
			 ret = ixe_handle_set_rallback(data);
	  	     break;
	  case IXE_RESET_PROGRAME:
			 ret = ixe_handle_reset_ixe(data);
	  	     break;
	  case IXE_SET_BLE_NAME:
			 ret = ixe_handle_set_ble_name(data,len);
	  	     break;
	  case IXE_SET_BLE_KEY:
			 ret = ixe_handle_set_ble_key(data,len);
	  	     break;
	  case IXE_SET_BLE_OTA:
			 ret = ixe_handle_set_ble_ota(data,len);
	  	     break;
	  default:
	  	     ret = 2;
	  	     break;
	 }
	ixe_resp_set(sub_type,ret,send_buf,send_len);
	return;
   }
  
    #ifdef PRODUCT_ISE
	  if(main_type == ISE_CMD_RECV)
	  {
	    ixe_product_comd_handler(recv_data,recv_len,send_buf,send_len);
	  }
	  return;
	#endif  
  
  return;
}

