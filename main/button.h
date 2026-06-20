/**
 * @file button.h
 * @brief Gestión del botón de disparo con debounce y detección de pulsación larga
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tipos de evento del botón
typedef enum {
    BTN_EVENT_SHORT_PRESS = 0,  // Pulsación corta (< BTN_LONG_PRESS_MS)
    BTN_EVENT_LONG_PRESS  = 1,  // Pulsación larga (>= BTN_LONG_PRESS_MS)
} button_event_t;

// Callback para eventos del botón
typedef void (*button_event_cb_t)(button_event_t event, void* user_data);

/**
 * @brief Inicializa el botón de captura con debounce por interrupción GPIO
 *
 * Configura el GPIO del botón como entrada con pull-up interno.
 * Instala el manejador de interrupción con debounce de software.
 *
 * @param callback   Función llamada en cada evento del botón
 * @param user_data  Dato de usuario para el callback
 * @return ESP_OK en éxito
 */
esp_err_t button_init(button_event_cb_t callback, void* user_data);

/**
 * @brief Verifica si el botón está actualmente presionado
 * @return true si el botón está pulsado en este momento
 */
bool button_is_pressed(void);

/**
 * @brief Deinicializa el botón y libera la interrupción GPIO
 */
void button_deinit(void);

#ifdef __cplusplus
}
#endif
