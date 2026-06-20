/**
 * @file ble_transfer.cpp
 * @brief Implementación del servidor BLE con NimBLE para transferencia de archivos JPEG
 *
 * Protocolo de transferencia:
 * 1. Cliente lee FILE_LIST → JSON con lista de fotos
 * 2. Cliente escribe en FILE_REQUEST → nombre del archivo
 * 3. Cliente escribe en CONTROL → BLE_CMD_GET_FILE
 * 4. ESP32 envía chunks de 480 bytes por FILE_DATA (Notify)
 *    Cabecera del primer chunk: [0xFF, 0xFE, size_high, size_low, ...]
 *    Chunks siguientes: [seq_high, seq_low, data...]
 *    Último chunk: [0xFF, 0xFF] (EOF)
 * 5. Cliente escribe ACK en CONTROL → 0x10 para confirmar recepción
 */
#include "ble_transfer.h"
#include "sd_storage.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// NimBLE headers
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char* TAG = "BLE_TRANSFER";

// =============================================================================
// Estado interno
// =============================================================================
static ble_state_t             s_state           = BLE_STATE_STOPPED;
static ble_capture_request_cb_t s_capture_cb     = NULL;
static void*                   s_capture_userdata = NULL;

// Handles de las características GATT
static uint16_t s_conn_handle     = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_handle_file_list    = 0;
static uint16_t s_handle_file_request = 0;
static uint16_t s_handle_file_data    = 0;
static uint16_t s_handle_control      = 0;
static uint16_t s_handle_status       = 0;

// Estado de transferencia
static char     s_requested_file[SD_MAX_FILENAME_LEN] = {};
static uint8_t* s_transfer_data     = NULL;
static size_t   s_transfer_size     = 0;
static size_t   s_transfer_offset   = 0;
static int      s_transfer_progress = -1;
static TaskHandle_t s_transfer_task = NULL;

// UUIDs del servicio
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0xAB, 0x89, 0x67, 0x45, 0x23, 0x01, 0x34, 0x12,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);
static const ble_uuid128_t s_chr_file_list_uuid = BLE_UUID128_INIT(
    0xB1, 0x9A, 0x56, 0x34, 0x12, 0x34, 0x12, 0x34,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);
static const ble_uuid128_t s_chr_file_request_uuid = BLE_UUID128_INIT(
    0xB2, 0x9A, 0x56, 0x34, 0x12, 0x34, 0x12, 0x34,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);
static const ble_uuid128_t s_chr_file_data_uuid = BLE_UUID128_INIT(
    0xB3, 0x9A, 0x56, 0x34, 0x12, 0x34, 0x12, 0x34,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);
static const ble_uuid128_t s_chr_control_uuid = BLE_UUID128_INIT(
    0xB4, 0x9A, 0x56, 0x34, 0x12, 0x34, 0x12, 0x34,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);
static const ble_uuid128_t s_chr_status_uuid = BLE_UUID128_INIT(
    0xB5, 0x9A, 0x56, 0x34, 0x12, 0x34, 0x12, 0x34,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);

