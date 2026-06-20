/**
 * @file sd_storage.h
 * @brief Gestión de almacenamiento en MicroSD
 *
 * Interfaz SDMMC 4-bit para máxima velocidad (~20 MB/s).
 * Sistema de archivos FAT32, directorio /DCIM/.
 * Nomenclatura: FOTO_YYYYMMDD_HHMMSS_NNN.jpg
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Información de un archivo de foto
typedef struct {
    char     filename[64];      // Nombre del archivo (sin ruta)
    char     full_path[128];    // Ruta completa incluyendo mount point
    uint32_t size_bytes;        // Tamaño del archivo en bytes
    char     timestamp[20];     // "YYYY-MM-DD HH:MM:SS"
} photo_info_t;

// Resultado de listado de fotos
typedef struct {
    photo_info_t* photos;       // Array de fotos (malloc'd, liberar con sd_free_list)
    uint32_t      count;        // Número de fotos encontradas
    uint64_t      total_bytes;  // Espacio total usado por fotos
} photo_list_t;

// =============================================================================
// API Pública
// =============================================================================

/**
 * @brief Inicializa la MicroSD en modo SDMMC 4-bit
 *
 * Monta el sistema de archivos FAT32.
 * Crea el directorio /DCIM/ si no existe.
 *
 * @return ESP_OK en éxito, ESP_FAIL si no hay tarjeta o error de montaje
 */
esp_err_t sd_storage_init(void);

/**
 * @brief Verifica si la MicroSD está disponible y montada
 * @return true si está lista para usar
 */
bool sd_storage_is_ready(void);

/**
 * @brief Guarda un JPEG en la MicroSD con timestamp
 *
 * Genera el nombre automáticamente con fecha/hora + contador.
 * Escribe el buffer JPEG directamente al archivo.
 *
 * @param jpeg_data  Puntero al buffer JPEG
 * @param jpeg_size  Tamaño del buffer en bytes
 * @param out_filename Buffer donde escribir el nombre generado (mín 64 bytes)
 * @return ESP_OK en éxito
 */
esp_err_t sd_save_photo(const uint8_t* jpeg_data, size_t jpeg_size, char* out_filename);

/**
 * @brief Lista todas las fotos en el directorio DCIM
 *
 * Devuelve un struct con array de fotos. DEBE liberarse con sd_free_photo_list().
 *
 * @param[out] list  Struct rellenado con la lista
 * @return ESP_OK en éxito
 */
esp_err_t sd_list_photos(photo_list_t* list);

/**
 * @brief Libera la memoria de una lista de fotos
 * @param list Lista a liberar (obtenida con sd_list_photos)
 */
void sd_free_photo_list(photo_list_t* list);

/**
 * @brief Lee un archivo de la MicroSD en memoria
 *
 * Asigna memoria dinámica para el contenido. DEBE liberarse con free().
 *
 * @param filename    Nombre del archivo (solo nombre, sin ruta completa)
 * @param[out] data   Puntero donde se guarda la dirección del buffer
 * @param[out] size   Tamaño del archivo leído en bytes
 * @return ESP_OK en éxito, ESP_ERR_NOT_FOUND si no existe
 */
esp_err_t sd_read_photo(const char* filename, uint8_t** data, size_t* size);

/**
 * @brief Borra una foto de la MicroSD
 * @param filename Nombre del archivo (solo nombre, sin ruta)
 * @return ESP_OK en éxito, ESP_ERR_NOT_FOUND si no existe
 */
esp_err_t sd_delete_photo(const char* filename);

/**
 * @brief Obtiene el espacio libre en la MicroSD
 * @param[out] free_bytes   Bytes libres disponibles
 * @param[out] total_bytes  Capacidad total de la tarjeta
 * @return ESP_OK en éxito
 */
esp_err_t sd_get_space_info(uint64_t* free_bytes, uint64_t* total_bytes);

/**
 * @brief Genera un JSON con la lista de fotos para enviar por BLE
 *
 * Formato: {"count":N,"photos":[{"name":"FOTO_xxx.jpg","size":12345,"time":"..."},...],"free_mb":NNN}
 *
 * @param[out] json_buf Buffer donde escribir el JSON
 * @param buf_size      Tamaño máximo del buffer
 * @return Número de bytes escritos, o -1 si error
 */
int sd_generate_file_list_json(char* json_buf, size_t buf_size);

/**
 * @brief Desmonta la MicroSD de forma segura
 */
void sd_storage_deinit(void);

#ifdef __cplusplus
}
#endif
