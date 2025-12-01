#include "wifi.h"

bool sta_connected = false;
bool ap_mode_active = false;
bool reconnect_task_running = false;

extern void wifi_on_sta_got_ip(esp_ip4_addr_t ip);

/* ===========================
 * RECONNECT TASK
 * =========================== */
void wifi_reconnect_task(void *pvParameters)
{
    uint8_t retry = 0;

    while (retry < MAX_RETRY && !sta_connected)
    {
        ESP_LOGW(WIFI_TAG, "Reconnecting... (%d)", retry + 1);
        esp_wifi_connect();
        vTaskDelay(pdMS_TO_TICKS(1500));
        retry++;
    }

    if (!sta_connected)
    {
        ESP_LOGE(WIFI_TAG, "Reconnect failed.");
        if (!ap_mode_active)
        {
            ESP_LOGI(WIFI_TAG, "Switching back to AP mode...");
            start_config_ap();
        }
    }

    reconnect_task_running = false;
    vTaskDelete(NULL);
}

/* ===========================
 * START AP MODE
 * =========================== */
void start_config_ap(void)
{
    if (ap_mode_active) return;

    ap_mode_active = true;

    wifi_config_t apcfg = {
        .ap = {
            .ssid = ESP32_WIFI_SSID,
            .ssid_len = strlen(ESP32_WIFI_SSID),
            .password = ESP32_WIFI_PASSWORD,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = 4
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apcfg));

    ESP_LOGI(WIFI_TAG, "AP MODE: SSID=%s PASS=%s",
             ESP32_WIFI_SSID, ESP32_WIFI_PASSWORD);

    start_stream_webserver();   // webserver cấu hình WiFi
}

/* ===========================
 * EVENT HANDLER
 * =========================== */
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(WIFI_TAG, "STA START → waiting for user WiFi input");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(WIFI_TAG, "STA CONNECTED");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(WIFI_TAG, "STA DISCONNECTED");
            sta_connected = false;

            if (!reconnect_task_running)
            {
                reconnect_task_running = true;
                xTaskCreate(wifi_reconnect_task, "wifi_reconnect",
                            4096, NULL, 5, NULL);
            }
            break;

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        sta_connected = true;
        ap_mode_active = false;

        ESP_LOGI(WIFI_TAG, "STA GOT IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        wifi_on_sta_got_ip(event->ip_info.ip);
    }
}

/* ===========================
 * INIT WIFI
 * =========================== */
void wifi_init_sta_or_ap(void)
{
    // 1. Init esp-netif & event loop (chỉ cần gọi 1 lần trong app)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Tạo default netif cho STA và AP
    //    (phải gọi TRƯỚC esp_wifi_init)
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // 3. Init WiFi, tắt NVS cho WiFi (không lưu SSID/PASS)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;                                 // không dùng NVS cho WiFi
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // chỉ lưu config WiFi trong RAM
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // 4. Đăng ký event handler
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // 5. Mode AP + STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // (Nếu bạn có esp_wifi_set_config(AP/STA) thì đặt ở đây)

    // 6. Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 7. Luôn bật AP config portal mỗi lần boot
    start_config_ap();
}
