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


#include "bgs_main.h"
#include "ixe_params.h"
#include "bgs_socket_util.h"
#include "bgs_socket_cmd.h"


extern   IxeData          x_datas;


#ifdef   PRODUCT_BGS 

extern volatile IseParam        ise_params;
extern volatile IseData         ise_datas;

static const char *TAG = "bgs_socket_cmd";

void ise_set_default_params()
{  
   memset((void*)&ise_params,0x00,sizeof(IseParam));  
   ise_params.cycle_hours = DEFAULT_RUNNING_CYCLE_HOURS;  
   ise_params.run_cycle = DEFAULT_RUNNING_TYPE;  
   ise_params.pump_go_times = DEFAULT_PUMP_GO_TIMES;  
   ise_params.pump_go_minutes = DEFAULT_PUMP_GO_MINUTES;  
   ise_params.pump_delay_minutes = DEFAULT_PUMP_DELAY_MINUTES;
}

esp_err_t ise_save_params(void)
{	
  nvs_handle_t	my_handle;
  esp_err_t	   err;			
  err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);	
  if (err != ESP_OK) return err;    
  err = nvs_set_blob(my_handle, "user_params", (void*)&ise_params,sizeof(IseParam));    
  ESP_LOGI(TAG,"%s",(err != ESP_OK) ? "***ise save param Failed!\n" : "***ise save user param Done\n");	
     
  err = nvs_commit(my_handle);    
  ESP_LOGI(TAG,"%s",(err != ESP_OK) ? "Failed!\n" : "Done\n");    
  nvs_close(my_handle);	
  return err;
}

esp_err_t ise_read_params(void)
{   

nvs_handle_t    my_handle;   
esp_err_t       err;   
size_t        length;         
err = nvs_open(NVS_PARAMS_NAME, NVS_READWRITE, &my_handle);   
if (err != ESP_OK) return err;   

length = sizeof(IseParam);   
err = nvs_get_blob(my_handle, "user_params", (void*)&ise_params, &length);   
switch (err) 
{      
	case ESP_OK:   								
		        ESP_LOGI(TAG,"[%d] read length = %d ,ise_params length = %d\n",__LINE__,length,sizeof(IseParam));	
		        ESP_LOGI(TAG,"[%d]The pump run type :%d,pump run mins = %d, cycle_hours = %d\n",__LINE__,
					       ise_params.run_cycle,ise_params.pump_go_minutes,ise_params.cycle_hours);                
		        break;      
	case ESP_ERR_NVS_NOT_FOUND:	  	        
		        ESP_LOGI(TAG,"[%d]The system param is not initialized yet!\n",__LINE__);				
				ise_set_default_params();				
				ise_save_params();                
				break;      
	default :                
		        ESP_LOGE(TAG,"[%d]Error (%s) reading!\n",__LINE__,esp_err_to_name(err));   
   }   
   nvs_close(my_handle);      
   return ESP_OK;
}

static void ise_app_query_resp(uint8_t *send_buf,uint8_t *send_len)
{
   uint64_t now_ts = esp_timer_get_time();
   uint32_t  last_watering = 0;
   uint32_t  next_watering = 0;
   uint32_t  run_mins = 0;

  run_mins = (now_ts - ise_datas.start_ts)/(1000*1000*60); 
  ESP_LOGI(TAG,"now = %lld ,satrt = %lld,run_mins = %d",now_ts,ise_datas.start_ts,run_mins); 
  if(ise_datas.pump_last_run_ts)
  {
    last_watering = (now_ts - ise_datas.pump_last_run_ts)/(1000*1000*60);
    ESP_LOGI(TAG,"now = %lld ,last = %lld,last_watering = %d",now_ts,ise_datas.pump_last_run_ts,last_watering);
  }else{
    last_watering = 0;
  }

  if(ise_params.run_cycle)
  {
	  next_watering = ise_datas.pump_next_run_mins;
	  ESP_LOGI(TAG,"next_watering = %lld",ise_datas.pump_next_run_mins);
  }else{
	 next_watering = 0;
  }
   
  send_buf[0] = ISE_CMD_RESP;
  send_buf[1] = ISE_QUERY_PAMS;
  send_buf[2] = x_datas.status;
  send_buf[3] = ise_datas.status;
  send_buf[4] = ise_datas.pump_running;
  send_buf[5] = ise_datas.pump_resean;
  send_buf[6] = ise_params.run_cycle;
  send_buf[7] = ise_params.pump_go_minutes;
  send_buf[8] = ise_params.cycle_hours >> 8;
  send_buf[9] = ise_params.cycle_hours  & 0x00ff;
  send_buf[10] = ise_params.pump_delay_minutes;
  send_buf[11] = ise_datas.pump_last_run_reason;
  send_buf[12] = last_watering/256;
  send_buf[13] = last_watering%256;
  send_buf[14] = next_watering/256;
  send_buf[15] = next_watering%256;
  send_buf[16] = run_mins/256;
  send_buf[17] = run_mins%256;
  
  *send_len = 18;
  
  return;
}

