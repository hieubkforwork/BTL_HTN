#include "web_config.h"

char wifi_ssid[32] = "";
char wifi_pass[64] = "";
esp_ip4_addr_t current_ip = {0};

/* ===========================
 * START CONFIG WEBSERVER
 * =========================== */
httpd_handle_t start_stream_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 8080;
    cfg.ctrl_port   = 8081;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &cfg) == ESP_OK)
    {
        httpd_uri_t root = { "/", HTTP_GET, get_handler, NULL };
        httpd_uri_t save = { "/save", HTTP_POST, save_handler, NULL };

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &save);

        ESP_LOGI(TAGWEB, "Config WebServer started on 8080");
    }
    return server;
}

/* ===========================
 * WEBSERVER HANDLERS
 * =========================== */
esp_err_t get_handler(httpd_req_t *req)
{
    char html[600];

    snprintf(html, sizeof(html),
        "<h2>ESP32 WiFi Config</h2>"
        "<form action=\"/save\" method=\"post\">"
        "SSID: <input name=\"ssid\"><br>"
        "PASS: <input name=\"pass\" type=\"password\"><br>"
        "<input type=\"submit\" value=\"Connect WiFi\">"
        "</form>"
    );

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ===========================
 * SAVE WiFi from User
 * =========================== */
esp_err_t save_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf)-1);
    buf[len] = '\0';

    sscanf(buf, "ssid=%31[^&]&pass=%63s", wifi_ssid, wifi_pass);

    ESP_LOGI(TAGWEB, "USER INPUT → SSID=%s PASS=%s",
             wifi_ssid, wifi_pass);

    wifi_config_t sta_cfg = {0};
    strcpy((char *)sta_cfg.sta.ssid, wifi_ssid);
    strcpy((char *)sta_cfg.sta.password, wifi_pass);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    httpd_resp_sendstr(req, "Connecting to WiFi...");
    return ESP_OK;
}
