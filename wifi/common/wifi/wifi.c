#include "wifi.h"

bool sta_connected = false;
bool reconnect_task_running = false;
bool ap_mode_active = false;

void wifi_reconnect_task(void *pvParameters)
{
    uint8_t retry = 0;

    while (retry < MAX_RETRY && !sta_connected)
    {
        ESP_LOGW(TAG, "Reconnecting to Wi-Fi... (%d)", retry + 1);
        esp_err_t err = esp_wifi_connect();

        if (err == ESP_ERR_WIFI_CONN)
        {
            ESP_LOGW(TAG, "Already connecting, skip...");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (!sta_connected)
    {
        ESP_LOGE(TAG, "Failed to reconnect after %d tries.", MAX_RETRY);
        if (!ap_mode_active)
        {
            ESP_LOGW(TAG, "Starting AP mode for configuration...");
            start_config_ap();
        }
        else
        {
            ESP_LOGW(TAG, "AP mode already active, skip starting AP.");
        }
    }

    reconnect_task_running = false;
    vTaskDelete(NULL);
}

/**
 * @brief Start Wi-Fi Access Point (AP) mode for configuration.
 *
 * This function initializes and starts the ESP32 in Access Point mode
 * so that users can connect to it for configuration purposes.
 * If the AP is already active, the function will return.
 *
 * @param None
 * @return None
 */
void start_config_ap(void)
{
    if (ap_mode_active)
        return;

    ap_mode_active = true;

    wifi_config_t ap_config = {
        .ap = {
            .ssid = ESP32_WIFI_SSID,
            .ssid_len = strlen(ESP32_WIFI_SSID),
            .password = ESP32_WIFI_PASSWORD,
            .max_connection = 4,
            .authmode = strlen(ESP32_WIFI_PASSWORD) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_LOGI(TAG, "AP started: SSID=%s, PASS=%s", ESP32_WIFI_SSID, ESP32_WIFI_PASSWORD);
    start_webserver();
}

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi started, begin connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP, starting scan...");
            wifi_scan_config_t scanConf = {
                .ssid = NULL,
                .bssid = NULL,
                .channel = 0,
                .show_hidden = true};
            ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, false));
            sta_connected = true;
            break;

        case WIFI_EVENT_SCAN_DONE:
        {
            uint16_t apCount = 0;
            esp_wifi_scan_get_ap_num(&apCount);
            ESP_LOGI(TAG, "Scan done. Found %d access points.", apCount);

            if (apCount == 0)
                return;

            wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * apCount);
            if (!ap_list)
            {
                ESP_LOGE(TAG, "Memory allocation failed!");
                return;
            }

            esp_wifi_scan_get_ap_records(&apCount, ap_list);

            for (int i = 0; i < apCount; i++)
            {
                const char *authmode;
                switch (ap_list[i].authmode)
                {
                case WIFI_AUTH_OPEN:
                    authmode = "OPEN";
                    break;
                case WIFI_AUTH_WEP:
                    authmode = "WEP";
                    break;
                case WIFI_AUTH_WPA_PSK:
                    authmode = "WPA_PSK";
                    break;
                case WIFI_AUTH_WPA2_PSK:
                    authmode = "WPA2_PSK";
                    break;
                case WIFI_AUTH_WPA_WPA2_PSK:
                    authmode = "WPA_WPA2_PSK";
                    break;
                default:
                    authmode = "UNKNOWN";
                    break;
                }

                ESP_LOGI(TAG, "[%2d] SSID: %s | RSSI: %d | Auth: %s",
                         i + 1, (char *)ap_list[i].ssid, ap_list[i].rssi, authmode);
            }

            free(ap_list);
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from Wi-Fi.");
            sta_connected = false;

            if (!reconnect_task_running) {
                reconnect_task_running = true;
                xTaskCreate(wifi_reconnect_task, "wifi_reconnect_task", 4096, NULL, 5, NULL);
            }
            break;

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        current_ip = event->ip_info.ip;
        start_webserver();
    }
}


void wifi_init_sta_or_ap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    nvs_handle_t nvs;
    size_t len;
    nvs_open("wifi", NVS_READWRITE, &nvs);
    len = sizeof(wifi_ssid);
    if (nvs_get_str(nvs, "ssid", wifi_ssid, &len) != ESP_OK) strcpy(wifi_ssid, "");
    len = sizeof(wifi_pass);
    if (nvs_get_str(nvs, "pass", wifi_pass, &len) != ESP_OK) strcpy(wifi_pass, "");
    nvs_close(nvs);

    start_config_ap();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (strlen(wifi_ssid) > 0)
    {
        ESP_LOGI(TAG, "Connecting to saved WiFi: %s", wifi_ssid);
        wifi_config_t sta_config = {0};
        strcpy((char *)sta_config.sta.ssid, wifi_ssid);
        strcpy((char *)sta_config.sta.password, wifi_pass);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        // ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else
    {
        ESP_LOGW(TAG, "No saved WiFi, running AP only");
    }
}

