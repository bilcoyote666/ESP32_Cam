/**
 * @file bt_classic.cpp
 * @brief Implementación BT Classic SPP para PC/Android
 *
 * Protocolo de comandos:
 *   LIST\r\n        → Devuelve JSON con lista de fotos
 *   GET:<filename>  → Devuelve [4 bytes tamaño][datos JPEG]
 *   DEL:<filename>  → Borra el archivo, devuelve "OK\r\n" o "ERR\r\n"
 *   STATUS\r\n      → Devuelve estado del sistema en JSON
 *   CAPTURE\r\n     → Dispara una foto
 */
#include "bt_classic.h"
#include "sd_storage.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "BT_CLASSIC";

static uint32_t                s_spp_handle   = 0;
static bool                    s_connected    = false;
static bt_capture_request_cb_t s_capture_cb   = NULL;
static void*                   s_capture_user = NULL;

// Buffer para comandos recibidos
static char s_cmd_buf[128];
static int  s_cmd_len = 0;

// =============================================================================
// Procesamiento de comandos SPP
// =============================================================================
static void process_spp_command(const char* cmd) {
    ESP_LOGI(TAG, "Comando SPP: %s", cmd);

    if (strcmp(cmd, "LIST") == 0) {
        char json_buf[2048];
        int len = sd_generate_file_list_json(json_buf, sizeof(json_buf));
        if (len > 0) {
            esp_spp_write(s_spp_handle, len, (uint8_t*)json_buf);
            esp_spp_write(s_spp_handle, 2, (uint8_t*)"\r\n");
        }

    } else if (strncmp(cmd, "GET:", 4) == 0) {
        const char* filename = cmd + 4;
        uint8_t* data = NULL;
        size_t   size = 0;

        esp_err_t err = sd_read_photo(filename, &data, &size);
        if (err == ESP_OK) {
            // Enviar cabecera de 4 bytes con el tamaño
            uint8_t size_header[4] = {
                (uint8_t)(size >> 24),
                (uint8_t)(size >> 16),
                (uint8_t)(size >>  8),
                (uint8_t)(size      )
            };
            esp_spp_write(s_spp_handle, 4, size_header);

            // Enviar datos en chunks
            size_t offset = 0;
            while (offset < size) {
                size_t chunk = (size - offset < 4096) ? (size - offset) : 4096;
                esp_spp_write(s_spp_handle, chunk, data + offset);
                offset += chunk;
                vTaskDelay(pdMS_TO_TICKS(10));  // Pausa para no saturar el buffer
            }
            free(data);
            ESP_LOGI(TAG, "Archivo enviado: %s (%zu bytes)", filename, size);
        } else {
            esp_spp_write(s_spp_handle, 11, (uint8_t*)"NOT_FOUND\r\n");
        }

    } else if (strncmp(cmd, "DEL:", 4) == 0) {
        const char* filename = cmd + 4;
        esp_err_t err = sd_delete_photo(filename);
        if (err == ESP_OK) {
            esp_spp_write(s_spp_handle, 4, (uint8_t*)"OK\r\n");
        } else {
            esp_spp_write(s_spp_handle, 6, (uint8_t*)"ERR\r\n");
        }

    } else if (strcmp(cmd, "STATUS") == 0) {
        char status[256];
        uint64_t free_b = 0, total_b = 0;
        sd_get_space_info(&free_b, &total_b);
        int len = snprintf(status, sizeof(status),
            "{\"fw\":\"%s\",\"sd_free_mb\":%llu,\"sd_total_mb\":%llu}\r\n",
            FIRMWARE_VERSION, free_b / 1024 / 1024, total_b / 1024 / 1024);
        if (len > 0) esp_spp_write(s_spp_handle, len, (uint8_t*)status);

    } else if (strcmp(cmd, "CAPTURE") == 0) {
        if (s_capture_cb) s_capture_cb(s_capture_user);
        esp_spp_write(s_spp_handle, 4, (uint8_t*)"OK\r\n");

    } else {
        esp_spp_write(s_spp_handle, 15, (uint8_t*)"UNKNOWN_CMD\r\n");
    }
}

// =============================================================================
// Callback SPP
// =============================================================================
static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    switch (event) {
        case ESP_SPP_INIT_EVT:
            ESP_LOGI(TAG, "SPP inicializado");
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, BT_CLASSIC_DEVICE_NAME);
            break;

        case ESP_SPP_SRV_OPEN_EVT:
            s_spp_handle = param->open.handle;
            s_connected  = true;
            ESP_LOGI(TAG, "Cliente BT Classic conectado");
            esp_spp_write(s_spp_handle, 28, (uint8_t*)"CamaraESP32 listo. Comandos:\r\n");
            break;

        case ESP_SPP_CLOSE_EVT:
            s_connected  = false;
            s_spp_handle = 0;
            ESP_LOGI(TAG, "Cliente BT Classic desconectado");
            break;

        case ESP_SPP_DATA_IND_EVT: {
            // Recibir datos y buscar \r\n para procesar comandos
            uint8_t* data = param->data_ind.data;
            int      len  = param->data_ind.len;

            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                if (c == '\r' || c == '\n') {
                    if (s_cmd_len > 0) {
                        s_cmd_buf[s_cmd_len] = '\0';
                        // Eliminar \r al final si existe
                        if (s_cmd_len > 0 && s_cmd_buf[s_cmd_len - 1] == '\r') {
                            s_cmd_buf[--s_cmd_len] = '\0';
                        }
                        process_spp_command(s_cmd_buf);
                        s_cmd_len = 0;
                    }
                } else if (s_cmd_len < (int)sizeof(s_cmd_buf) - 1) {
                    s_cmd_buf[s_cmd_len++] = c;
                }
            }
            break;
        }

        case ESP_SPP_WRITE_EVT:
            // Escritura completada
            break;

        default:
            break;
    }
}

// =============================================================================
// API Pública
// =============================================================================
esp_err_t bt_classic_init(bt_capture_request_cb_t capture_cb, void* user_data) {
    ESP_LOGI(TAG, "Inicializando BT Classic SPP...");

    s_capture_cb   = capture_cb;
    s_capture_user = user_data;
    s_cmd_len      = 0;

    // Registrar callback SPP
    esp_err_t ret = esp_spp_register_callback(spp_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando callback SPP: %s", esp_err_to_name(ret));
        return ret;
    }

    // Inicializar SPP
    esp_spp_cfg_t spp_cfg = {
        .mode             = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size   = 0,  // Usar buffer por defecto
    };
    ret = esp_spp_enhanced_init(&spp_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando SPP: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configurar nombre del dispositivo BT
    esp_bt_dev_set_device_name(BT_CLASSIC_DEVICE_NAME);

    // Hacer dispositivo visible para emparejamiento
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "BT Classic SPP listo — nombre: '%s'", BT_CLASSIC_DEVICE_NAME);
    return ESP_OK;
}

bool bt_classic_is_connected(void) {
    return s_connected;
}

void bt_classic_notify_new_photo(const char* filename) {
    if (!s_connected || !filename) return;
    char msg[96];
    int len = snprintf(msg, sizeof(msg), "NEW_PHOTO:%s\r\n", filename);
    if (len > 0) {
        esp_spp_write(s_spp_handle, len, (uint8_t*)msg);
    }
}

void bt_classic_deinit(void) {
    esp_spp_deinit();
    s_connected = false;
    ESP_LOGI(TAG, "BT Classic deinicializado");
}
