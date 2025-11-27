#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "cJSON.h"

static const char *TAG = "MOTOR_WS";

/* ======================= WIFI CONFIG ======================= */
#define WIFI_SSID      "POCOF3"
#define WIFI_PASSWORD  "duy161124"

#define WIFI_CONNECTED_BIT BIT0
#define FIREBASE_POST_BIT  BIT1   // báo cần gửi IP WS lên Firebase

static EventGroupHandle_t wifi_event_group;
static char ip_str[16] = "0.0.0.0";

/* URL WS cần gửi lên Firebase */
static char ws_url_to_send[64] = {0};

/* ======================= FIREBASE CONFIG ======================= */
#define FIREBASE_HOST   "esp32-fire-ae12a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SECRET "RkdFX4MJsPFQISXrvDooAaWenjKHxnq0QNxWU2hR"

/* ======================= DRV8833/LEDC CONFIG ======================= */

// Mạch DRV8833 #1
#define AIN1_GPIO 33
#define AIN2_GPIO 32
#define BIN1_GPIO 25
#define BIN2_GPIO 26
#define STBY1_GPIO 27

// Mạch DRV8833 #2
#define CIN1_GPIO 18
#define CIN2_GPIO 19
#define DIN1_GPIO 17
#define DIN2_GPIO 16
#define STBY2_GPIO 5

#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT // 10-bit -> 0~1023
#define LEDC_FREQUENCY  1000
#define MAX_PWM         1023

// LEDC Channels
#define LEDC_CHANNEL_A1 LEDC_CHANNEL_0
#define LEDC_CHANNEL_A2 LEDC_CHANNEL_1
#define LEDC_CHANNEL_B1 LEDC_CHANNEL_2
#define LEDC_CHANNEL_B2 LEDC_CHANNEL_3
#define LEDC_CHANNEL_C1 LEDC_CHANNEL_4
#define LEDC_CHANNEL_C2 LEDC_CHANNEL_5
#define LEDC_CHANNEL_D1 LEDC_CHANNEL_6
#define LEDC_CHANNEL_D2 LEDC_CHANNEL_7

/* ======================= ADC CONFIG (ĐO PIN) ======================= */
#define VOLTAGE_DIVIDER_RATIO (3.14176f)
#define NUM_SAMPLES 10

static int Vout_samples[NUM_SAMPLES] = {0};
static int sample_index = 0;
static int read_count = 0;
static int adcOld = 0;
static int latest_vin_mv = 0;   // Điện áp mới nhất (mV) để gửi cho app

/* ======================= WEBSOCKET GLOBAL ======================= */
static httpd_handle_t ws_server = NULL;
static int ws_client_fd = -1;   // chỉ xử lý 1 client cho đơn giản

/* ======================= DRV8833 INIT ======================= */
static void drv8833_init(void)
{
    // 1. Timer PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Các kênh PWM
    ledc_channel_config_t ledc_channel[] = {
        { .channel = LEDC_CHANNEL_A1,
          .speed_mode = LEDC_MODE,
          .gpio_num = AIN1_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_A2,
          .speed_mode = LEDC_MODE,
          .gpio_num = AIN2_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_B1,
          .speed_mode = LEDC_MODE,
          .gpio_num = BIN1_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_B2,
          .speed_mode = LEDC_MODE,
          .gpio_num = BIN2_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_C1,
          .speed_mode = LEDC_MODE,
          .gpio_num = CIN1_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_C2,
          .speed_mode = LEDC_MODE,
          .gpio_num = CIN2_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_D1,
          .speed_mode = LEDC_MODE,
          .gpio_num = DIN1_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
        { .channel = LEDC_CHANNEL_D2,
          .speed_mode = LEDC_MODE,
          .gpio_num = DIN2_GPIO,
          .timer_sel = LEDC_TIMER,
          .duty = 0,
          .hpoint = 0,
          .intr_type = LEDC_INTR_DISABLE,
          .flags.output_invert = 0 },
    };

    for (int i = 0; i < 8; i++) {
        esp_err_t err = ledc_channel_config(&ledc_channel[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel %d config failed: %s", i, esp_err_to_name(err));
        }
    }

    // 3. STBY
    gpio_reset_pin(STBY1_GPIO);
    gpio_set_direction(STBY1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(STBY1_GPIO, 1);

    gpio_reset_pin(STBY2_GPIO);
    gpio_set_direction(STBY2_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(STBY2_GPIO, 1);

    ESP_LOGI(TAG, "DRV8833 (x2) initialized");
}

/* ========== MOTOR CONTROL ========== */
static void motor_control(ledc_channel_t ch1, ledc_channel_t ch2, uint32_t speed, bool forward)
{
    if (speed > MAX_PWM) speed = MAX_PWM;

    if (forward) {
        ledc_set_duty(LEDC_MODE, ch1, speed);
        ledc_update_duty(LEDC_MODE, ch1);
        ledc_set_duty(LEDC_MODE, ch2, 0);
        ledc_update_duty(LEDC_MODE, ch2);
    } else {
        ledc_set_duty(LEDC_MODE, ch1, 0);
        ledc_update_duty(LEDC_MODE, ch1);
        ledc_set_duty(LEDC_MODE, ch2, speed);
        ledc_update_duty(LEDC_MODE, ch2);
    }
}

static void motor_stop(ledc_channel_t ch1, ledc_channel_t ch2)
{
    ledc_set_duty(LEDC_MODE, ch1, 0);
    ledc_update_duty(LEDC_MODE, ch1);
    ledc_set_duty(LEDC_MODE, ch2, 0);
    ledc_update_duty(LEDC_MODE, ch2);
}

static void all_stop(void)
{
    motor_stop(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2);
    motor_stop(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2);
    motor_stop(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2);
    motor_stop(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2);
}

/* ======================= FIREBASE: GỬI IP WS ======================= */

static void send_ws_ip_to_firebase(const char *ip_ws)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://%s/esp32-ip.json?auth=%s",
             FIREBASE_HOST, FIREBASE_SECRET);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "esp32-ip", ip_ws);
    char *json = cJSON_PrintUnformatted(root);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Sent WebSocket IP to Firebase: %s", json);

    free(json);
    cJSON_Delete(root);
}

/* ======================= WEBSOCKET SEND HELPER ======================= */

static void ws_send_json(const char *json)
{
    if (ws_server == NULL || ws_client_fd < 0) return;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json)
    };

    esp_err_t ret = httpd_ws_send_frame_async(ws_server, ws_client_fd, &frame);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws_send_json error: %s", esp_err_to_name(ret));
    }
}

