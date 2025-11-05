#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "DRV8833"

// ==== GPIO định nghĩa ====
// Mạch DRV8833 #1
#define AIN1_GPIO 18
#define AIN2_GPIO 19
#define BIN1_GPIO 25
#define BIN2_GPIO 26
#define STBY1_GPIO 27

// Mạch DRV8833 #2
#define CIN1_GPIO 32
#define CIN2_GPIO 33
#define DIN1_GPIO 14
#define DIN2_GPIO 15
#define STBY2_GPIO 4

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
        ledc_set_duty(LEDC_MODE, ch1, speed);
        ledc_update_duty(LEDC_MODE, ch1);
        ledc_set_duty(LEDC_MODE, ch2, 0);
        ledc_update_duty(LEDC_MODE, ch2);
    }
    else
    {
        ledc_set_duty(LEDC_MODE, ch1, 0);
        ledc_update_duty(LEDC_MODE, ch1);
        ledc_set_duty(LEDC_MODE, ch2, speed);
        ledc_update_duty(LEDC_MODE, ch2);
    }
}

// ==== Tiện ích ====
#define motorA_forward(s) motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, s, true)
#define motorA_backward(s) motor_control(LEDC_CHANNEL_A1, LEDC_CHANNEL_A2, s, false)
#define motorB_forward(s) motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, s, true)
#define motorB_backward(s) motor_control(LEDC_CHANNEL_B1, LEDC_CHANNEL_B2, s, false)
#define motorC_forward(s) motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, s, true)
#define motorC_backward(s) motor_control(LEDC_CHANNEL_C1, LEDC_CHANNEL_C2, s, false)
#define motorD_forward(s) motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, s, true)
#define motorD_backward(s) motor_control(LEDC_CHANNEL_D1, LEDC_CHANNEL_D2, s, false)

// ==== Ứng dụng chính ====
void app_main(void)
{
    drv8833_init();
    uint32_t speed = 512; // ~50% duty

    while (1)
    {
        ESP_LOGI(TAG, "All motors forward...");
        motorA_forward(speed);
        motorB_forward(speed);
        motorC_forward(speed);
        motorD_forward(speed);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "All motors backward...");
        motorA_backward(speed);
        motorB_backward(speed);
        motorC_backward(speed);
        motorD_backward(speed);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Stop...");
        motorA_forward(0);
        motorB_forward(0);
        motorC_forward(0);
        motorD_forward(0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
