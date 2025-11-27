#ifndef WIFI_H
#define WIFI_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>       
#include "esp_err.h"             
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h" 
#include "web_config.h"   

#define TAG  "WIFI_CONFIG"
#define ESP32_WIFI_SSID "ESP32_Config_Car"
#define ESP32_WIFI_PASSWORD "12345678"
#define MAX_RETRY 10 //retry when disconnect

extern bool sta_connected;

void wifi_init_sta_or_ap(void);
void start_config_ap(void);


#endif // WIFI_H