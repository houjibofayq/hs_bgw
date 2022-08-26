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


#include "driver/timer.h"
#include "esp_timer.h"


#include "driver/gpio.h"
#include "driver/uart.h"

#include "bgs_main.h"
#include "ixe_params.h"

#include "bgs_socket_util.h"
#include "bgs_socket_cmd.h"
#include "driver/adc.h"
//#include "esp_adc_cal.h"
 
static const char *TAG = "bgs_socket_util";


#define GPIO_OUTPUT_SOCKET_EN      26
#define GPIO_OUTPUT_LED_RED        27
#define GPIO_OUTPUT_LED_GREEN      4

#define GPIO_OUTPUT_LED_MASK  ((1ULL<<GPIO_OUTPUT_LED_RED) | (1ULL<<GPIO_OUTPUT_LED_GREEN) | (1ULL<<GPIO_OUTPUT_SOCKET_EN))


#define GPIO_INPUT_RESET_KEY      25

#define GPIO_INPUT_PIN_SEL     (1ULL<<GPIO_INPUT_RESET_KEY)

#define ESP_INTR_FLAG_DEFAULT 0


volatile uint8_t    led_green = 0;
volatile uint8_t    led_red = 0;

volatile int8_t    pump_run_once = 0;


extern   IxeWifi         x_wifi;
extern   IxeData         x_datas;
extern   IxeBle          x_ble;

volatile IseParam        ise_params;
volatile IseData         ise_datas;



void bgs_led_on(gpio_num_t gpio_num)
{
  gpio_set_level(gpio_num, 1);
}

void bgs_led_off(gpio_num_t gpio_num)
{
  gpio_set_level(gpio_num, 0);
}

void bgs_socket_on(void)
{
  gpio_set_level(GPIO_OUTPUT_SOCKET_EN, 1);
  ise_datas.pump_running = 0x01;
}

void bgs_socket_off(void)
{
  gpio_set_level(GPIO_OUTPUT_SOCKET_EN, 0);
  ise_datas.pump_running = 0x00;
}

#define  BGS_LED_TASK_DELAY   (100 / portTICK_RATE_MS)
void bgs_led_display(uint8_t LedOn_Cnt,uint8_t LedOff_Cnt,gpio_num_t gpio_num)
{
  static uint8_t cnt = 0;
  if(++cnt >= LedOff_Cnt)
  {
    cnt = 0;
    bgs_led_on(gpio_num);
	vTaskDelay(LedOn_Cnt* BGS_LED_TASK_DELAY);
    bgs_led_off(gpio_num);
  }
}

void bgs_led_blink_slow(gpio_num_t gpio_num)
{
  uint8_t On = 2;
  uint8_t Off = 10;
  bgs_led_display(On,Off,gpio_num);
}

void bgs_led_blink_quick(gpio_num_t gpio_num)
{
  uint8_t On = 1;
  uint8_t Off = 1;
  bgs_led_display(On,Off,gpio_num);
}


static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR bgs_gpio_isr_handler(void* arg)
{    
   uint32_t gpio_num = (uint32_t) arg;    
   xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}


static void bgs_gpio_init()
{

  gpio_config_t io_conf;    
  //disable interrupt    
  io_conf.intr_type = GPIO_PIN_INTR_DISABLE;    
  //set as output mode    
  io_conf.mode = GPIO_MODE_OUTPUT;    
  //bit mask of the pins that you want to set,e.g.GPIO18/19    
  io_conf.pin_bit_mask = GPIO_OUTPUT_LED_MASK;    
  //disable pull-down mode    
  io_conf.pull_down_en = 0;    
  //disable pull-up mode    
  io_conf.pull_up_en = 0;    
  //configure GPIO with the given settings    
  gpio_config(&io_conf);
  //gpio init,set led off
  gpio_set_level(GPIO_OUTPUT_LED_RED, 1);
  gpio_set_level(GPIO_OUTPUT_LED_GREEN, 1);
  gpio_set_level(GPIO_OUTPUT_SOCKET_EN, 0);
  //BLUFI_ERROR("[%s:%d]...Test flag ....\n", __FILE__,__LINE__);
  
  //interrupt of rising edge    
  io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;    
  //bit mask of the pins, use GPIO4/5 here    
  io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;    
  //set as input mode        
  io_conf.mode = GPIO_MODE_INPUT;    
  //enable pull-up mode    
  io_conf.pull_up_en = 1;    
  gpio_config(&io_conf);

  //change gpio intrrupt type for one pin
  gpio_set_intr_type(GPIO_INPUT_RESET_KEY, GPIO_INTR_ANYEDGE);
 
  //create a queue to handle gpio event from isr
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  
  //install gpio isr service
  gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
  //hook isr handler for specific gpio pin
  gpio_isr_handler_add(GPIO_INPUT_RESET_KEY, bgs_gpio_isr_handler, (void*) GPIO_INPUT_RESET_KEY);
  
}

static uint8_t bgs_set_leds_status(void)
{
   uint8_t  value = 0;
   
  if(x_datas.ble_con == 1)
    return GREEN_BLE_CONING;
  
  if(x_datas.status == 0)
    value = GREEN_BLE_NOCON;
  else if(x_datas.status == 1)
	value = RED_WIFI_CONFAULT;
  else if(x_datas.status >= 0x10)
    return  GREEN_SYS_UPDATING;
   
   return value;
}

typedef enum {
	KEY_SHORT_PRESS = 1, 
	KEY_LONG_PRESS,
} alink_key_t;

