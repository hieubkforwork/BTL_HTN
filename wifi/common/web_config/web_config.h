#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

esp_err_t get_handler(httpd_req_t *req);
esp_err_t save_handler(httpd_req_t *req);
httpd_handle_t start_webserver(void);

#define TAGWEB "INFO_WIFI_WEB"
extern char wifi_ssid[16];
extern char wifi_pass[32];
#endif