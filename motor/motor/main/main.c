#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "MOTOR_FIREBASE";

// --- Khai báo Firebase thông tin ---
#define FIREBASE_HOST "esp32-fire-ae12a-default-rtdb.asia-southeast1.firebasedatabase.app" // Ví dụ: my-esp32-project-default-rtdb.firebaseio.com
#define FIREBASE_SECRET "RkdFX4MJsPFQISXrvDooAaWenjKHxnq0QNxWU2hR"                         // Lấy từ Project Settings -> Service Accounts -> Database secrets
// ************************************************* 

// ********* THAY THẾ BẰNG THÔNG TIN WIFI CỦA BẠN *********
#define WIFI_SSID "ACLAB"
#define WIFI_PASSWORD "ACLAB2023"
// ********************************************************

// ********* CẤU HÌNH LỌC VÀ HỆ SỐ CHIA ÁP *********
#define VOLTAGE_DIVIDER_RATIO (3.14176f)
#define NUM_SAMPLES 10
static int Vout_samples[NUM_SAMPLES] = {0};
static int sample_index = 0;

// KHAI BÁO BIẾN ĐẾM TĨNH để nó giữ giá trị giữa các lần lặp while(1)
static int read_count = 0;
static char ip_str[16];
static int adcOld = 0;

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group;

// ==== GPIO định nghĩa ====
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

// ==== Cấu hình PWM ====
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT // 10-bit -> 0~1023
#define LEDC_FREQUENCY 1000             // 1kHz PWM

// ==== LEDC Channel map ====
#define LEDC_CHANNEL_A1 LEDC_CHANNEL_0
#define LEDC_CHANNEL_A2 LEDC_CHANNEL_1
#define LEDC_CHANNEL_B1 LEDC_CHANNEL_2
#define LEDC_CHANNEL_B2 LEDC_CHANNEL_3
#define LEDC_CHANNEL_C1 LEDC_CHANNEL_4
#define LEDC_CHANNEL_C2 LEDC_CHANNEL_5
#define LEDC_CHANNEL_D1 LEDC_CHANNEL_6
#define LEDC_CHANNEL_D2 LEDC_CHANNEL_7

