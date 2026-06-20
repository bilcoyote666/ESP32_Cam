/**
 * @file bt_classic.h
 * @brief BT Classic SPP para transferencia de fotos a PC/Android legacy
 *
 * Serial Port Profile (SPP) sobre Bluetooth Classic.
 * Compatible con: Windows, Linux, Android (sin BLE).
 * Protocolo de texto ASCII con datos binarios para el JPEG.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Callback para solicitud de captura desde BT Classic
typedef void (*bt_capture_request_cb_t)(void* user_data);

/**
 * @brief Inicializa el servidor BT Classic SPP
 *
 * Registra el servicio SPP con el nombre del dispositivo.
 * Compatible con emparejamiento estándar de BT Classic.
 *
 * @param capture_cb  Callback para cuando el cliente pide una foto
 * @param user_data   Dato de usuario para el callback
 * @return ESP_OK en éxito
 */
esp_err_t bt_classic_init(bt_capture_request_cb_t capture_cb, void* user_data);

/**
 * @brief Verifica si hay un cliente BT Classic conectado
 * @return true si hay conexión activa
 */
bool bt_classic_is_connected(void);

/**
 * @brief Notifica al cliente que hay una nueva foto
 * @param filename Nombre de la nueva foto
 */
void bt_classic_notify_new_photo(const char* filename);

/**
 * @brief Deinicializa BT Classic SPP
 */
void bt_classic_deinit(void);

#ifdef __cplusplus
}
#endif
