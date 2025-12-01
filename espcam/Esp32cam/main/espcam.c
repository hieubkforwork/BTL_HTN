#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "esp_camera.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"          // THÊM: dùng esp_wifi_set_mode

#include "wifi.h"
#include "web_config.h"
#include "cJSON.h"

static const char *TAG = "ESP32CAM";

/* ===========================================================
 * FIREBASE CONFIG
 * ===========================================================
 */
#define FIREBASE_HOST   "esp32-fire-ae12a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SECRET "RkdFX4MJsPFQISXrvDooAaWenjKHxnq0QNxWU2hR"


/* ===========================================================
 * CAMERA PIN (AI-THINKER)
 * ===========================================================
 */
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5

#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

/* ===========================================================
 * CAMERA WEB SERVER (JPEG SNAPSHOT)
 * ===========================================================
 */

// CHỈ SỬA: loại bỏ việc đổi framesize mỗi request
// => Tránh reset camera liên tục gây lag + corrupt jpeg
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

// GIỮ NGUYÊN tên hàm, chỉ xóa phần đổi framesize
static esp_err_t jpg_lo(httpd_req_t *req)
{
    return jpg_handler(req);
}

static esp_err_t jpg_mid(httpd_req_t *req)
{
    return jpg_handler(req);
}

static esp_err_t jpg_hi(httpd_req_t *req)
{
    return jpg_handler(req);
}


httpd_handle_t start_cam_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port   = 81;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t lo  = { "/cam-lo.jpg",  HTTP_GET, jpg_lo,  NULL };
        httpd_uri_t mid = { "/cam-mid.jpg", HTTP_GET, jpg_mid, NULL };
        httpd_uri_t hi  = { "/cam-hi.jpg",  HTTP_GET, jpg_hi,  NULL };

        httpd_register_uri_handler(server, &lo);
        httpd_register_uri_handler(server, &mid);
        httpd_register_uri_handler(server, &hi);

        ESP_LOGI(TAG, "Camera stream server started on port 80");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start camera server");
    }

    return server;
}


/* ===========================================================
 * CAMERA INIT — SỬA Ở ĐÂY
 * ===========================================================
 */

// CHỈ SỬA: framesize thiết lập đúng 1 lần DUY NHẤT tại đây
// => Không reset sensor liên tục nữa
static void camera_init(void)
{
    camera_config_t config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer   = LEDC_TIMER_0,
        .pin_d0 = CAM_PIN_D0, .pin_d1 = CAM_PIN_D1,
        .pin_d2 = CAM_PIN_D2, .pin_d3 = CAM_PIN_D3,
        .pin_d4 = CAM_PIN_D4, .pin_d5 = CAM_PIN_D5,
        .pin_d6 = CAM_PIN_D6, .pin_d7 = CAM_PIN_D7,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_pclk = CAM_PIN_PCLK,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href  = CAM_PIN_HREF,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .xclk_freq_hz = 20000000,
        .pixel_format = PIXFORMAT_JPEG,
        

        // CHỈ SỬA: 
        // Chỉ set framesize 1 lần ở đây (VGA hoặc SVGA đều OK)
        .frame_size = FRAMESIZE_VGA,     // <<======= SỬA QUAN TRỌNG
        .jpeg_quality = 12,
        .fb_count = 3
    };

    ESP_ERROR_CHECK(esp_camera_init(&config));
    ESP_LOGI(TAG, "Camera initialized");
}


/* ===========================================================
 * FIREBASE REPORT
 * ===========================================================
 */
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
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "Firebase updated (HTTP %d)",
                esp_http_client_get_status_code(client));
    else
        ESP_LOGE(TAG, "Firebase error: %s", esp_err_to_name(err));

    esp_http_client_cleanup(client);
    cJSON_Delete(json);
    free(json_str);
}


/* ===========================================================
 * TASK RIÊNG: CAMERA + DNS + FIREBASE
 * ===========================================================
 */

static esp_ip4_addr_t g_sta_ip;
static bool cam_task_started = false;
static void cam_and_firebase_task(void *pv)
{
    char ip_str[20];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&g_sta_ip));
    ESP_LOGI(TAG, "[TASK] Start cam + Firebase for IP: %s", ip_str);

    // 1️⃣ SET DNS GOOGLE — CHUẨN IDF 5.5.1
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");

    esp_err_t r = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);

    if (r == ESP_OK)
        ESP_LOGI(TAG, "DNS set to 8.8.8.8");
    else
        ESP_LOGE(TAG, "DNS set failed: %s", esp_err_to_name(r));

    // 2️⃣ Tắt AP để ổn định mạng
    ESP_LOGI(TAG, "Switching to STA-only mode...");
    esp_wifi_set_mode(WIFI_MODE_STA);
    vTaskDelay(pdMS_TO_TICKS(300));

    // 3️⃣ Khởi động camera
    camera_init();

    // 4️⃣ Start camera webserver
    start_cam_webserver();

    ESP_LOGI(TAG, "============== CAMERA STREAM URL ==============");
    ESP_LOGI(TAG, "http://%s/cam-lo.jpg",  ip_str);
    ESP_LOGI(TAG, "http://%s/cam-mid.jpg", ip_str);
    ESP_LOGI(TAG, "http://%s/cam-hi.jpg",  ip_str);
    ESP_LOGI(TAG, "===============================================");

    // 5️⃣ Gửi IP lên Firebase
    send_ip_to_firebase(ip_str);

    ESP_LOGI(TAG, "[TASK] cam_and_firebase_task done, delete task");
    vTaskDelete(NULL);
}


/* ===========================================================
 * CALLBACK TỪ wifi.c
 * ===========================================================
 */
void wifi_on_sta_got_ip(esp_ip4_addr_t ip)
{
    ESP_LOGI(TAG, "STA GOT IP (callback): " IPSTR, IP2STR(&ip));

    g_sta_ip = ip;

    if (cam_task_started)
    {
        ESP_LOGW(TAG, "cam_and_firebase_task already started, skip");
        return;
    }

    cam_task_started = true;

    // Tạo task riêng để tránh stack overflow ở sys_evt
    BaseType_t res = xTaskCreate(
        cam_and_firebase_task,
        "cam_fb_task",
        8192,      // stack depth (words) → ~32 KB
        NULL,
        5,
        NULL
    );

    if (res != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create cam_fb_task");
        cam_task_started = false;
    }
}


/* ===========================================================
 * MAIN
 * ===========================================================
 */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Start WiFi (AP + WebConfig + STA)
    wifi_init_sta_or_ap();

    // Main loop does nothing — mọi thứ chạy qua callback + task
    while (1)
        vTaskDelay(pdMS_TO_TICKS(1000));
}