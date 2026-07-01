#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi as Access Point (SoftAP) and start captive portal DNS.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ap_init(void);

#ifdef __cplusplus
}
#endif