esp_err_t bgs_key_scan(TickType_t ticks_to_wait,uint32_t *io_num) 
{
	BaseType_t press_key = pdFALSE;
	BaseType_t lift_key = pdFALSE;
	int backup_time = 0;
	
    //ESP_LOGI(TAG,"[%d]...ESP key scan start ....\n",__LINE__);
	while (1) {

		//接收从消息队列发来的消息
		xQueueReceive(gpio_evt_queue, io_num, ticks_to_wait);
        //ESP_LOGI(TAG,"[%d]...ESP key %d pressed ....\n",__LINE__,*io_num);
		//记录下用户按下按键的时间点
		if (gpio_get_level(*io_num) == 0) {
			press_key = pdTRUE;
			backup_time = esp_timer_get_time();
			//如果当前GPIO口的电平已经记录为按下，则开始减去上次按下按键的时间点
		} else if (press_key) {
			//记录抬升时间点
			lift_key = pdTRUE;
			backup_time = esp_timer_get_time() - backup_time;
		}

		//当按下标志位和按键弹起标志位都为1时候，才执行回调
		if (press_key & lift_key) {
			press_key = pdFALSE;
			lift_key = pdFALSE;

			//如果大于1s则回调长按，否则就短按回调
			if (backup_time > 2000000) {
				return KEY_LONG_PRESS;
			} else {
				return KEY_SHORT_PRESS;
			}
		}
	}
}

void bgs_key_trigger(void *arg) 
{
	esp_err_t ret = 0;
    uint32_t io_num;
	
	while (1) {
		ret = bgs_key_scan(portMAX_DELAY,&io_num);
		if(io_num == GPIO_INPUT_RESET_KEY){
		  switch (ret) 
		  {
		    case KEY_SHORT_PRESS:
			       ESP_LOGI(TAG,"[%d]...ESP Restart ....\n",__LINE__);
				   esp_restart();
			       break;
		    case KEY_LONG_PRESS:
			      ESP_LOGI(TAG,"[%d]...ESP Reset user params ....\n",__LINE__);
			      sleep(2);
			      ixe_reset_factory();
				  esp_restart();
			      break;
		    default:
			       break;
		  }
	  }
	  
	}

}


void bgs_led_key_task(void* arg)
{
  
  uint8_t    leds_val = 0;

  bgs_gpio_init();
  xTaskCreate(bgs_key_trigger, "key_trigger", 1024 * 2, NULL, 10,NULL);
  while(1)
  {  
    vTaskDelay(BGS_LED_TASK_DELAY);
    // led display
    leds_val = bgs_set_leds_status();
	switch(leds_val)
	{
       case SYSTEM_STARTING:
	   	                    break;
	   case GREEN_BLE_NOCON:
	   	                    bgs_led_off(GPIO_OUTPUT_LED_RED);
							bgs_led_blink_slow(GPIO_OUTPUT_LED_GREEN);
	   	                    break;
	   case GREEN_BLE_CONING:
	   	                    bgs_led_off(GPIO_OUTPUT_LED_RED);
							bgs_led_on(GPIO_OUTPUT_LED_GREEN);
							break;
	   case GREEN_SYS_UPDATING:
						   bgs_led_off(GPIO_OUTPUT_LED_RED);
						   bgs_led_blink_quick(GPIO_OUTPUT_LED_GREEN);
						   break;
	   case RED_WIFI_CONFAULT:
						   bgs_led_off(GPIO_OUTPUT_LED_GREEN);
						   bgs_led_blink_slow(GPIO_OUTPUT_LED_RED);
						   break;
	   
	              default:
						  break;
	  }
	}
}


#if 1
void test_timer_periodic_cb(void *arg); 

esp_timer_handle_t test_p_handle = 0;
static uint64_t timer_cnts = 0;

//定义一个周期重复运行的定时器结构体
esp_timer_create_args_t test_periodic_arg = { 
         .callback = &test_timer_periodic_cb, //设置回调函数		
         .arg = NULL, //不携带参数		
         .name = "Ise Timer" //定时器名字		
};

void bgs_timer_start()
{
  esp_err_t err = esp_timer_create(&test_periodic_arg, &test_p_handle);	
  err = esp_timer_start_periodic(test_p_handle, 10*1000 * 1000);	
  ESP_LOGI(TAG,"ise periodic time ctreate %s", err == ESP_OK ? "ok!\r\n" : "failed!\r\n");
}
void bgs_timer_clear()
{ 
  esp_err_t err = esp_timer_stop(test_p_handle);	  
  ESP_LOGI(TAG,"%s stop %s", test_periodic_arg.name,err == ESP_OK ? "ok!\r\n" : "failed!\r\n"); 	  
  err = esp_timer_delete(test_p_handle);		  
  ESP_LOGI(TAG,"\%s delete %s", test_periodic_arg.name,err == ESP_OK ? "ok!\r\n" : "failed!\r\n");

}
void test_timer_periodic_cb(void *arg) 
{
  timer_cnts ++;
  ise_datas.pump_next_run_mins = ise_params.cycle_hours*60 - timer_cnts/6;
  if(timer_cnts >= ise_params.cycle_hours*60*6)
  {
	 ise_datas.params_change = 1; 
  }
}
#endif

void bgs_data_init(void)
{
   
}
void bgs_util_socket_task(void* arg)
{
 
  uint8_t   bgs_timer_flag = 0;
 
  while(1)
  {
    sleep(1);
  }
}



