#include <stdio.h>
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" 

static const char *TAG = "ADC_EXAMPLE";

// CẤU HÌNH LỌC VÀ HỆ SỐ CHIA ÁP
// Ratio thực tế dựa trên đo lường (7.38V / 2.349V ≈ 3.14176)
#define VOLTAGE_DIVIDER_RATIO (3.14176f) 
#define NUM_SAMPLES     10 // Số lượng mẫu để lấy trung bình

// Khai báo mảng tĩnh cho bộ lọc và chỉ số
static int Vout_samples[NUM_SAMPLES] = {0};
static int sample_index = 0;
// KHAI BÁO BIẾN ĐẾM TĨNH để nó giữ giá trị giữa các lần lặp while(1)
static int read_count = 0; 


void app_main(void)
{
    // 1️⃣ Khởi tạo ADC đơn (oneshot)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,  // Dải đo tối đa ~3.3V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));
    // ADC_CHANNEL_6 = GPIO34 trên ESP32

    // 2️⃣ Hiệu chuẩn (ADC calibration)
    adc_cali_handle_t cali_handle = NULL;
    bool do_calibration = false;

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle) == ESP_OK) {
        do_calibration = true;
        ESP_LOGI(TAG, "ADC calibration: Line Fitting enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration: Line Fitting not supported");
    }

    // 3️⃣ Đọc giá trị ADC và tính V_in
    int raw = 0;
    int Vout_mV = 0;   // Điện áp đo được tại R2 (Vout)
    int Vin_mV = 0;    // Điện áp nguồn vào (Vin)
    int Vout_avg = 0;  // Giá trị Vout đã được lọc

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw));

        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &Vout_mV));

            // BỘ LỌC TRUNG BÌNH ĐỘNG (Moving Average Filter)
            
            // 1. Lưu giá trị Vout_mV mới vào mảng
            Vout_samples[sample_index] = Vout_mV;
            
            // 2. Cập nhật chỉ số (xoay vòng)
            sample_index = (sample_index + 1) % NUM_SAMPLES;
            
            
            if (read_count < NUM_SAMPLES) {
                // Tăng biến đếm và thông báo trong thời gian khởi động
                read_count++;
                ESP_LOGI(TAG, "DANG KHOI DONG: Lay mau thu %d/%d...", read_count, NUM_SAMPLES);
                
            } else {
                // Đủ mẫu, tiến hành lọc và in kết quả
                
                // 3. Tính tổng và lấy giá trị Vout trung bình
                long sum = 0;
                for (int i = 0; i < NUM_SAMPLES; i++) {
                    sum += Vout_samples[i];
                }
                Vout_avg = (int)(sum / NUM_SAMPLES);

                // 4. TÍNH TOÁN V_in DỰA TRÊN Vout_avg
                Vin_mV = (int)((float)Vout_avg * VOLTAGE_DIVIDER_RATIO);
                
                // In Vin dưới dạng Volts
                ESP_LOGI(TAG, "Raw: %d | Vout_Avg: %d mV | Vin: %d mV (%.2f V)", 
                         raw, 
                         Vout_avg, 
                         Vin_mV, 
                         (float)Vin_mV / 1000.0f);
            }
        } else {
            ESP_LOGI(TAG, "Raw: %d (no calibration)", raw);
        }

        // Đợi 1 giây trước lần đọc tiếp theo
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 4️⃣ Giải phóng tài nguyên (thực tế không chạy do vòng lặp vô hạn)
    if (do_calibration) {
        ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(cali_handle));
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}