#include "web_config.h"

char wifi_ssid[16] = "";
char wifi_pass[32] = "";
esp_ip4_addr_t current_ip = {0};
httpd_handle_t start_webserver(void) {
    // if (webserver_running) return server;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    // server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = get_handler
        };
        httpd_uri_t save = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_handler
        };
        httpd_uri_t del = {
            .uri = "/delete",
            .method = HTTP_POST,
            .handler = delete_handler
        };
        httpd_register_uri_handler(server, &del);
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &save);
        // webserver_running = true;
        ESP_LOGI(TAGWEB, "Webserver started!");
    }
    return server;
}

esp_err_t get_handler(httpd_req_t *req) {
    char html[512];
    uint8_t ip1, ip2, ip3, ip4;

    ip1 = esp_ip4_addr1(&current_ip);
    ip2 = esp_ip4_addr2(&current_ip);
    ip3 = esp_ip4_addr3(&current_ip);
    ip4 = esp_ip4_addr4(&current_ip);

    snprintf(html, sizeof(html),
        "<h2>WiFi Configuration</h2>"
        "<p>Current IP: %d.%d.%d.%d</p>"
        "<p>Connected SSID: <b>%s</b></p>"
        "<form action=\"/save\" method=\"post\">"
        "SSID: <input name=\"ssid\"><br>"
        "Password: <input name=\"pass\" type=\"password\"><br>"
        "<input type=\"submit\" value=\"Save\">"
        "</form>"
        "<form action=\"/delete\" method=\"post\" style=\"margin-top:20px;\">"
        "<input type=\"submit\" value=\"Delete WiFi Configuration\" style=\"background-color:#d9534f;color:white;\">"
        "</form>",
        ip1, ip2, ip3, ip4,
        wifi_ssid[0] ? wifi_ssid : "None");

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t delete_handler(httpd_req_t *req) {
    nvs_handle_t nvs;
    nvs_open("wifi", NVS_READWRITE, &nvs);
    nvs_erase_key(nvs, "ssid");
    nvs_erase_key(nvs, "pass");
    nvs_commit(nvs);
    nvs_close(nvs);

    httpd_resp_sendstr(req, "WiFi config deleted! Restarting...");

    vTaskDelay(500 / portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}

esp_err_t save_handler(httpd_req_t *req) {
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[len] = '\0';

    char ssid[32], pass[64];
    sscanf(buf, "ssid=%31[^&]&pass=%63s", ssid, pass);
    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid));
    strncpy(wifi_pass, pass, sizeof(wifi_pass));
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    wifi_pass[sizeof(wifi_pass) - 1] = '\0';

    ESP_LOGI(TAGWEB, "New WiFi: SSID=%s, PASS=%s", wifi_ssid, wifi_pass);

    nvs_handle_t nvs;
    nvs_open("wifi", NVS_READWRITE, &nvs);
    nvs_set_str(nvs, "ssid", wifi_ssid);
    nvs_set_str(nvs, "pass", wifi_pass);
    nvs_commit(nvs);
    nvs_close(nvs);

    httpd_resp_sendstr(req, "Saved! Switching to WiFi...");

    // esp_wifi_stop();
    // wifi_init_sta_or_ap();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}
