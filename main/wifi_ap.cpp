#include "wifi_ap.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WIFI_AP";

#define DNS_PORT 53

// Tarea para el servidor DNS Captive Portal
static void dns_server_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create IPv4 socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server started on port 53 (Captive Portal)");

    static char rx_buffer[512];
    static char tx_buffer[512];

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len > 12) {
            int copy_len = (len < (int)sizeof(tx_buffer) - 16) ? len : (sizeof(tx_buffer) - 16);
            memcpy(tx_buffer, rx_buffer, copy_len);
            
            // Set Flags to Response, no error
            tx_buffer[2] = 0x81;
            tx_buffer[3] = 0x80;
            // Questions
            tx_buffer[4] = rx_buffer[4];
            tx_buffer[5] = rx_buffer[5];
            
            // Find end of QNAME to discard EDNS0 (Additional RRs)
            // Header is 12 bytes. Question starts at 12.
            int qname_pos = 12;
            while (qname_pos < copy_len - 1 && rx_buffer[qname_pos] != 0) {
                int label_len = (uint8_t)rx_buffer[qname_pos];
                if (qname_pos + label_len + 1 >= copy_len) {
                    break; // Malformed or truncated
                }
                qname_pos += label_len + 1;
            }
            
            if (qname_pos < copy_len && rx_buffer[qname_pos] == 0) {
                int q_end = qname_pos + 1 + 2 + 2; // zero byte + QTYPE (2) + QCLASS (2)
                if (q_end <= copy_len && q_end + 16 <= (int)sizeof(tx_buffer)) {
                    // Set Answer RRs to the number of Questions (usually 1)
                    tx_buffer[6] = rx_buffer[4];
                    tx_buffer[7] = rx_buffer[5];
                    
                    // Clear Authority & Additional RRs = 0
                    tx_buffer[8] = 0; tx_buffer[9] = 0;
                    tx_buffer[10] = 0; tx_buffer[11] = 0;
                    
                    int reply_len = q_end;
                    // Construct DNS Answer appended to the Question
                    // Pointer to the domain name (offset 12)
                    tx_buffer[reply_len++] = 0xC0;
                    tx_buffer[reply_len++] = 0x0C;
                    // Type A (1)
                    tx_buffer[reply_len++] = 0x00;
                    tx_buffer[reply_len++] = 0x01;
                    // Class IN (1)
                    tx_buffer[reply_len++] = 0x00;
                    tx_buffer[reply_len++] = 0x01;
                    // TTL 60 seconds
                    tx_buffer[reply_len++] = 0x00;
                    tx_buffer[reply_len++] = 0x00;
                    tx_buffer[reply_len++] = 0x00;
                    tx_buffer[reply_len++] = 0x3C;
                    // Data length (4 bytes for IPv4)
                    tx_buffer[reply_len++] = 0x00;
                    tx_buffer[reply_len++] = 0x04;
                    // IP Address (192.168.4.1)
                    tx_buffer[reply_len++] = 192;
                    tx_buffer[reply_len++] = 168;
                    tx_buffer[reply_len++] = 4;
                    tx_buffer[reply_len++] = 1;

                    sendto(sock, tx_buffer, reply_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                }
            }
        }
    }
}

#include "auth.h"

esp_err_t wifi_ap_set_password(const char* new_password) {
    if (!new_password || strlen(new_password) < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t wifi_config = {};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_AP, &wifi_config);
    if (err == ESP_OK) {
        memset(wifi_config.ap.password, 0, sizeof(wifi_config.ap.password));
        strncpy((char*)wifi_config.ap.password, new_password, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi AP password actualizado: %s", new_password);
        }
    }
    return err;
}

esp_err_t wifi_ap_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    char pwd[64] = {0};
    auth_get_password(pwd, sizeof(pwd));
    if (strlen(pwd) < 8) {
        strncpy(pwd, WIFI_AP_DEFAULT_PASS, sizeof(pwd) - 1);
    }

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, WIFI_AP_SSID);
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    wifi_config.ap.channel = 1;
    strncpy((char*)wifi_config.ap.password, pwd, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.authmode = (strlen(pwd) >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Desactivar el modo ahorro de energía del WiFi para evitar desconexiones con móviles bloqueados
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "WiFi AP iniciado. SSID: %s | Pass: %s (WPA2)", WIFI_AP_SSID, pwd);

    // Arrancar el DNS Server en background con stack seguro
    xTaskCreate(dns_server_task, "dns_task", 6144, NULL, 5, NULL);

    return ESP_OK;
}
