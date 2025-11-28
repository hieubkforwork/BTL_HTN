#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"  
#include "esp_wifi.h"   
esp_err_t get_handler(httpd_req_t *req);
esp_err_t save_handler(httpd_req_t *req);
esp_err_t delete_handler(httpd_req_t *req);
httpd_handle_t start_stream_webserver(void);

#define TAGWEB "INFO_WIFI_WEB"

extern char wifi_ssid[32];
extern char wifi_pass[64];
extern esp_ip4_addr_t current_ip;
#endif