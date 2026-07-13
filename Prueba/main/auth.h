#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUTH_SESSION_COOKIE_NAME "cam_session"
#define AUTH_MAX_PASSWORD_LEN 64

/**
 * @brief Initialize the auth module (loads password from NVS, generates session token)
 */
esp_err_t auth_init(void);

/**
 * @brief Check if a password has been configured in NVS
 */
bool auth_is_password_set(void);

/**
 * @brief Set the password in NVS
 */
esp_err_t auth_set_password(const char* new_pwd);

/**
 * @brief Verify a password against the one stored in NVS
 */
bool auth_verify_password(const char* pwd);

/**
 * @brief Clear the password from NVS (for factory reset)
 */
esp_err_t auth_clear_password(void);

/**
 * @brief Get the expected session token to send to the client upon login
 */
const char* auth_get_session_token(void);

/**
 * @brief Check if the provided session token is valid
 */
bool auth_verify_session(const char* token);

#ifdef __cplusplus
}
#endif
