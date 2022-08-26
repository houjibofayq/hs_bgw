#ifndef _IXE_ISE_UTIL
#define _IXE_ISE_UTIL


#define DEFAULT_RUNNING_CYCLE_HOURS          24
#define DEFAULT_RUNNING_TYPE                 0
#define DEFAULT_PUMP_GO_TIMES                3
#define DEFAULT_PUMP_GO_MINUTES              3
#define DEFAULT_PUMP_DELAY_MINUTES           0


#define   INTERVAL                 1
#define   CONTINUE                2
#define   PUMP_ONCE_RUN_MINTUES   3


#define ISE_POWER_IN_TEST                      1
#define ISE_TANK_WATER_TEST                    1


enum
{
	SYSTEM_STARTING = 0,
	GREEN_BLE_NOCON,
	GREEN_BLE_CONING,
	GREEN_SYS_UPDATING,
	GREEN_END
}LED_GREEN;

enum
{
	RED_WIFI_CONFAULT = 4,
	RED_SYSTEM_END
}LED_RED;


typedef struct IseParam
{ 
	uint16_t   cycle_hours;
	uint16_t   pump_go_times;
	uint16_t   pump_go_minutes;
	uint16_t   pump_delay_minutes;
	uint8_t    run_cycle;
	uint8_t    buf[23];
	
}IseParam; 


typedef struct _Ise_datas_{
	uint8_t     status; 
	uint8_t     run_type; 
	uint8_t     power_low; 
	uint8_t     pump_resean; 
	uint8_t     pump_running;
	uint8_t     watering;
	uint8_t     params_change;
	uint8_t     pump_last_run_reason;
	uint64_t    pump_last_run_ts;
	uint64_t    pump_next_run_mins;
	uint64_t    start_ts;
}IseData;



void bgs_led_key_task(void* arg);
void bgs_util_socket_task(void* arg);



#endif
