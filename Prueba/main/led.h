/**
 * @file led.h
 * @brief Control del LED de estado con patrones de parpadeo no-bloqueantes
 *
 * El LED (GPIO48, WS2812 o LED normal) indica el estado del sistema.
 * Usa timer de hardware para los patrones, no bloquea ninguna tarea.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Patrones de LED (mapean a los estados del sistema)
typedef enum {
    LED_PATTERN_OFF           = 0,  // Apagado
    LED_PATTERN_READY         = 1,  // Encendido fijo (tenue)
    LED_PATTERN_DETECTING     = 2,  // Parpadeo lento 1Hz
    LED_PATTERN_CAPTURING     = 3,  // Flash rápido x3
    LED_PATTERN_SAVING        = 4,  // Parpadeo rápido 5Hz
    LED_PATTERN_TRANSFERRING  = 5,  // Doble parpadeo cada 2s
    LED_PATTERN_ERROR_SD      = 6,  // 3 parpadeos rápidos + pausa larga
    LED_PATTERN_ERROR_CAM     = 7,  // Apagado (no LED = error cámara)
    LED_PATTERN_BLE_CONNECTED = 8,  // Parpadeo muy lento 0.5Hz
} led_pattern_t;

/**
 * @brief Inicializa el LED de estado
 *
 * Configura el GPIO del LED y arranca el timer de parpadeo.
 * El LED arranca en patrón OFF.
 *
 * @return ESP_OK en éxito
 */
esp_err_t led_init(void);

/**
 * @brief Establece el patrón de parpadeo del LED
 *
 * El cambio de patrón es inmediato y no bloqueante.
 *
 * @param pattern Nuevo patrón LED
 */
void led_set_pattern(led_pattern_t pattern);

/**
 * @brief Actualiza el LED en función del estado global del sistema
 *
 * Mapea automáticamente el system_state_t a un led_pattern_t.
 *
 * @param state Estado del sistema
 */
void led_update_from_state(system_state_t state);

/**
 * @brief Dispara un flash de confirmación (independiente del patrón actual)
 *
 * Emite N destellos rápidos y vuelve al patrón anterior.
 *
 * @param flashes Número de destellos (1-5)
 */
void led_flash_confirm(uint8_t flashes);

/**
 * @brief Deinicializa el LED y para el timer
 */
void led_deinit(void);

#ifdef __cplusplus
}
#endif
