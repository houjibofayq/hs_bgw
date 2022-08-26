#ifndef _IXE_MAIN
#define _IXE_MAIN


/* Types */
typedef enum {
    OTA_TYPE_FIRMWARE,
    OTA_TYPE_CONFIG,
} ota_type_t;

typedef enum {
    OTA_ERR_SUCCESS = 0,
    OTA_ERR_NO_CHANGE,
    OTA_ERR_IN_PROGRESS,
    OTA_ERR_FAILED_DOWNLOAD,
    OTA_ERR_FAILED_BEGIN,
    OTA_ERR_FAILED_WRITE,
    OTA_ERR_FAILED_END,
} ota_err_t;


void ota_on_completed(ota_type_t type, ota_err_t err);


int ixe_get_time(uint8_t* hour,uint8_t* min,uint8_t* sec);
int ixe_set_time(uint8_t hour,uint8_t min,uint8_t sec);

esp_err_t bgs_save_ixe_params(void);
esp_err_t bgs_save_ble_params(void);

uint8_t ixe_reset_factory(void);
esp_err_t ixe_wifi_connect();
void ixe_ble_start();
esp_err_t ixe_ble_stop(void);



#endif
