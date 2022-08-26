#ifndef _BGS_BLUFI
#define _BGS_BLUFI

/* Event callback types */
typedef void (*wifi_on_connected_cb_t)(void);
typedef void (*wifi_on_disconnected_cb_t)(void);

void wifi_set_blufi_on();


/* Event handlers */
void wifi_set_on_connected_cb(wifi_on_connected_cb_t cb);
void wifi_set_on_disconnected_cb(wifi_on_disconnected_cb_t cb);

void ixe_initialise_wifi(void);

void ixe_blufi_start();
esp_err_t ixe_blufi_stop(void);

void ixe_sntp_task(void *param);


#endif