/* Gửi info (IP + Volt) cho app */
static void ws_send_info()
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "info");
    cJSON_AddStringToObject(root, "ip", ip_str);
    cJSON_AddNumberToObject(root, "volt", latest_vin_mv);

    char *json = cJSON_PrintUnformatted(root);
    ws_send_json(json);

    cJSON_Delete(root);
    free(json);
}

/* ======================= WEBSOCKET HANDLER ======================= */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake WebSocket
        int sockfd = httpd_req_to_sockfd(req);
        ws_client_fd = sockfd;
        ESP_LOGI(TAG, "WebSocket client connected, fd=%d", sockfd);

        // Gửi info ban đầu
        ws_send_info();
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
    };

    // Lấy length
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;

    if (frame.len == 0) return ESP_OK;

    frame.payload = malloc(frame.len + 1);
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        free(frame.payload);
        return ret;
    }
    frame.payload[frame.len] = 0;

    ESP_LOGI(TAG, "WS RX: %s", frame.payload);

    // Nếu là JSON: {"dir":"F","speed":80}
    if (frame.payload[0] == '{') {
        cJSON *root = cJSON_Parse((char *)frame.payload);
        if (root) {
            cJSON *dir = cJSON_GetObjectItem(root, "dir");
            cJSON *spd = cJSON_GetObjectItem(root, "speed");

            uint32_t speed_percent = (cJSON_IsNumber(spd)) ? spd->valueint : 0;
            if (speed_percent > 100) speed_percent = 100;

            uint32_t pwm_value = (speed_percent * 1023) / 100;

            // Hệ số cân bánh (tuỳ bạn chỉnh tiếp)
            float A_rate = 1.0f;
            float B_rate = 0.94f;
            float C_rate = 0.94f;
            float D_rate = 1.2f;

            uint32_t pwm_A_tmp = (uint32_t)(pwm_value * A_rate);
            uint32_t pwm_B_tmp = (uint32_t)(pwm_value * B_rate);
            uint32_t pwm_C_tmp = (uint32_t)(pwm_value * C_rate);
            uint32_t pwm_D_tmp = (uint32_t)(pwm_value * D_rate);

            uint32_t pwm_A = (pwm_A_tmp > 1023) ? 1023 : pwm_A_tmp;
            uint32_t pwm_B = (pwm_B_tmp > 1023) ? 1023 : pwm_B_tmp;
            uint32_t pwm_C = (pwm_C_tmp > 1023) ? 1023 : pwm_C_tmp;
            uint32_t pwm_D = (pwm_D_tmp > 1023) ? 1023 : pwm_D_tmp;

            const char *d = cJSON_IsString(dir) ? dir->valuestring : "S";

            ESP_LOGI(TAG, "DIR=%s, speed=%d%% (PWM=%lu)", d, speed_percent, (unsigned long)pwm_value);

            if (strcmp(d, "B") == 0) {
                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, true);
                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, true);
                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, true);
                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, true);
            } else if (strcmp(d, "F") == 0) {
                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, false);
                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, false);
                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, false);
                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, false);
            } else if (strcmp(d, "L") == 0) {
                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, false);
                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, true);
                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, false);
                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, true);
            } else if (strcmp(d, "R") == 0) {
                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, true);
                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, false);
                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, true);
                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, false);
            } else if (strcmp(d, "FL") == 0) {
                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A - pwm_A * 40 / 100, false);
                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, false);
                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, false);
                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D - pwm_D * 40 / 100, false);
            } else if (strcmp(d, "FR") == 0) {
                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, false);
                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B - pwm_B * 40 / 100, false);
                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C - pwm_C * 40 / 100, false);
                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, false);
            } else {
                all_stop();
            }

            cJSON_Delete(root);
        }
    }

    free(frame.payload);
    return ESP_OK;
}

