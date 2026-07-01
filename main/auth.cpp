#include "auth.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "AUTH";
static const char* NVS_NAMESPACE = "auth";
static const char* NVS_KEY_PWD = "web_password";

static char s_password[AUTH_MAX_PASSWORD_LEN] = {0};
static bool s_has_password = false;
static char s_session_token[33] = {0};

esp_err_t auth_init(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t len = sizeof(s_password);
        if (nvs_get_str(handle, NVS_KEY_PWD, s_password, &len) == ESP_OK) {
            s_has_password = true;
            ESP_LOGI(TAG, "Password loaded from NVS");
        }
        nvs_close(handle);
    } else {
        ESP_LOGI(TAG, "No password set yet");
    }

    // Generate a random session token for this boot
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    uint32_t r4 = esp_random();
    snprintf(s_session_token, sizeof(s_session_token), "%08lx%08lx%08lx%08lx", 
             (long unsigned int)r1, (long unsigned int)r2, (long unsigned int)r3, (long unsigned int)r4);
    
    return ESP_OK;
}

bool auth_is_password_set(void) {
    return s_has_password;
}

esp_err_t auth_set_password(const char* new_pwd) {
    if (!new_pwd || strlen(new_pwd) >= AUTH_MAX_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_PWD, new_pwd);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err == ESP_OK) {
            strncpy(s_password, new_pwd, sizeof(s_password) - 1);
            s_has_password = true;
            ESP_LOGI(TAG, "Password updated successfully");
        }
    }
    nvs_close(handle);
    return err;
}

bool auth_verify_password(const char* pwd) {
    if (!s_has_password || !pwd) return false;
    return strcmp(s_password, pwd) == 0;
}

esp_err_t auth_clear_password(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(handle, NVS_KEY_PWD);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(handle);
        memset(s_password, 0, sizeof(s_password));
        s_has_password = false;
        ESP_LOGI(TAG, "Password cleared from NVS");
        err = ESP_OK;
    }
    nvs_close(handle);
    return err;
}

const char* auth_get_session_token(void) {
    return s_session_token;
}

bool auth_verify_session(const char* token) {
    if (!s_has_password) return false;
    if (!token) return false;
    return strcmp(s_session_token, token) == 0;
}
