/**
 * @file ble_transfer.h
 * @brief Servidor BLE GATT para transferencia de fotos
 *
 * Usa NimBLE (más eficiente en memoria que Bluedroid).
 * Implementa un protocolo de transferencia de archivos por chunks.
 * Compatible con: iPhone, iPad, Mac, Android moderno.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Estado de la conexión BLE
typedef enum {
    BLE_STATE_STOPPED       = 0,
    BLE_STATE_ADVERTISING   = 1,
    BLE_STATE_CONNECTED     = 2,
    BLE_STATE_TRANSFERRING  = 3,
} ble_state_t;

// Callback cuando un cliente BLE solicita captura
typedef void (*ble_capture_request_cb_t)(void* user_data);

// =============================================================================
// API Pública
// =============================================================================

/**
 * @brief Inicializa el servidor BLE NimBLE
 *
 * Configura el stack NimBLE, define el servicio GATT personalizado
 * con todas las características para transferencia de archivos.
 *
 * @param capture_cb  Callback para cuando el cliente pide una foto
 * @param user_data   Dato de usuario para el callback
 * @return ESP_OK en éxito
 */
esp_err_t ble_transfer_init(ble_capture_request_cb_t capture_cb, void* user_data);

/**
 * @brief Inicia el advertising BLE para que los dispositivos puedan encontrar la cámara
 * @return ESP_OK en éxito
 */
esp_err_t ble_transfer_start_advertising(void);

/**
 * @brief Para el advertising BLE
 */
void ble_transfer_stop_advertising(void);

/**
 * @brief Notifica a todos los clientes conectados que hay una nueva foto disponible
 *
 * Envía una notificación en la característica de estado con el nombre del nuevo archivo.
 * Los clientes pueden entonces solicitar la lista actualizada o directamente el archivo.
 *
 * @param filename Nombre de la nueva foto (ej: "FOTO_20240101_120000_001.jpg")
 */
void ble_transfer_notify_new_photo(const char* filename);

/**
 * @brief Obtiene el estado actual de la conexión BLE
 * @return Estado actual (BLE_STATE_*)
 */
ble_state_t ble_transfer_get_state(void);

/**
 * @brief Verifica si hay algún cliente BLE conectado
 * @return true si hay al menos un cliente conectado
 */
bool ble_transfer_is_connected(void);

/**
 * @brief Obtiene el número de clientes BLE conectados actualmente
 * @return Número de conexiones activas
 */
uint8_t ble_transfer_get_connection_count(void);

/**
 * @brief Obtiene el progreso de la transferencia en curso (0-100)
 * @return Porcentaje de progreso, o -1 si no hay transferencia activa
 */
int ble_transfer_get_progress(void);

/**
 * @brief Deinicializa el servidor BLE y libera recursos
 */
void ble_transfer_deinit(void);

#ifdef __cplusplus
}
#endif