static uint8_t ise_app_set_params(uint8_t *data)
{
  uint8_t   run_minutes = 0;
  uint8_t   delay_minutes = 0;
  uint16_t  cycle_hours = 0;
  uint8_t   save_flag = 0;
  uint8_t   set_type;
  
  set_type = data[0];
  ESP_LOGI(TAG,"Pump running set : running_type =%d\r\n",set_type);
  
  if(set_type == 0)
  {
     if(ise_params.run_cycle != set_type )
    {		
	  ise_params.run_cycle = set_type;
	  ise_datas.params_change = 1;
	  ise_save_params();
    }
	 
  }else if(set_type == 2)
  {
     //bgs_set_pump_once(data[1]);
	 
  }else if(set_type == 1)
  {
    cycle_hours = data[2] << 8 | data[3];
    run_minutes = data[1];
    delay_minutes = data[4];
    ESP_LOGI(TAG,"Ble set :pump go minutes: %d  cycle_time =%d\r\n",run_minutes,cycle_hours);
	if(ise_params.run_cycle != set_type )
    {		
	  ise_params.run_cycle = set_type;
	  save_flag = 1;
    }
    if(ise_params.pump_go_minutes != run_minutes )
    {		
	  ise_params.pump_go_minutes = run_minutes;
	  save_flag = 1;
    }
    if(ise_params.pump_delay_minutes != delay_minutes )
    {		
	  ise_params.pump_delay_minutes = delay_minutes;
	  save_flag = 1;
    }			
  
    if( (data[2] << 8 | data[3])  )
    {
	   if(ise_params.cycle_hours != cycle_hours )
	   {
	     ise_params.cycle_hours = cycle_hours;
	     save_flag = 1;
	   }
    }

    if(save_flag)
    {
      ise_save_params();
	  ise_datas.params_change = 1;
    }
  }
  return 1;
}

void ise_app_set_resp(uint8_t set_type,uint8_t set_result,uint8_t *send_buf,uint8_t *send_len)
{
  send_buf[0] = ISE_CMD_RESP;
  send_buf[1] = set_type;
  send_buf[2] = set_result;
  *send_len = 3;
  ESP_LOGI(TAG,"[%d]set_type:%d ,result = %d\n",__LINE__,set_type,set_result);
  
  return;
}


void ixe_product_comd_handler(uint8_t *recv_data,uint32_t recv_len,uint8_t *send_buf,uint8_t *send_len)
{
  uint8_t  main_type;
  uint8_t  sub_type;
  uint8_t  *data;
  uint8_t  ret = 0;

  
  if(NULL == recv_data || recv_len<2)
  {
    return;
  }
  main_type = recv_data[0];
  sub_type  = recv_data[1];
  data = &recv_data[2];
  
  //query command
  if(main_type == ISE_CMD_RECV)
  {
    switch(sub_type)
    {
      case ISE_QUERY_PAMS:
	  	     ise_app_query_resp(send_buf,send_len);
	  	     break;
	  case ISE_SET_PAMS:
	  	     ret = ise_app_set_params(data);
	  	     ise_app_set_resp(sub_type,ret,send_buf,send_len);
	  	     break;
	  default:
	  	     break;
	}
  }else{
      ise_app_set_resp(sub_type,2,send_buf,send_len);
  }
  return;
}


#endif

