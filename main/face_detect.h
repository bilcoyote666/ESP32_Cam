/**
 * @file face_detect.h
 * @brief Detección de caras con esp-who (modelo MTMN)
 *
 * Corre en Core 0. Analiza frames QVGA de la cámara y emite
 * eventos cuando detecta caras con suficiente confianza.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Resultado de detección de cara
typedef struct {
    bool     detected;        // true si se detectó al menos una cara
    float    confidence;      // Confianza máxima (0.0 - 1.0)
    uint32_t face_count;      // Número de caras detectadas
    // Bounding box de la cara con mayor confianza (en píxeles del frame)
    int16_t  x;               // Esquina superior izquierda X
    int16_t  y;               // Esquina superior izquierda Y
    int16_t  width;           // Ancho del bounding box
    int16_t  height;          // Alto del bounding box
} face_result_t;

// Callback que se llama cuando se detecta una cara (desde ISR-safe context)
typedef void (*face_detected_cb_t)(const face_result_t* result, void* user_data);

// =============================================================================
// API Pública
// =============================================================================

/**
 * @brief Inicializa el sistema de detección de caras
 *
 * Carga el modelo MTMN en PSRAM. Requiere ~2MB de PSRAM libre.
 * Registra el callback para eventos de cara detectada.
 *
 * @param callback   Función a llamar cuando se detecta una cara
 * @param user_data  Dato de usuario pasado al callback
 * @return ESP_OK en éxito
 */
esp_err_t face_detect_init(face_detected_cb_t callback, void* user_data);

/**
 * @brief Activa el bucle de detección de caras
 *
 * Lanza la tarea FreeRTOS de detección en Core 0.
 * La tarea corre hasta que se llame face_detect_stop().
 *
 * @return ESP_OK en éxito
 */
esp_err_t face_detect_start(void);

/**
 * @brief Para el bucle de detección
 *
 * Detiene la tarea de detección de forma segura.
 */
void face_detect_stop(void);

/**
 * @brief Pausa la detección temporalmente (durante captura de foto)
 *
 * No destruye la tarea, solo suspende el análisis para que la cámara
 * pueda cambiar a modo CAPTURE sin conflictos de acceso.
 */
void face_detect_pause(void);

/**
 * @brief Reanuda la detección después de una pausa
 */
void face_detect_resume(void);

/**
 * @brief Obtiene el resultado de la última detección
 * @param[out] result Struct rellenado con el último resultado
 */
void face_detect_get_last_result(face_result_t* result);

/**
 * @brief Obtiene el FPS actual de la detección
 * @return FPS promedio de las últimas 10 iteraciones
 */
float face_detect_get_fps(void);

/**
 * @brief Deinicializa el sistema de detección y libera PSRAM
 */
void face_detect_deinit(void);

#ifdef __cplusplus
}
#endif
