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
#define WIFI_SSID       "POCOF3"
#define WIFI_PASS       "duy161124"
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


/*--------------------------------WIFI-------------------------------------*/
/**
 * @brief Trình xử lý sự kiện Wi-Fi chính cho thiết bị ESP32 (hoặc ESP-IDF).
 * * Hàm này xử lý các sự kiện Wi-Fi và IP quan trọng như:
 * 1. Bắt đầu kết nối khi STA khởi động.
 * 2. Tự động kết nối lại khi kết nối bị ngắt.
 * 3. Đặt cờ sự kiện và lưu địa chỉ IP khi nhận được IP thành công.
 *
 * @param arg Con trỏ tùy chọn, thường là NULL hoặc dữ liệu cấu hình.
 * @param base Cơ sở sự kiện để phân biệt loại sự kiện (WIFI_EVENT, IP_EVENT).
 * @param id ID sự kiện cụ thể (ví dụ: WIFI_EVENT_STA_START, IP_EVENT_STA_GOT_IP).
 * @param data Con trỏ tới dữ liệu sự kiện cụ thể (ví dụ: ip_event_got_ip_t cho sự kiện IP_EVENT_STA_GOT_IP).
 */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    /*Esp32 đã khởi động thành công. Bắt đầu quá trình kết nối*/
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    /*Esp32 bị ngắt kết nối khỏi Wifi. Cố gắng kết nối lại.*/
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    /*Esp32 đã nhận được địa chỉ IP thành công từ DHCP server.*/
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        /*SET bit Connected*/
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
/**
 * @brief Khởi tạo và kết nối Wi-Fi ở chế độ Station (STA).
 *
 * Hàm này thiết lập toàn bộ quy trình khởi tạo Wi-Fi cho ESP32, bao gồm:
 * - Tạo nhóm sự kiện (Event Group) để đồng bộ trạng thái kết nối.
 * - Khởi tạo network interface và vòng lặp sự kiện (event loop).
 * - Tạo interface Wi-Fi Station mặc định.
 * - Khởi tạo driver Wi-Fi với cấu hình mặc định.
 * - Đăng ký các event handler để xử lý sự kiện Wi-Fi và IP.
 * - Thiết lập SSID, mật khẩu và chế độ hoạt động (Station mode).
 * - Khởi động Wi-Fi và chờ đến khi thiết bị kết nối thành công.
 *
 * @note Hàm này sẽ block (chờ) cho đến khi Wi-Fi được kết nối thành công.
 */
static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    /*Khởi tạo liên kết driver */
    esp_netif_init();
    /*Loop để đọc sự kiện khi kết nối, khởi tạo để sử dụng esp_event_handler_register*/
    esp_event_loop_create_default();
    /*Tạo interface mạng cho Wi-Fi Station, sau đó mới được khởi tạo esp_wifi_init, esp_wifi_set_mode, esp_wifi_start*/
    esp_netif_create_default_wifi_sta();
    /*Khởi tạo Wi-Fi driver dựa trên cấu hình bạn vừa tạo ở trên.*/
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    /*Đăng ký hàm callback xử lý khi có sự kiện xảy ra (event handler).*/
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    /*Config Wifi và Start*/
    wifi_config_t wifi_config = { 0 };
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");

    /*Wait until Bit Connected được Set*/
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", ip_str);
}

/*--------------------------------CAMERA-------------------------------------*/
/**
 * @brief Khởi tạo module camera và cấu hình cảm biến.
 *
 * Hàm này thiết lập toàn bộ cấu hình phần cứng cho camera ESP32-CAM, bao gồm:
 * - Gán chân tín hiệu (D0–D7, XCLK, PCLK, VSYNC, HREF, SDA, SCL...).
 * - Cấu hình tần số xung clock (XCLK) cho camera.
 * - Chọn định dạng ảnh (JPEG) và độ phân giải (QVGA).
 * - Thiết lập chất lượng nén JPEG và số lượng frame buffer.
 * - Khởi tạo driver camera bằng esp_camera_init().
 * - Cấu hình cảm biến (ví dụ: lật gương ngang ảnh).
 */
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
        .frame_size = FRAMESIZE_VGA,  
        .jpeg_quality = 15,             
        .fb_count = 2
    };
    esp_camera_init(&config);
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_hmirror(s, 1);
    ESP_LOGI(TAG, "Camera initialized");
}



static const char *BOUNDARY = "123456";
/**
 * @brief Xử lý luồng video MJPEG từ camera và gửi liên tục đến client qua HTTP.
 *
 * Hàm này thiết lập phản hồi HTTP kiểu "multipart/x-mixed-replace" để truyền
 * nhiều khung hình JPEG nối tiếp nhau, tạo thành luồng video trực tiếp (stream).
 * Trong vòng lặp vô hạn, hàm sẽ:
 *  - Lấy khung hình (frame buffer) từ camera.
 *  - Tạo phần header cho mỗi ảnh JPEG.
 *  - Gửi header và dữ liệu ảnh đến client dưới dạng từng "chunk".
 *  - Trả lại bộ nhớ frame cho driver camera.
 *  - Chờ một khoảng thời gian ngắn để điều chỉnh tốc độ khung hình (FPS).
 *
 * @param req Con trỏ đến yêu cầu HTTP (httpd_req_t) được gửi từ client.
 * @return esp_err_t ESP_OK nếu luồng hoạt động bình thường (không bao giờ thoát ra trừ khi client ngắt kết nối).
 */
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    static char part_buf[64];

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) continue;

        // Tạo phần header
        int header_len = snprintf(part_buf, sizeof(part_buf),
            "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            BOUNDARY, fb->len);

        httpd_resp_send_chunk(req, part_buf, header_len);
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(30));  // giảm delay để mượt hơn (20 FPS ~ 50ms)
    }

    return ESP_OK;
}

