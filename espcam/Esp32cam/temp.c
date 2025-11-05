#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "driver/ledc.h"

// ==== WIFI & FIREBASE CONFIG ====
#define WIFI_SSID       "minhthao_2.4g"
#define WIFI_PASS       "14012004"
#define FIREBASE_HOST   "esp32-fire-ae12a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SECRET "RkdFX4MJsPFQISXrvDooAaWenjKHxnq0QNxWU2hR"

#define TAG "ESP32_CAM"
#define FLASH_PIN 4

static char ip_str[16];
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// ==== CAMERA CONFIG (AI Thinker) ====
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// ==== WIFI HANDLER ====
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = { 0 };
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", ip_str);
}

// ==== CAMERA INIT ====
static void camera_init(void) {
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN, .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK, .pin_sscb_sda = CAM_PIN_SIOD, .pin_sscb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6, .pin_d5 = CAM_PIN_D5, .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2, .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC, .pin_href = CAM_PIN_HREF, .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,  // ✅ Giảm kích thước ảnh để mượt
        .jpeg_quality = 15,             // ✅ Giảm CPU load
        .fb_count = 2
    };
    esp_camera_init(&config);
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_hmirror(s, 1);
    ESP_LOGI(TAG, "Camera initialized");
}

// ==== FLASH LED PWM INIT ====
#define LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_1
#define LEDC_TIMER   LEDC_TIMER_1

static void flash_led_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_channel_config_t ch = {
        .gpio_num = FLASH_PIN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0
    };
    ledc_timer_config(&timer);
    ledc_channel_config(&ch);
    ESP_LOGI(TAG, "Flash LED initialized");
}

// ==== SEND INFO TO FIREBASE ====
static void send_info_to_firebase(void) {
    char url[256];
    snprintf(url, sizeof(url), "https://%s/esp32_info.json?auth=%s", FIREBASE_HOST, FIREBASE_SECRET);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ip", ip_str);
    cJSON_AddStringToObject(root, "status", "connected");
    char *json = cJSON_PrintUnformatted(root);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_PUT, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    cJSON_Delete(root);
    free(json);
    ESP_LOGI(TAG, "Device info sent to Firebase");
}

// ==== FIREBASE LED CONTROL ====
static void firebase_led_task(void *pv)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://%s/esp32.json?auth=%s",
             FIREBASE_HOST, FIREBASE_SECRET);

    while (1)
    {
        ESP_LOGI(TAG, "📡 Requesting: %s", url);

        esp_http_client_config_t cfg = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 5000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);

        esp_err_t err = esp_http_client_open(client, 0);
        if (err == ESP_OK)
        {
            int content_length = esp_http_client_fetch_headers(client);
            ESP_LOGI(TAG, "✅ HTTP Headers OK, length = %d", content_length);

            char buffer[256];
            int read_len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
            if (read_len > 0)
            {
                buffer[read_len] = '\0';
                ESP_LOGI(TAG, "📦 Raw Firebase Data: %s", buffer);

                // 🧠 Parse JSON
                cJSON *root = cJSON_Parse(buffer);
                if (root)
                {
                    cJSON *led = cJSON_GetObjectItem(root, "led");
                    if (cJSON_IsNumber(led))
                    {
                        int pwm = led->valueint;
                        if (pwm < 0) pwm = 0;
                        if (pwm > 255) pwm = 255;

                        // 💡 Update PWM duty
                        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, pwm);
                        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

                        ESP_LOGI(TAG, "💡 LED brightness = %d (0–255)", pwm);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "⚠️ Không có trường 'led' trong JSON!");
                    }
                    cJSON_Delete(root);
                }
                else
                {
                    ESP_LOGW(TAG, "❌ JSON parse error!");
                }
            }
            else
            {
                ESP_LOGW(TAG, "⚠️ Không đọc được nội dung từ Firebase (read_len=%d)", read_len);
            }
        }
        else
        {
            ESP_LOGE(TAG, "🚨 Không mở được kết nối Firebase: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 giây đọc lại
    }
}

// ==== CAMERA STREAM SERVER ====
static const char *BOUNDARY = "123456";

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    char buf[64];
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456");

    while (1) {
        fb = esp_camera_fb_get();
        if (!fb) continue;
        snprintf(buf, sizeof(buf),
                 "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                 BOUNDARY, fb->len);
        httpd_resp_send_chunk(req, buf, strlen(buf));
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(100)); // ✅ giảm tần suất để mượt
    }
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
    const char *html = "<html><body><h3>ESP32-CAM Stream</h3><img src='/stream'></body></html>";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void start_server(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 81;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) == ESP_OK) {
        httpd_register_uri_handler(server, &(httpd_uri_t){"/", HTTP_GET, index_handler, NULL});
        httpd_register_uri_handler(server, &(httpd_uri_t){"/stream", HTTP_GET, stream_handler, NULL});
        ESP_LOGI(TAG, "HTTP server started on port 81");
    }
}

// ==== MAIN ====
void app_main(void) {
    nvs_flash_init();
    wifi_init();
    camera_init();
    flash_led_init();
    send_info_to_firebase();
    start_server();

    // ✅ Tách luồng Firebase sang core 1
    xTaskCreatePinnedToCore(firebase_led_task, "firebase_led_task", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Camera Stream Ready: http://%s:81/stream", ip_str);
}