/* ======================= START WS SERVER ======================= */

static httpd_handle_t start_ws_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t ws_uri = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ws_uri));

    ESP_LOGI(TAG, "WebSocket server started on ws://%s/ws", ip_str);
    return server;
}

/* ======================= ADC TASK: ĐO VÀ GỬI VOLT ======================= */

static void adc_task(void *pvParameters)
{
    // ADC init
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config)); // GPIO34

    // Calibration
    adc_cali_handle_t cali_handle = NULL;
    bool do_calibration = false;

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle) == ESP_OK) {
        do_calibration = true;
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration not supported");
    }

    int raw = 0;
    int Vout_mV = 0;
    int Vout_avg = 0;
    int Vin_mV = 0;

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw));

        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &Vout_mV));

            Vout_samples[sample_index] = Vout_mV;
            sample_index = (sample_index + 1) % NUM_SAMPLES;

            if (read_count < NUM_SAMPLES) {
                read_count++;
            } else {
                long sum = 0;
                for (int i = 0; i < NUM_SAMPLES; i++) sum += Vout_samples[i];

                Vout_avg = (int)(sum / NUM_SAMPLES);
                Vin_mV = (int)((float)Vout_avg * VOLTAGE_DIVIDER_RATIO);

                ESP_LOGI(TAG, "Raw=%d | Vout_avg=%d mV | Vin=%d mV (%.2f V)",
                         raw, Vout_avg, Vin_mV, Vin_mV / 1000.0f);

                if (Vin_mV != adcOld) {
                    adcOld = Vin_mV;
                    latest_vin_mv = Vin_mV;

                    // Gửi qua websocket cho app
                    cJSON *root = cJSON_CreateObject();
                    cJSON_AddStringToObject(root, "type", "volt");
                    cJSON_AddNumberToObject(root, "value", Vin_mV);
                    char *json = cJSON_PrintUnformatted(root);

                    ws_send_json(json);

                    cJSON_Delete(root);
                    free(json);
                }
            }
        } else {
            ESP_LOGI(TAG, "Raw: %d (no calibration)", raw);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ======================= WIFI EVENT HANDLER ======================= */

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Tạo URL WebSocket và LƯU vào biến global
        snprintf(ws_url_to_send, sizeof(ws_url_to_send), "ws://%s/ws", ip_str);

        // Báo: đã kết nối WiFi
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        // Báo thêm: cần gửi IP lên Firebase (task khác xử lý)
        xEventGroupSetBits(wifi_event_group, FIREBASE_POST_BIT);
    }
}

/* ======================= WIFI INIT ======================= */

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init finished");
}

/* ======================= FIREBASE TASK (GỬI IP WS) ======================= */

static void firebase_task(void *pvParameters)
{
    while (1) {
        // Chờ đến khi có bit FIREBASE_POST_BIT
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group,
            FIREBASE_POST_BIT,
            pdTRUE,      // clear bit sau khi nhận
            pdFALSE,
            portMAX_DELAY
        );

        if (bits & FIREBASE_POST_BIT) {
            if (strlen(ws_url_to_send) > 0) {
                ESP_LOGI(TAG, "Posting WS URL to Firebase: %s", ws_url_to_send);
                send_ws_ip_to_firebase(ws_url_to_send);
            }
        }
    }
}

/* ======================= APP MAIN ======================= */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting DRV8833 + WebSocket + Firebase IP...");

    wifi_init();

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi connection timeout, continuing anyway...");
    }

    drv8833_init();
    all_stop();

    ws_server = start_ws_server();

    // Task đọc ADC và gửi volt
    xTaskCreate(adc_task, "adc_task", 4096, NULL, 5, NULL);

    // Task gửi IP lên Firebase (stack riêng, tránh tràn sys_evt)
    xTaskCreate(firebase_task, "firebase_task", 4096, NULL, 5, NULL);
}
