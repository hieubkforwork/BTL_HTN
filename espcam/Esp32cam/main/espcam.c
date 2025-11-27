#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"

#include "esp_camera.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "cJSON.h"

static const char *TAG = "ESP32CAM";

/* ======================= WIFI CONFIG ======================= */
#define WIFI_SSID "minhthao_2.4g"
#define WIFI_PASS "14012004"

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group;

/* ======================= FIREBASE CONFIG ======================= */
#define FIREBASE_HOST "esp32-fire-ae12a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SECRET "RkdFX4MJsPFQISXrvDooAaWenjKHxnq0QNxWU2hR"

/* ======================= CAMERA CONFIG (AI-THINKER) ======================= */
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5

#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

/* ======================= WIFI EVENT HANDLER ======================= */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WiFi disconnected. Reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ======================= HTTP STREAM ======================= */
static esp_err_t jpg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    return ESP_OK;
}

static esp_err_t jpg_lo_handler(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QVGA);
    return jpg_handler(req);
}

static esp_err_t jpg_mid_handler(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_VGA);
    return jpg_handler(req);
}

static esp_err_t jpg_hi_handler(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_SVGA);
    return jpg_handler(req);
}

/* ======================= START HTTP SERVER ======================= */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
       httpd_uri_t uri_lo = {
    .uri = "/cam-lo.jpg",
    .method = HTTP_GET,
    .handler = jpg_lo_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_mid = {
    .uri = "/cam-mid.jpg",
    .method = HTTP_GET,
    .handler = jpg_mid_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_hi = {
    .uri = "/cam-hi.jpg",
    .method = HTTP_GET,
    .handler = jpg_hi_handler,
    .user_ctx = NULL
};


        httpd_register_uri_handler(server, &uri_lo);
        httpd_register_uri_handler(server, &uri_mid);
        httpd_register_uri_handler(server, &uri_hi);

        ESP_LOGI(TAG, "HTTP server started");
    }
    else
    {
        ESP_LOGE(TAG, "Error starting server!");
    }

    return server;
}

/* ======================= WIFI INIT ======================= */
static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi…");
}

/* ======================= CAMERA INIT ======================= */
static void camera_init(void)
{
    camera_config_t config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = CAM_PIN_D0,
        .pin_d1 = CAM_PIN_D1,
        .pin_d2 = CAM_PIN_D2,
        .pin_d3 = CAM_PIN_D3,
        .pin_d4 = CAM_PIN_D4,
        .pin_d5 = CAM_PIN_D5,
        .pin_d6 = CAM_PIN_D6,
        .pin_d7 = CAM_PIN_D7,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_pclk = CAM_PIN_PCLK,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .xclk_freq_hz = 10000000,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_SVGA,
        .jpeg_quality = 10,
        .fb_count = 2
    };

    ESP_ERROR_CHECK(esp_camera_init(&config));
    ESP_LOGI(TAG, "Camera initialized");
}

/* ======================= SEND IP TO FIREBASE ======================= */
static void send_ip_to_firebase(const char *ip)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://%s/esp32cam_info.json?auth=%s",
             FIREBASE_HOST, FIREBASE_SECRET);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ip_cam", ip);
    cJSON_AddStringToObject(json, "status", "connected");

    char *json_str = cJSON_PrintUnformatted(json);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "IP sent to Firebase (HTTP %d)",
                 esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "Send error: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_Delete(json);
    free(json_str);
}

/* ======================= MAIN ======================= */
void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_init();

    // CHỜ WIFI CONNECT XONG
    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        false,
        false,
        portMAX_DELAY);

    // INIT CAMERA
    camera_init();

    // GET IP
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);

    char ip_str[20];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Stream URLs:");
    ESP_LOGI(TAG, "http://" IPSTR "/cam-lo.jpg", IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "http://" IPSTR "/cam-mid.jpg", IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "http://" IPSTR "/cam-hi.jpg", IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "======================================");

    send_ip_to_firebase(ip_str);

    start_webserver();

    while (1)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
