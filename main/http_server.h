#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Web HTTP Server and Captive Portal endpoints.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_server_start(void);

#ifdef __cplusplus
}
#endif
