#include "wifi.h"
//#include <lwip / sockets .h>





/* Hàm chính */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_sta_or_ap();
}

