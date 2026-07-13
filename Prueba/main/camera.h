/**
 * @file camera.h
 * @brief Driver de cámara OV5640 — Modo dual resolución
 *
 * Gestiona la cámara OV5640 en dos modos:
 *  - DETECT: QVGA 320x240 a ~15fps para face detection
 *  - CAPTURE: UXGA 1600x1200 JPEG de alta calidad
 *
 * El OV5640 tiene autoenfoque (AF) por hardware controlado via I2C.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Modos de operación de la cámara
// =============================================================================
typedef enum {
    CAMERA_MODE_DETECT  = 0,  // QVGA, baja calidad JPEG, alta velocidad
    CAMERA_MODE_CAPTURE = 1,  // UXGA, máxima calidad, baja velocidad
} camera_mode_t;

// Resultado del autoenfoque
typedef enum {
    AF_RESULT_SUCCESS  = 0,  // Enfoque conseguido
    AF_RESULT_TIMEOUT  = 1,  // Timeout sin conseguir enfoque
    AF_RESULT_FAILED   = 2,  // Error de comunicación I2C
} af_result_t;

// =============================================================================
// API Pública
// =============================================================================

/**
 * @brief Inicializa la cámara OV5640
 *
 * Configura el driver esp_camera con los pines de config.h.
 * Arranca en modo DETECT (QVGA).
 *
 * @return ESP_OK en éxito, ESP_FAIL o error específico si falla.
 */
esp_err_t camera_init(void);

/**
 * @brief Cambia el modo de la cámara (resolución y calidad)
 *
 * Cambiar de modo requiere reconfigurar el sensor vía I2C,
 * tarda ~100-300ms. No llamar durante captura activa.
 *
 * @param mode CAMERA_MODE_DETECT o CAMERA_MODE_CAPTURE
 * @return ESP_OK en éxito
 */
esp_err_t camera_set_mode(camera_mode_t mode);

/**
 * @brief Obtiene el modo actual de la cámara
 * @return Modo actual (DETECT o CAPTURE)
 */
camera_mode_t camera_get_mode(void);

/**
 * @brief Captura un frame y lo devuelve
 *
 * El caller es RESPONSABLE de liberar el frame con camera_free_frame().
 * En modo DETECT devuelve QVGA RGB565.
 * En modo CAPTURE devuelve UXGA JPEG.
 *
 * @return Puntero a frame buffer, o NULL si error
 */
camera_fb_t* camera_capture_frame(void);

/**
 * @brief Libera un frame capturado previamente
 * @param fb Frame buffer a liberar (retornado por camera_capture_frame)
 */
void camera_free_frame(camera_fb_t* fb);

/**
 * @brief Activa el autoenfoque del OV5640 y espera resultado
 *
 * Envía comando AF por I2C al OV5640 y espera hasta que confirme
 * enfoque conseguido o timeout. Bloquea hasta CAM_AF_TIMEOUT_MS.
 *
 * @return AF_RESULT_SUCCESS, AF_RESULT_TIMEOUT, o AF_RESULT_FAILED
 */
af_result_t camera_trigger_autofocus(void);

/**
 * @brief Para el autoenfoque continuo (ahorra CPU del sensor)
 */
void camera_stop_autofocus(void);

/**
 * @brief Ajusta el brillo de la imagen (-2 a +2)
 * @param level Nivel de brillo
 */
void camera_set_brightness(int level);

/**
 * @brief Activa/desactiva corrección de iluminación automática (AEC)
 * @param enable true para activar
 */
void camera_set_auto_exposure(bool enable);

/**
 * @brief Activa/desactiva balance de blancos automático (AWB)
 * @param enable true para activar
 */
void camera_set_auto_white_balance(bool enable);

/**
 * @brief Deinicializa la cámara y libera recursos
 */
void camera_deinit(void);

#ifdef __cplusplus
}
#endif