// ==== Hàm khởi tạo DRV8833 ====
static void drv8833_init(void)
{
    // 1. Cấu hình timer PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Cấu hình các kênh PWM
    ledc_channel_config_t ledc_channel[] = {
        {.channel = LEDC_CHANNEL_A1,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = AIN1_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_A2,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = AIN2_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_B1,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = BIN1_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_B2,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = BIN2_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_C1,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = CIN1_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_C2,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = CIN2_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_D1,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = DIN1_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
        {.channel = LEDC_CHANNEL_D2,
         .speed_mode = LEDC_LOW_SPEED_MODE,
         .gpio_num = DIN2_GPIO,
         .timer_sel = LEDC_TIMER,
         .duty = 0,
         .hpoint = 0,
         .intr_type = LEDC_INTR_DISABLE,
         .flags.output_invert = 0},
    };

    for (int i = 0; i < 8; i++)
    {
        esp_err_t err = ledc_channel_config(&ledc_channel[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "LEDC channel %d config failed: %s", i, esp_err_to_name(err));
        }
    }

    // 3. Cấu hình STBY cho 2 chip
    gpio_reset_pin(STBY1_GPIO);
    gpio_set_direction(STBY1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(STBY1_GPIO, 1);

    gpio_reset_pin(STBY2_GPIO);
    gpio_set_direction(STBY2_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(STBY2_GPIO, 1);

    ESP_LOGI(TAG, "DRV8833 (x2) initialized successfully.");
}

// ==== Hàm điều khiển động cơ ====
static void motor_control(ledc_channel_t ch1, ledc_channel_t ch2, uint32_t speed, bool forward)
{
    if (forward)
    {
        // Ch1: Duty = speed, Ch2: Duty = 0
        ledc_set_duty(LEDC_MODE, ch1, speed);
        ledc_update_duty(LEDC_MODE, ch1);
        ledc_set_duty(LEDC_MODE, ch2, 0);
        ledc_update_duty(LEDC_MODE, ch2);
    }
    else
    {
        // Ch1: Duty = 0, Ch2: Duty = speed
        ledc_set_duty(LEDC_MODE, ch1, 0);
        ledc_update_duty(LEDC_MODE, ch1);
        ledc_set_duty(LEDC_MODE, ch2, speed);
        ledc_update_duty(LEDC_MODE, ch2);
    }
}

// ==== HÀM DỪNG ĐỘNG CƠ MỚI (TƯỜNG MINH) ====
static void motor_stop(ledc_channel_t ch1, ledc_channel_t ch2)
{
    // Đặt cả hai kênh về 0% duty cycle (Short Brake)
    ledc_set_duty(LEDC_MODE, ch1, 0);
    ledc_update_duty(LEDC_MODE, ch1);
    ledc_set_duty(LEDC_MODE, ch2, 0);
    ledc_update_duty(LEDC_MODE, ch2);
}

// --- 🔌 5️⃣ Khởi tạo WiFi STA ---
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Reconnecting to Wi-Fi...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // 🔹 Báo hiệu WiFi đã sẵn sàng
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    // Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi tạo TCP/IP stack và Default Event Loop
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // Đăng ký Event Handler
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

    // Cấu hình WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
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

    // Bắt đầu kết nối
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished.");
}

static void send_info_to_firebase(void)
{
    char url[256];
    snprintf(url, sizeof(url), "https://%s/esp32_info.json?auth=%s", FIREBASE_HOST, FIREBASE_SECRET);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ip_esp32", ip_str);
    cJSON_AddStringToObject(root, "status", "connected");
    char *json = cJSON_PrintUnformatted(root);

    esp_http_client_config_t cfg = {.url = url, .method = HTTP_METHOD_PUT, .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    cJSON_Delete(root);
    free(json);
    ESP_LOGI(TAG, "Device info sent to Firebase");
}

// --- Hàm xử lý điều khiển động cơ từ Firebase ---
static void firebase_motor_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000)); // Chờ WiFi kết nối

    char url[256];
    snprintf(url, sizeof(url),
             "https://%s/esp32_motor.json?auth=%s",
             FIREBASE_HOST, FIREBASE_SECRET);

    ESP_LOGI(TAG, "Firebase URL: %s", url);

    while (1)
    {
        esp_http_client_config_t cfg = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        esp_err_t err = esp_http_client_open(client, 0);

        if (err == ESP_OK)
        {
            int header_status = esp_http_client_fetch_headers(client);
            if (header_status >= 0)
            {
                char buffer[512];
                int len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);

                if (len > 0)
                {
                    buffer[len] = '\0';
                    cJSON *root = cJSON_Parse(buffer);
                    if (root)
                    {
                        cJSON *dir = cJSON_GetObjectItem(root, "direction");
                        cJSON *spd = cJSON_GetObjectItem(root, "speed");

                        // ✅ speed là phần trăm (0–100)
                        uint32_t speed_percent = (cJSON_IsNumber(spd)) ? spd->valueint : 0;
                        if (speed_percent > 100)
                            speed_percent = 100;

                        uint32_t pwm_value = (speed_percent * 1023) / 100;

                        // Hệ số đã tính từ bảng đo
                        float A_rate = 1.0f;
                        // float B_rate = 1.02f;
                        // float C_rate = 1.02f;
                        // float D_rate = 0.95f;
                        float B_rate = 0.94f;
                        float C_rate = 0.94f;
                        float D_rate = 1.2f;

                        // Tính PWM từng bánh
                        uint32_t pwm_A_tmp = (uint32_t)(pwm_value * A_rate);
                        uint32_t pwm_B_tmp = (uint32_t)(pwm_value * B_rate);
                        uint32_t pwm_C_tmp = (uint32_t)(pwm_value * C_rate);
                        uint32_t pwm_D_tmp = (uint32_t)(pwm_value * D_rate);

                        // Giới hạn 0–1023
                        uint32_t pwm_A = (pwm_A_tmp > 1023) ? 1023 : pwm_A_tmp;
                        uint32_t pwm_B = (pwm_B_tmp > 1023) ? 1023 : pwm_B_tmp;
                        uint32_t pwm_C = (pwm_C_tmp > 1023) ? 1023 : pwm_C_tmp;
                        uint32_t pwm_D = (pwm_D_tmp > 1023) ? 1023 : pwm_D_tmp;

                        if (dir && cJSON_IsString(dir))
                        {
                            const char *d = dir->valuestring;
                            ESP_LOGI(TAG, "Direction: %s, Speed: %d%% (PWM=%d)",
                                     d, speed_percent, pwm_value);

                            // Dừng trước khi đổi hướng
                            motor_stop(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2);
                            motor_stop(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2);
                            motor_stop(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2);
                            motor_stop(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2);

                            // Logic điều khiển xe omni (4 bánh)
                            if (strcmp(d, "B") == 0)
                            { //
                                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, true);
                                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, true);
                                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, true);
                                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, true);
                            }
                            else if (strcmp(d, "F") == 0)
                            { //
                                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, false);
                                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, false);
                                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, false);
                                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, false);
                            }
                            else if (strcmp(d, "R") == 0)
                            { //
                                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, false);
                                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, true);
                                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, false);
                                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, true);
                            }
                            else if (strcmp(d, "L") == 0)
                            { //
                                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, true);
                                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, false);
                                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, true);
                                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, false);
                            }
                            // else if (strcmp(d, "BL") == 0)
                            // { //
                            //     motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_value, true);
                            //     motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, 0, true);
                            //     motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_C2, pwm_D, true);
                            //     motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_D2, 0, true);
                            // }
                            // else if (strcmp(d, "BR") == 0)
                            // { //
                            //     motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, 0, true);
                            //     motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_value, true);
                            //     motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_C2, 0, true);
                            //     motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_D2, pwm_value, true);
                            // }
                            else if (strcmp(d, "FL") == 0)
                            { // Lùi phải
                                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, true);
                                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, false);
                                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, false);
                                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, true);
                            }
                            else if (strcmp(d, "FR") == 0)
                            { // Lùi trái
                                motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, pwm_A, false);
                                motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, pwm_B, true);
                                motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, pwm_C, true);
                                motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, pwm_D, false);
                            }
                            else
                            { // Stop hoặc sai dữ liệu
                                motor_stop(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2);
                                motor_stop(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2);
                                motor_stop(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2);
                                motor_stop(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2);
                                ESP_LOGI(TAG, "Stop");
                            }
                        }
                        else
                        {
                            ESP_LOGW(TAG, "Invalid direction");
                        }

                        cJSON_Delete(root);
                    }
                }
            }
        }

        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(500)); // 0.5s đọc lại Firebase
    }
}