/**
 * @brief Xử lý yêu cầu HTTP tại trang chủ và trả về giao diện xem video stream.
 *
 * Khi client truy cập vào địa chỉ gốc ("/"), hàm này sẽ gửi về một trang HTML đơn giản
 * chứa tiêu đề và thẻ `<img>` hiển thị video trực tiếp từ đường dẫn `/stream`.
 * Đây là điểm truy cập chính để người dùng xem luồng camera trên trình duyệt.
 *
 * @param req Con trỏ đến yêu cầu HTTP (httpd_req_t) từ client.
 * @return esp_err_t ESP_OK nếu gửi phản hồi thành công.
 */
static esp_err_t index_handler(httpd_req_t *req) {
    const char *html = "<html><body><h3>ESP32-CAM Stream</h3><img src='/stream'></body></html>";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Khởi động máy chủ HTTP và đăng ký các endpoint xử lý yêu cầu.
 *
 * Hàm này cấu hình và khởi động web server trên cổng 81, sau đó đăng ký
 * hai đường dẫn (URI handler):
 *  - "/" để hiển thị trang HTML chính (index_handler).
 *  - "/stream" để truyền luồng video MJPEG từ camera (stream_handler).
 * Khi server khởi động thành công, thông báo sẽ được ghi vào log.
 */
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

/*--------------------------------FIREBASE_CONTROL_LED-------------------------------------*/
#define LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_1
#define LEDC_TIMER   LEDC_TIMER_1

/**
 * @brief Khởi động máy chủ HTTP để phục vụ trang web và luồng video từ camera.
 *
 * Hàm này cấu hình cổng, khởi động web server, và đăng ký các endpoint:
 *  - "/" trả về trang HTML giao diện chính.
 *  - "/stream" phát luồng video MJPEG từ camera.
 */
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
/**
 * @brief Gửi thông tin thiết bị ESP32 lên Firebase Realtime Database.
 *
 * Hàm này tạo một yêu cầu HTTP PUT để cập nhật dữ liệu tại nút "esp32_info"
 * trong Firebase. Thông tin bao gồm:
 *  - Địa chỉ IP của thiết bị (`ip_str`)
 *  - Trạng thái kết nối hiện tại ("connected")
 *
 * Quy trình:
 *  1. Tạo chuỗi JSON chứa thông tin thiết bị bằng thư viện cJSON.
 *  2. Thiết lập cấu hình HTTP client với chứng chỉ và URL Firebase.
 *  3. Gửi yêu cầu PUT với nội dung JSON lên Firebase.
 *  4. Giải phóng bộ nhớ và ghi log khi hoàn tất.
 */
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
/**
 * @brief Nhiệm vụ (task) đọc dữ liệu điều khiển LED từ Firebase và cập nhật độ sáng LED.
 *
 * Hàm này chạy liên tục trong một vòng lặp vô hạn để:
 *  1. Gửi yêu cầu HTTP GET đến đường dẫn "esp32.json" trên Firebase.
 *  2. Nhận phản hồi JSON chứa giá trị trường "led".
 *  3. Phân tích giá trị PWM (0–255) và điều chỉnh độ sáng LED qua PWM.
 *  4. Ghi log trạng thái hoạt động và cảnh báo nếu có lỗi.
 *
 * Nếu giá trị nhận được nằm ngoài phạm vi hợp lệ (nhỏ hơn 0 hoặc lớn hơn 255),
 * hàm sẽ tự động giới hạn lại.  
 * Quá trình lặp lại mỗi 500 ms để cập nhật trạng thái LED theo dữ liệu mới nhất.
 *
 * @param pv Tham số đầu vào của task (thường không sử dụng, có thể truyền NULL).
 */
static void firebase_led_task(void *pv)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://%s/esp32.json?auth=%s",
             FIREBASE_HOST, FIREBASE_SECRET);

    while (1)
    {
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
            char buffer[256];
            int read_len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);

            if (read_len > 0)
            {
                buffer[read_len] = '\0';  

                cJSON *root = cJSON_Parse(buffer);
                if (root)
                {
                    cJSON *led = cJSON_GetObjectItem(root, "led");
                    if (cJSON_IsNumber(led))
                    {
                        int pwm = led->valueint;

                        if (pwm < 0) pwm = 0;
                        if (pwm > 255) pwm = 255;

                        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, pwm);
                        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

                        ESP_LOGI(TAG, "LED brightness = %d (0–255)", pwm);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "'led' field not found in JSON");
                    }

                    cJSON_Delete(root);  
                }
                else
                {
                    ESP_LOGW(TAG, "JSON parse error");
                }
            } else {
                ESP_LOGW(TAG, "Failed to read content from Firebase (read_len=%d)", read_len);
            }
        } else {
            ESP_LOGE(TAG, "Failed to open Firebase connection: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);

        vTaskDelay(pdMS_TO_TICKS(500));
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

    xTaskCreatePinnedToCore(firebase_led_task, "firebase_led_task", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Camera Stream Ready: http://%s:81/stream", ip_str);
}