// =============================================================================
// Tarea de transferencia de archivo (envía chunks via Notify)
// =============================================================================
static void ble_transfer_task_fn(void* pvParam) {
    ESP_LOGI(TAG, "Iniciando transferencia de %s (%zu bytes)", s_requested_file, s_transfer_size);

    s_state             = BLE_STATE_TRANSFERRING;
    s_transfer_offset   = 0;
    s_transfer_progress = 0;

    // Preparar buffer de chunk (con cabecera de 4 bytes)
    uint8_t chunk_buf[BLE_CHUNK_SIZE + 4];
    uint16_t seq = 0;

    // Enviar cabecera inicial: [0xFF, 0xFE, size_high, size_low_high, size_low, filename...]
    // Primer paquete especial con metadatos del archivo
    uint32_t file_size_32 = (uint32_t)s_transfer_size;
    uint8_t header[12];
    header[0] = 0xFF;  // Marcador de inicio
    header[1] = 0xFE;  // Marcador de inicio
    header[2] = (file_size_32 >> 24) & 0xFF;
    header[3] = (file_size_32 >> 16) & 0xFF;
    header[4] = (file_size_32 >>  8) & 0xFF;
    header[5] = (file_size_32 >>  0) & 0xFF;

    struct os_mbuf* om = ble_hs_mbuf_from_flat(header, 6);
    if (om) {
        ble_gatts_notify_custom(s_conn_handle, s_handle_file_data, om);
    }
    vTaskDelay(pdMS_TO_TICKS(20));  // Pausa para que el cliente procese la cabecera

    // Enviar datos en chunks
    while (s_transfer_offset < s_transfer_size &&
           s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {

        size_t remaining = s_transfer_size - s_transfer_offset;
        size_t chunk_len = (remaining < BLE_CHUNK_SIZE) ? remaining : BLE_CHUNK_SIZE;

        // Cabecera de chunk: número de secuencia de 2 bytes
        chunk_buf[0] = (seq >> 8) & 0xFF;
        chunk_buf[1] = (seq     ) & 0xFF;
        memcpy(chunk_buf + 2, s_transfer_data + s_transfer_offset, chunk_len);

        struct os_mbuf* data_om = ble_hs_mbuf_from_flat(chunk_buf, chunk_len + 2);
        if (!data_om) {
            ESP_LOGE(TAG, "Error creando mbuf para chunk %d", seq);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_handle_file_data, data_om);
        if (rc != 0) {
            ESP_LOGW(TAG, "Error enviando chunk %d: %d — reintentando...", seq, rc);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        s_transfer_offset += chunk_len;
        seq++;
        s_transfer_progress = (int)(s_transfer_offset * 100 / s_transfer_size);

        // Pausa mínima entre chunks para no saturar el stack BLE
        vTaskDelay(pdMS_TO_TICKS(5));

        if (seq % 50 == 0) {
            ESP_LOGD(TAG, "Transferencia: %d%% (%zu/%zu bytes)", 
                     s_transfer_progress, s_transfer_offset, s_transfer_size);
        }
    }

    // Enviar marcador EOF
    uint8_t eof_marker[2] = {0xFF, 0xFF};
    struct os_mbuf* eof_om = ble_hs_mbuf_from_flat(eof_marker, 2);
    if (eof_om) {
        ble_gatts_notify_custom(s_conn_handle, s_handle_file_data, eof_om);
    }

    // Liberar buffer
    free(s_transfer_data);
    s_transfer_data     = NULL;
    s_transfer_size     = 0;
    s_transfer_offset   = 0;
    s_transfer_progress = -1;

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        s_state = BLE_STATE_CONNECTED;
    } else {
        s_state = BLE_STATE_ADVERTISING;
    }

    ESP_LOGI(TAG, "Transferencia completada: %d chunks enviados", seq);
    s_transfer_task = NULL;
    vTaskDelete(NULL);
}

// =============================================================================
// Handler de acceso a características GATT
// =============================================================================
static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt* ctxt, void* arg) {
    int rc = 0;

    // FILE_LIST — Lectura: devuelve JSON con lista de fotos
    if (attr_handle == s_handle_file_list) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            char json_buf[1024];
            int len = sd_generate_file_list_json(json_buf, sizeof(json_buf));
            if (len > 0) {
                rc = os_mbuf_append(ctxt->om, json_buf, len);
            }
        }
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // FILE_REQUEST — Escritura: nombre del archivo a transferir/borrar
    if (attr_handle == s_handle_file_request) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len >= SD_MAX_FILENAME_LEN) len = SD_MAX_FILENAME_LEN - 1;
            os_mbuf_copydata(ctxt->om, 0, len, s_requested_file);
            s_requested_file[len] = '\0';
            ESP_LOGI(TAG, "Archivo solicitado: %s", s_requested_file);
        }
        return 0;
    }

    // CONTROL — Escritura: comandos
    if (attr_handle == s_handle_control) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint8_t cmd = 0;
            os_mbuf_copydata(ctxt->om, 0, 1, &cmd);
            ESP_LOGI(TAG, "Comando BLE recibido: 0x%02X", cmd);

            switch (cmd) {
                case BLE_CMD_LIST_FILES: {
                    // Notificar lista actualizada
                    char json_buf[1024];
                    int len = sd_generate_file_list_json(json_buf, sizeof(json_buf));
                    if (len > 0 && s_handle_file_list != 0) {
                        struct os_mbuf* om = ble_hs_mbuf_from_flat(json_buf, len);
                        if (om) ble_gatts_notify_custom(conn_handle, s_handle_file_list, om);
                    }
                    break;
                }

                case BLE_CMD_GET_FILE: {
                    if (s_transfer_task != NULL) {
                        ESP_LOGW(TAG, "Ya hay una transferencia en curso");
                        break;
                    }
                    if (strlen(s_requested_file) == 0) {
                        ESP_LOGW(TAG, "Ningún archivo solicitado");
                        break;
                    }
                    // Leer archivo de la SD
                    esp_err_t err = sd_read_photo(
                        s_requested_file, &s_transfer_data, &s_transfer_size
                    );
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Error leyendo archivo para BLE");
                        break;
                    }
                    // Lanzar tarea de transferencia
                    xTaskCreatePinnedToCore(
                        ble_transfer_task_fn, "ble_tx",
                        TASK_BLE_STACK, NULL,
                        TASK_BLE_PRIO, &s_transfer_task,
                        TASK_BLE_CORE
                    );
                    break;
                }

                case BLE_CMD_DELETE_FILE:
                    if (strlen(s_requested_file) > 0) {
                        sd_delete_photo(s_requested_file);
                        memset(s_requested_file, 0, sizeof(s_requested_file));
                    }
                    break;

                case BLE_CMD_CANCEL:
                    if (s_transfer_task != NULL) {
                        vTaskDelete(s_transfer_task);
                        s_transfer_task = NULL;
                        if (s_transfer_data) { free(s_transfer_data); s_transfer_data = NULL; }
                        s_state = BLE_STATE_CONNECTED;
                    }
                    break;

                case BLE_CMD_CAPTURE_NOW:
                    if (s_capture_cb) {
                        s_capture_cb(s_capture_userdata);
                    }
                    break;

                case BLE_CMD_GET_STATUS: {
                    // Enviar estado del sistema como string
                    char status_buf[128];
                    int len = snprintf(status_buf, sizeof(status_buf),
                        "{\"state\":%d,\"progress\":%d,\"fw\":\"%s\"}",
                        (int)s_state, s_transfer_progress, FIRMWARE_VERSION);
                    if (len > 0 && s_handle_status != 0) {
                        struct os_mbuf* om = ble_hs_mbuf_from_flat(status_buf, len);
                        if (om) ble_gatts_notify_custom(conn_handle, s_handle_status, om);
                    }
                    break;
                }
            }
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// =============================================================================
// Definición del servicio GATT
// =============================================================================
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type     = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid     = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            // FILE_LIST: Read + Notify
            {
                .uuid       = &s_chr_file_list_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_handle_file_list,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            // FILE_REQUEST: Write Without Response
            {
                .uuid       = &s_chr_file_request_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_handle_file_request,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            // FILE_DATA: Notify (chunks de datos JPEG)
            {
                .uuid       = &s_chr_file_data_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_handle_file_data,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            // CONTROL: Write
            {
                .uuid       = &s_chr_control_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_handle_control,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            // STATUS: Read + Notify
            {
                .uuid       = &s_chr_status_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_handle_status,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },  // Terminador
        },
    },
    { 0 },  // Terminador
};

// =============================================================================
// Callbacks GAP (conexión/desconexión)
// =============================================================================
static int gap_event_cb(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_state       = BLE_STATE_CONNECTED;
                ESP_LOGI(TAG, "Cliente BLE conectado — handle: %d", s_conn_handle);

                // Solicitar MTU máximo (512 bytes)
                ble_att_set_preferred_mtu(BLE_MTU_SIZE);
                ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);

                // Solicitar PHY 2M para mayor velocidad
                ble_gap_set_prefered_le_phy(
                    s_conn_handle,
                    BLE_GAP_LE_PHY_2M_MASK,
                    BLE_GAP_LE_PHY_2M_MASK,
                    BLE_GAP_LE_PHY_CODED_ANY
                );
            } else {
                ESP_LOGW(TAG, "Conexión BLE fallida: %d", event->connect.status);
                s_state = BLE_STATE_ADVERTISING;
                ble_transfer_start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Cliente BLE desconectado — razón: %d",
                     event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_state       = BLE_STATE_ADVERTISING;
            // Cancelar transferencia si estaba en curso
            if (s_transfer_task) {
                vTaskDelete(s_transfer_task);
                s_transfer_task = NULL;
                if (s_transfer_data) { free(s_transfer_data); s_transfer_data = NULL; }
                s_transfer_progress = -1;
            }
            // Reiniciar advertising
            ble_transfer_start_advertising();
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU negociado: %d bytes", event->mtu.value);
            break;

        default:
            break;
    }
    return 0;
}

