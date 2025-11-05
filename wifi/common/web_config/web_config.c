#include "web_config.h"

char wifi_ssid[16] = "";
char wifi_pass[32] = "";
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = get_handler};
        httpd_uri_t save = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_handler};

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &save);
    }
    return server;
}

esp_err_t get_handler(httpd_req_t *req)
{
    const char *html =
        "<h2>WiFi Configuration</h2>"
        "<form action=\"/save\" method=\"post\">"
        "SSID: <input name=\"ssid\"><br>"
        "Password: <input name=\"pass\" type=\"password\"><br>"
        "<input type=\"submit\" value=\"Save\">"
        "</form>";

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t save_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    buf[len] = '\0';

    sscanf(buf, "ssid=%31[^&]&pass=%63s", wifi_ssid, wifi_pass);

    ESP_LOGI(TAGWEB, "New WiFi: SSID=%s, PASS=%s", wifi_ssid, wifi_pass);

    nvs_handle_t nvs;
    nvs_open("wifi", NVS_READWRITE, &nvs);
    nvs_set_str(nvs, "ssid", wifi_ssid);
    nvs_set_str(nvs, "pass", wifi_pass);
    nvs_commit(nvs);
    nvs_close(nvs);

    httpd_resp_sendstr(req, "Saved! Rebooting...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}