// --- Hàm xử lý ADC lên Firebase ---
void adc_task(void *pvParameters)
{
    // Khởi tạo ADC
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));
    // ADC_CHANNEL_6 tương ứng GPIO34

    // Hiệu chuẩn ADC
    adc_cali_handle_t cali_handle = NULL;
    bool do_calibration = false;

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle) == ESP_OK)
    {
        do_calibration = true;
        ESP_LOGI(TAG, "ADC calibration: Line Fitting enabled");
    }
    else
    {
        ESP_LOGW(TAG, "ADC calibration: Line Fitting not supported");
    }

    // Vòng lặp đọc ADC
    int raw = 0;
    int Vout_mV = 0;
    int Vin_mV = 0;
    int Vout_avg = 0;

    while (1)
    {
        char url[256];
        snprintf(url, sizeof(url), "https://%s/esp32_adc.json?auth=%s", FIREBASE_HOST, FIREBASE_SECRET);

        ESP_LOGI(TAG, "Device info sent to Firebase");
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw));

        if (do_calibration)
        {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &Vout_mV));

            // Moving Average Filter
            Vout_samples[sample_index] = Vout_mV;
            sample_index = (sample_index + 1) % NUM_SAMPLES;

            if (read_count < NUM_SAMPLES)
            {
                read_count++;
                ESP_LOGI(TAG, "Đang khởi động: lấy mẫu %d/%d...", read_count, NUM_SAMPLES);
            }
            else
            {
                long sum = 0;
                for (int i = 0; i < NUM_SAMPLES; i++)
                {
                    sum += Vout_samples[i];
                }
                Vout_avg = (int)(sum / NUM_SAMPLES);
                Vin_mV = (int)((float)Vout_avg * VOLTAGE_DIVIDER_RATIO);

                ESP_LOGI(TAG, "Raw: %d | Vout_Avg: %d mV | Vin: %d mV (%.2f V)",
                         raw, Vout_avg, Vin_mV, Vin_mV / 1000.0f);
                if (Vin_mV != adcOld)
                {
                    adcOld = Vin_mV;
                    cJSON *root = cJSON_CreateObject();
                    cJSON_AddNumberToObject(root, "Volt", Vin_mV);
                    char *json = cJSON_PrintUnformatted(root);

                    esp_http_client_config_t cfg = {.url = url, .method = HTTP_METHOD_PUT, .crt_bundle_attach = esp_crt_bundle_attach};
                    esp_http_client_handle_t client = esp_http_client_init(&cfg);
                    esp_http_client_set_header(client, "Content-Type", "application/json");
                    esp_http_client_set_post_field(client, json, strlen(json));
                    esp_http_client_perform(client);
                    esp_http_client_cleanup(client);

                    cJSON_Delete(root);
                    free(json);
                }
            }
        }
        else
        {
            ESP_LOGI(TAG, "Raw: %d (no calibration)", raw);
        }

        // Đọc mỗi giây
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Giải phóng tài nguyên (nếu thoát)
    if (do_calibration)
    {
        ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(cali_handle));
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

    vTaskDelete(NULL); // Kết thúc task
}

// --- Hàm app_main() chính ---
void app_main(void)
{
    ESP_LOGI(TAG, "Starting DRV8833 Firebase Control...");

    wifi_init();

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(10000)); // timeout 10s

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "WiFi connected, now sending info...");
        send_info_to_firebase();
    }
    else
    {
        ESP_LOGW(TAG, "WiFi connection timeout, skipping send_info_to_firebase()");
    }

    drv8833_init();

    xTaskCreate(firebase_motor_task, "firebase_task", 8192, NULL, 5, NULL);
    xTaskCreate(adc_task, "adc_task", 4096, NULL, 5, NULL);
}