// =============================================================================
// Sincronización del stack NimBLE
// =============================================================================
static void ble_on_sync(void) {
    esp_err_t rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error configurando dirección BLE");
        return;
    }
    ESP_LOGI(TAG, "BLE stack sincronizado");
    ble_transfer_start_advertising();
}

static void ble_on_reset(int reason) {
    ESP_LOGW(TAG, "BLE reset: %d", reason);
}

// Tarea host NimBLE (requerida por NimBLE)
static void nimble_host_task(void* param) {
    ESP_LOGI(TAG, "Tarea NimBLE host iniciada");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// =============================================================================
// API Pública
// =============================================================================
esp_err_t ble_transfer_init(ble_capture_request_cb_t capture_cb, void* user_data) {
    ESP_LOGI(TAG, "Inicializando BLE (NimBLE)...");

    s_capture_cb       = capture_cb;
    s_capture_userdata = user_data;
    s_conn_handle      = BLE_HS_CONN_HANDLE_NONE;

    // Inicializar NimBLE
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando NimBLE: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configurar callbacks del host
    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // Configurar servicios GAP
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    ble_svc_gatt_init();

    // Registrar servicio GATT
    ret = ble_gatts_count_cfg(s_gatt_svcs);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error contando GATT config: %d", ret);
        return ESP_FAIL;
    }
    ret = ble_gatts_add_svcs(s_gatt_svcs);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error añadiendo servicios GATT: %d", ret);
        return ESP_FAIL;
    }

    // Lanzar tarea host NimBLE
    nimble_port_freertos_init(nimble_host_task);

    s_state = BLE_STATE_ADVERTISING;
    ESP_LOGI(TAG, "BLE inicializado correctamente");
    return ESP_OK;
}

