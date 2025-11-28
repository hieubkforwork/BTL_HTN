#ifndef WIFI_H
#define WIFI_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_config.h"

#define WIFI_TAG  "WIFI_CONFIG"
#define ESP32_WIFI_SSID "ESP32_Config_Car"
#define ESP32_WIFI_PASSWORD "12345678"
#define MAX_RETRY 10

// trạng thái STA
extern bool sta_connected;

// Hàm khởi động AP + STA
void wifi_init_sta_or_ap(void);

// Hàm bật AP cấu hình WiFi
void start_config_ap(void);

// Callback gọi từ wifi.c → main.c
void wifi_on_sta_got_ip(esp_ip4_addr_t ip);

#endif // WIFI_H
