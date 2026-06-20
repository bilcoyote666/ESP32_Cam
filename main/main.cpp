/**
 * @file main.cpp
 * @brief Punto de entrada principal — CámaraESP32 con OV5640
 *
 * Placa:   Freenove ESP32-S3-WROOM CAM (N16R8)
 * Cámara:  OV5640 5MP con autoenfoque hardware
 * Storage: MicroSD via SDMMC 4-bit
 * Radio:   BLE 5.0 (NimBLE) + BT Classic SPP
 *
 * Arquitectura de tareas FreeRTOS:
 *   Core 0: face_detect_task (intensivo en CPU/AI)
 *   Core 1: capture_task, NimBLE host task, bt_classic_task
 *
 * Flujo:
 *   1. Arranque → inicializar todos los subsistemas
 *   2. Face detection en loop continuo (Core 0)
 *   3. Al detectar cara → activa AF + flag
 *   4. Botón (corto) → dispara foto inmediata
 *   5. Botón (largo)  → modo ráfaga (3 fotos)
 *   6. BLE CMD_CAPTURE → dispara foto
 *   7. Foto: AF → captura UXGA → guarda SD → notifica BLE/BT
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "config.h"
#include "camera.h"
#include "face_detect.h"
#include "sd_storage.h"
#include "ble_transfer.h"
#include "bt_classic.h"
#include "button.h"
#include "led.h"

static const char* TAG = "MAIN";

// =============================================================================
// Estado global del sistema
// =============================================================================
static volatile system_state_t s_sys_state = SYS_STATE_INIT;
static QueueHandle_t           s_capture_queue = NULL;  // Cola de mensajes de captura
static SemaphoreHandle_t       s_capture_mutex = NULL;  // Mutex: solo 1 captura a la vez

// Control de auto-disparo por detección de caras
static volatile uint32_t s_last_face_capture_ms = 0;
static volatile bool     s_face_auto_capture     = false;  // false = solo AF, no dispara auto

// =============================================================================
// Actualización del estado del sistema
// =============================================================================
static void set_system_state(system_state_t new_state) {
    if (s_sys_state == new_state) return;
    s_sys_state = new_state;
    led_update_from_state(new_state);
    ESP_LOGI(TAG, "Estado sistema: %d", (int)new_state);
}

// =============================================================================
// CALLBACK: Cara detectada (llamado desde face_detect_task en Core 0)
// =============================================================================
static void on_face_detected(const face_result_t* result, void* user_data) {
    if (!result || !result->detected) return;

    ESP_LOGD(TAG, "Cara detectada: %.2f confianza", result->confidence);

    // Activar AF del OV5640 en la región de la cara
    // (el AF por hardware del OV5640 actúa sobre toda la imagen,
    //  pero podemos ajustar la región de interés vía I2C si queremos)
    // Por ahora, simplemente marcamos que hay cara para el AF

    // Auto-disparo: si está activado y ha pasado el intervalo mínimo
    if (s_face_auto_capture) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - s_last_face_capture_ms >= FACE_DETECT_MIN_INTERVAL_MS) {
            capture_msg_t msg = {
                .trigger      = MSG_CAPTURE_FACE,
                .burst_count  = 1
            };
            // Enviar a la cola sin bloquear (si la cola está llena, ignorar)
            xQueueSendFromISR(s_capture_queue, &msg, NULL);
            s_last_face_capture_ms = now_ms;
        }
    }
}

// =============================================================================
// CALLBACK: Botón pulsado
// =============================================================================
static void on_button_event(button_event_t event, void* user_data) {
    capture_msg_t msg = {};

    if (event == BTN_EVENT_SHORT_PRESS) {
        ESP_LOGI(TAG, "Botón: pulsación corta → captura");
        msg.trigger     = MSG_CAPTURE_BTN;
        msg.burst_count = 1;
        xQueueSend(s_capture_queue, &msg, 0);

    } else if (event == BTN_EVENT_LONG_PRESS) {
        ESP_LOGI(TAG, "Botón: pulsación larga → ráfaga x3");
        msg.trigger     = MSG_BURST_START;
        msg.burst_count = 3;
        xQueueSend(s_capture_queue, &msg, 0);
    }
}

// =============================================================================
// CALLBACK: BLE solicita captura
// =============================================================================
static void on_ble_capture_request(void* user_data) {
    ESP_LOGI(TAG, "BLE: solicitud de captura remota");
    capture_msg_t msg = {
        .trigger     = MSG_CAPTURE_BLE,
        .burst_count = 1
    };
    xQueueSend(s_capture_queue, &msg, 0);
}

// =============================================================================
// TAREA DE CAPTURA — Core 1
// Espera mensajes en la cola y gestiona el ciclo completo de captura
// =============================================================================
static void capture_task(void* pvParam) {
    ESP_LOGI(TAG, "Tarea de captura iniciada en Core %d", xPortGetCoreID());

    capture_msg_t msg;
    char last_filename[SD_MAX_FILENAME_LEN];

    while (true) {
        // Esperar mensaje de captura (bloqueante indefinidamente)
        if (xQueueReceive(s_capture_queue, &msg, portMAX_DELAY) != pdTRUE) continue;

        // Mutex: solo 1 captura a la vez
        if (xSemaphoreTake(s_capture_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "Captura en curso, ignorando nueva solicitud");
            continue;
        }

        uint8_t num_shots = (msg.trigger == MSG_BURST_START) ? msg.burst_count : 1;

        for (uint8_t shot = 0; shot < num_shots; shot++) {
            ESP_LOGI(TAG, "=== CAPTURA %d/%d (trigger=%d) ===", shot + 1, num_shots, msg.trigger);

            // 1. Pausar face detection para liberar acceso a la cámara
            face_detect_pause();

            // 2. Cambiar a modo CAPTURE (alta resolución)
            set_system_state(SYS_STATE_FOCUSING);
            esp_err_t err = camera_set_mode(CAMERA_MODE_CAPTURE);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error cambiando a modo CAPTURE");
                set_system_state(SYS_STATE_ERROR_CAM);
                break;
            }

            // 3. Disparar autoenfoque y esperar resultado
            af_result_t af = camera_trigger_autofocus();
            if (af == AF_RESULT_SUCCESS) {
                ESP_LOGI(TAG, "AF conseguido ✓");
            } else if (af == AF_RESULT_TIMEOUT) {
                ESP_LOGW(TAG, "AF timeout — capturando sin AF óptimo");
            } else {
                ESP_LOGW(TAG, "AF error — capturando sin AF");
            }

            // 4. Capturar frame JPEG
            set_system_state(SYS_STATE_CAPTURING);
            led_flash_confirm(1);  // Flash de confirmación

            camera_fb_t* fb = camera_capture_frame();
            if (!fb) {
                ESP_LOGE(TAG, "Error capturando frame");
                set_system_state(SYS_STATE_ERROR_CAM);
                break;
            }

            ESP_LOGI(TAG, "Foto capturada: %zu bytes (%dx%d)",
                     fb->len, fb->width, fb->height);

            // 5. Guardar en MicroSD
            set_system_state(SYS_STATE_SAVING);
            memset(last_filename, 0, sizeof(last_filename));
            err = sd_save_photo(fb->buf, fb->len, last_filename);

            // Liberar frame buffer (ya copiado a la SD)
            camera_free_frame(fb);

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error guardando en SD");
                set_system_state(SYS_STATE_ERROR_SD);
                break;
            }

            ESP_LOGI(TAG, "Foto guardada: %s", last_filename);

            // 6. Notificar a BLE y BT Classic
            ble_transfer_notify_new_photo(last_filename);
            bt_classic_notify_new_photo(last_filename);

            // 7. Volver a modo DETECT para el siguiente frame
            camera_set_mode(CAMERA_MODE_DETECT);
            face_detect_resume();
            set_system_state(SYS_STATE_DETECTING);

            // Pausa entre fotos de ráfaga
            if (shot < num_shots - 1) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }

        // Asegurar que volvemos al estado correcto
        if (s_sys_state == SYS_STATE_CAPTURING ||
            s_sys_state == SYS_STATE_SAVING    ||
            s_sys_state == SYS_STATE_FOCUSING) {
            camera_set_mode(CAMERA_MODE_DETECT);
            face_detect_resume();
            set_system_state(SYS_STATE_DETECTING);
        }

        xSemaphoreGive(s_capture_mutex);
    }
}

// =============================================================================
// PUNTO DE ENTRADA PRINCIPAL — app_main()
// =============================================================================
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "  CámaraESP32 — Firmware v%s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "  Placa: Freenove ESP32-S3-WROOM CAM");
    ESP_LOGI(TAG, "  Cámara: OV5640 5MP");
    ESP_LOGI(TAG, "=============================================");

    esp_err_t err;

    // -------------------------------------------------------------------------
    // 1. Inicializar NVS (requerido por BT y WiFi)
    // -------------------------------------------------------------------------
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrompido — borrando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
    ESP_LOGI(TAG, "[1/8] NVS inicializado ✓");

    // -------------------------------------------------------------------------
    // 2. Inicializar LED de estado
    // -------------------------------------------------------------------------
    ESP_ERROR_CHECK(led_init());
    led_set_pattern(LED_PATTERN_SAVING);  // Parpadeo rápido durante init
    ESP_LOGI(TAG, "[2/8] LED inicializado ✓");

    // -------------------------------------------------------------------------
    // 3. Inicializar MicroSD
    // -------------------------------------------------------------------------
    err = sd_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[3/8] ERROR: MicroSD no disponible — %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Verifica: ¿Tarjeta insertada? ¿Formateada en FAT32?");
        set_system_state(SYS_STATE_ERROR_SD);
        // Continuar sin SD — el resto puede funcionar
    } else {
        ESP_LOGI(TAG, "[3/8] MicroSD inicializada ✓");
    }

    // -------------------------------------------------------------------------
    // 4. Inicializar Cámara OV5640
    // -------------------------------------------------------------------------
    err = camera_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[4/8] ERROR CRÍTICO: Cámara OV5640 no disponible");
        ESP_LOGE(TAG, "Verifica: ¿Cámara conectada? ¿Pines correctos en config.h?");
        set_system_state(SYS_STATE_ERROR_CAM);
        // Sin cámara no hay sentido continuar, pero no bloqueamos
    } else {
        ESP_LOGI(TAG, "[4/8] Cámara OV5640 inicializada ✓");
    }

    // -------------------------------------------------------------------------
    // 5. Crear cola y mutex de captura
    // -------------------------------------------------------------------------
    s_capture_queue = xQueueCreate(5, sizeof(capture_msg_t));
    s_capture_mutex = xSemaphoreCreateMutex();
    if (!s_capture_queue || !s_capture_mutex) {
        ESP_LOGE(TAG, "ERROR: No se pudo crear cola/mutex de captura");
        esp_restart();
    }
    ESP_LOGI(TAG, "[5/8] Cola de captura creada ✓");

    // -------------------------------------------------------------------------
    // 6. Inicializar Bluetooth (NVS debe estar ya init)
    // -------------------------------------------------------------------------
    // Liberar memoria de Classic o BLE según configuración
    // Usamos modo completo (BT Classic + BLE) → no liberamos nada
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_IDLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));  // BT Classic + BLE
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Inicializar BLE (NimBLE) para iPhone/Mac/Android moderno
    err = ble_transfer_init(on_ble_capture_request, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[6/8] BLE no disponible: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "[6/8] BLE (NimBLE) inicializado ✓");
    }

    // Inicializar BT Classic SPP para PC/Android legacy
    err = bt_classic_init(on_ble_capture_request, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "       BT Classic no disponible: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "       BT Classic SPP inicializado ✓");
    }

    // -------------------------------------------------------------------------
    // 7. Inicializar Face Detection
    // -------------------------------------------------------------------------
    err = face_detect_init(on_face_detected, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[7/8] Face detection no disponible");
    } else {
        err = face_detect_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "[7/8] Face detection iniciada ✓");
        }
    }

    // -------------------------------------------------------------------------
    // 8. Inicializar Botón
    // -------------------------------------------------------------------------
    err = button_init(on_button_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[8/8] ERROR: Botón no disponible");
    } else {
        ESP_LOGI(TAG, "[8/8] Botón inicializado ✓");
    }

    // -------------------------------------------------------------------------
    // Sistema listo
    // -------------------------------------------------------------------------
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✓ Sistema listo");
    ESP_LOGI(TAG, "  BLE:         Busca '%s' en tu dispositivo", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, "  BT Classic:  Empareja '%s' en PC/Android", BT_CLASSIC_DEVICE_NAME);
    ESP_LOGI(TAG, "  Botón:       GPIO%d (BOOT button)", PIN_BTN_CAPTURE);
    ESP_LOGI(TAG, "  LED:         GPIO%d (estado del sistema)", PIN_LED_STATUS);
    ESP_LOGI(TAG, "");

    set_system_state(SYS_STATE_DETECTING);
    led_set_pattern(LED_PATTERN_DETECTING);

    // -------------------------------------------------------------------------
    // Lanzar tarea de captura en Core 1
    // -------------------------------------------------------------------------
    xTaskCreatePinnedToCore(
        capture_task,
        "capture_task",
        TASK_CAPTURE_STACK,
        NULL,
        TASK_CAPTURE_PRIO,
        NULL,
        TASK_CAPTURE_CORE
    );

    // -------------------------------------------------------------------------
    // Loop principal — monitoreo del sistema
    // app_main puede terminar; las tareas FreeRTOS siguen ejecutándose
    // -------------------------------------------------------------------------
    while (true) {
        // Log de estado cada 30 segundos
        static uint32_t last_log_ms = 0;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        if (now_ms - last_log_ms >= 30000) {
            last_log_ms = now_ms;

            uint64_t free_b = 0, total_b = 0;
            if (sd_storage_is_ready()) {
                sd_get_space_info(&free_b, &total_b);
            }

            ESP_LOGI(TAG, "--- ESTADO ---");
            ESP_LOGI(TAG, "  Sistema:     %d", (int)s_sys_state);
            ESP_LOGI(TAG, "  BLE:         %s", ble_transfer_is_connected() ? "conectado" : "advertising");
            ESP_LOGI(TAG, "  BT Classic:  %s", bt_classic_is_connected() ? "conectado" : "disponible");
            ESP_LOGI(TAG, "  SD Libre:    %.1f GB", free_b / 1024.0 / 1024.0 / 1024.0);
            ESP_LOGI(TAG, "  Face FPS:    %.1f", face_detect_get_fps());
            ESP_LOGI(TAG, "  RAM libre:   %lu KB", esp_get_free_heap_size() / 1024);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