esp_err_t ble_transfer_start_advertising(void) {
    struct ble_gap_adv_params adv_params = {};
    struct ble_hs_adv_fields fields      = {};

    // Configurar datos de advertising
    fields.flags               = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl          = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char* name = ble_svc_gap_device_name();
    fields.name             = (uint8_t*)name;
    fields.name_len         = strlen(name);
    fields.name_is_complete = 1;

    // Añadir UUID del servicio para que los clientes lo identifiquen
    fields.uuids128             = &s_svc_uuid;
    fields.num_uuids128         = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error configurando advertising fields: %d", rc);
        return ESP_FAIL;
    }

    // Advertising general, sin límite de tiempo
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = BLE_GAP_ADV_ITVL_MS(100);  // 100ms intervalo
    adv_params.itvl_max  = BLE_GAP_ADV_ITVL_MS(200);  // 200ms intervalo

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Error iniciando advertising: %d", rc);
        return ESP_FAIL;
    }

    s_state = BLE_STATE_ADVERTISING;
    ESP_LOGI(TAG, "BLE Advertising iniciado — busca '%s' en tu dispositivo", BLE_DEVICE_NAME);
    return ESP_OK;
}

void ble_transfer_stop_advertising(void) {
    ble_gap_adv_stop();
    s_state = BLE_STATE_STOPPED;
}

void ble_transfer_notify_new_photo(const char* filename) {
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    // Enviar notificación en STATUS con nombre de la nueva foto
    char notify_buf[80];
    int len = snprintf(notify_buf, sizeof(notify_buf),
                       "{\"event\":\"new_photo\",\"file\":\"%s\"}", filename);
    if (len > 0 && s_handle_status != 0) {
        struct os_mbuf* om = ble_hs_mbuf_from_flat(notify_buf, len);
        if (om) ble_gatts_notify_custom(s_conn_handle, s_handle_status, om);
    }
}

ble_state_t ble_transfer_get_state(void)            { return s_state; }
bool        ble_transfer_is_connected(void)          { return s_conn_handle != BLE_HS_CONN_HANDLE_NONE; }
uint8_t     ble_transfer_get_connection_count(void)  { return (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) ? 1 : 0; }
int         ble_transfer_get_progress(void)          { return s_transfer_progress; }

void ble_transfer_deinit(void) {
    ble_transfer_stop_advertising();
    nimble_port_stop();
    nimble_port_deinit();
    s_state = BLE_STATE_STOPPED;
    ESP_LOGI(TAG, "BLE deinicializado");
}
