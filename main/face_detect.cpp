/**
 * @file face_detect.cpp
 * @brief Implementación de detección de caras con esp-who MTMN
 *
 * El modelo MTMN (Multi-task Cascaded Convolutional Networks) es el
 * modelo de referencia de Espressif para face detection en ESP32-S3.
 * Corre en el acelerador vectorial del ESP32-S3 para mayor velocidad.
 */
#include "face_detect.h"
#include "camera.h"
#include "config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// esp-who incluye el modelo MTMN y el pipeline de detección
// Si esp-who no está disponible, usamos detección stub para compilar
#if __has_include("who_camera.h")
    #include "who_camera.h"
    #include "who_human_face_detection.hpp"
    #define HAVE_ESP_WHO 1
#else
    // Stub: el código compila pero la detección devuelve siempre false
    // Instalar esp-who: https://github.com/espressif/esp-who
    #define HAVE_ESP_WHO 0
    #warning "esp-who no encontrado. La detección de caras estará desactivada."
    #warning "Instala esp-who como componente IDF y recompila."
#endif

static const char* TAG = "FACE_DETECT";

// Estado interno
static TaskHandle_t         s_task_handle   = NULL;
static face_detected_cb_t   s_callback      = NULL;
static void*                s_user_data     = NULL;
static face_result_t        s_last_result   = {};
static SemaphoreHandle_t    s_result_mutex  = NULL;
static volatile bool        s_running       = false;
static volatile bool        s_paused        = false;

// Métricas de rendimiento
static float     s_fps         = 0.0f;
static uint32_t  s_fps_counter = 0;
static uint32_t  s_fps_ms_start = 0;

// =============================================================================
// Tarea de detección (Core 0)
// =============================================================================
static void face_detect_task(void* pvParam) {
    ESP_LOGI(TAG, "Tarea de detección iniciada en Core %d", xPortGetCoreID());

    s_fps_ms_start = xTaskGetTickCount() * portTICK_PERIOD_MS;

#if HAVE_ESP_WHO
    // Inicializar el pipeline de detección MTMN
    // El modelo se carga automáticamente desde flash al PSRAM
    HumanFaceDetectMSR01 detect(
        FACE_DETECT_THRESHOLD,     // Umbral de score
        FACE_DETECT_NMS_THRESHOLD, // NMS threshold
        1,                         // Pirámide de escalas
        0.5f                       // Factor de escala
    );
#endif

    while (s_running) {
        // Si está pausado, esperar
        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Capturar frame en modo DETECT (QVGA)
        camera_fb_t* fb = camera_capture_frame();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        face_result_t result = {};

#if HAVE_ESP_WHO
        // Análisis MTMN
        // El modelo espera imagen en formato RGB888 o RGB565
        // esp_camera en modo DETECT devuelve JPEG, necesitamos convertir
        // Para QVGA con esp-who, configurar pixel_format = PIXFORMAT_RGB565
        // y usar el dl_matrix3du_t apropiado

        std::list<dl::detect::result_t> detect_results =
            detect.infer((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3});

        result.face_count = detect_results.size();
        result.detected   = !detect_results.empty();

        if (result.detected) {
            // Tomar el resultado con mayor confianza (el primero es el mejor)
            auto& best = detect_results.front();
            result.confidence = best.score;
            result.x          = best.box[0];
            result.y          = best.box[1];
            result.width      = best.box[2] - best.box[0];
            result.height     = best.box[3] - best.box[1];

            ESP_LOGD(TAG, "Cara detectada: confianza=%.2f, pos=(%d,%d,%dx%d)",
                     result.confidence, result.x, result.y, result.width, result.height);
        }
#else
        // Sin esp-who: siempre false
        result.detected   = false;
        result.face_count = 0;
        result.confidence = 0.0f;
        // Simular detección periódica para pruebas (QUITAR en producción)
        // static uint32_t stub_counter = 0;
        // if (++stub_counter % 100 == 0) { result.detected = true; result.confidence = 0.9f; }
#endif

        camera_free_frame(fb);

        // Actualizar resultado con mutex
        xSemaphoreTake(s_result_mutex, portMAX_DELAY);
        s_last_result = result;
        xSemaphoreGive(s_result_mutex);

        // Llamar callback si hay cara detectada
        if (result.detected && s_callback) {
            s_callback(&result, s_user_data);
        }

        // Calcular FPS
        s_fps_counter++;
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = now_ms - s_fps_ms_start;
        if (elapsed >= 1000) {
            s_fps = (float)s_fps_counter * 1000.0f / (float)elapsed;
            s_fps_counter  = 0;
            s_fps_ms_start = now_ms;
            ESP_LOGD(TAG, "Face detection FPS: %.1f", s_fps);
        }

        // Ceder CPU brevemente para otras tareas
        taskYIELD();
    }

    ESP_LOGI(TAG, "Tarea de detección terminada");
    vTaskDelete(NULL);
}

// =============================================================================
// API Pública
// =============================================================================

esp_err_t face_detect_init(face_detected_cb_t callback, void* user_data) {
    ESP_LOGI(TAG, "Inicializando detección de caras...");

#if HAVE_ESP_WHO
    ESP_LOGI(TAG, "Modelo MTMN disponible (esp-who)");
#else
    ESP_LOGW(TAG, "esp-who NO disponible — detección desactivada");
    ESP_LOGW(TAG, "Para activar: añade esp-who como componente IDF");
#endif

    s_result_mutex = xSemaphoreCreateMutex();
    if (!s_result_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_callback  = callback;
    s_user_data = user_data;
    memset(&s_last_result, 0, sizeof(s_last_result));

    ESP_LOGI(TAG, "Face detect inicializado");
    return ESP_OK;
}

esp_err_t face_detect_start(void) {
    if (s_task_handle != NULL) {
        ESP_LOGW(TAG, "Detección ya en marcha");
        return ESP_OK;
    }

    s_running = true;
    s_paused  = false;

    BaseType_t ret = xTaskCreatePinnedToCore(
        face_detect_task,
        "face_detect",
        TASK_FACE_DETECT_STACK,
        NULL,
        TASK_FACE_DETECT_PRIO,
        &s_task_handle,
        TASK_FACE_DETECT_CORE
    );

    if (ret != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Error creando tarea de detección");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Detección de caras iniciada (Core %d)", TASK_FACE_DETECT_CORE);
    return ESP_OK;
}

void face_detect_stop(void) {
    s_running = false;
    // La tarea se eliminará sola en su próxima iteración
    vTaskDelay(pdMS_TO_TICKS(200));
    s_task_handle = NULL;
}

void face_detect_pause(void) {
    s_paused = true;
    ESP_LOGD(TAG, "Detección pausada");
}

void face_detect_resume(void) {
    s_paused = false;
    ESP_LOGD(TAG, "Detección reanudada");
}

void face_detect_get_last_result(face_result_t* result) {
    if (!result) return;
    xSemaphoreTake(s_result_mutex, portMAX_DELAY);
    *result = s_last_result;
    xSemaphoreGive(s_result_mutex);
}

float face_detect_get_fps(void) {
    return s_fps;
}

void face_detect_deinit(void) {
    face_detect_stop();
    if (s_result_mutex) {
        vSemaphoreDelete(s_result_mutex);
        s_result_mutex = NULL;
    }
    s_callback  = NULL;
    s_user_data = NULL;
    ESP_LOGI(TAG, "Face detect deinicializado");
}
