/**
 * @file sd_storage.cpp
 * @brief Implementación de almacenamiento en MicroSD via SDMMC 4-bit
 */
#include "sd_storage.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "SD_STORAGE";

// Estado interno
static bool              s_mounted    = false;
static sdmmc_card_t*     s_card       = NULL;
static SemaphoreHandle_t s_sd_mutex   = NULL;  // Mutex para acceso thread-safe
static uint32_t          s_photo_counter = 0;   // Contador para evitar colisiones de nombre

// =============================================================================
// Inicialización SD (SPI)
// =============================================================================
esp_err_t sd_storage_init(void) {
    if (s_mounted) {
        ESP_LOGW(TAG, "SD ya montada");
        return ESP_OK;
    }

    // Crear mutex de protección
    s_sd_mutex = xSemaphoreCreateMutex();
    if (!s_sd_mutex) {
        ESP_LOGE(TAG, "No se pudo crear mutex SD");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Montando MicroSD en modo SPI (XIAO Sense)...");

    // Configurar el bus SPI
    esp_err_t err;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_SPI_PIN_MOSI,
        .miso_io_num = SD_SPI_PIN_MISO,
        .sclk_io_num = SD_SPI_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    err = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando bus SPI");
        return err;
    }

    // Configurar los pines de la SD
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)SD_SPI_PIN_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    // Opciones de montaje FAT
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files              = SD_MAX_FILES;
    mount_config.allocation_unit_size   = SD_ALLOC_UNIT_SIZE;

    err = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card
    );

    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Fallo al montar FAT32. ¿Tarjeta formateada o conectada?");
        } else {
            ESP_LOGE(TAG, "Error al inicializar SD: %s", esp_err_to_name(err));
        }
        return err;
    }

    // Mostrar info de la tarjeta
    sdmmc_card_print_info(stdout, s_card);

    // Crear directorio DCIM si no existe
    struct stat st;
    if (stat(SD_DCIM_DIR, &st) != 0) {
        ESP_LOGI(TAG, "Creando directorio %s", SD_DCIM_DIR);
        if (mkdir(SD_DCIM_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Error creando DCIM: %s", strerror(errno));
        }
    }
    // Sincronizar contador de fotos para no sobreescribir al reiniciar
    DIR* dir = opendir(SD_DCIM_DIR);
    if (dir) {
        struct dirent* entry;
        uint32_t max_num = 0;
        size_t prefix_len = strlen(SD_FILENAME_PREFIX);
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_DIR && strncmp(entry->d_name, SD_FILENAME_PREFIX, prefix_len) == 0) {
                if (entry->d_name[prefix_len] == '_') {
                    uint32_t num = atoi(entry->d_name + prefix_len + 1);
                    if (num > max_num) max_num = num;
                }
            }
        }
        closedir(dir);
        s_photo_counter = max_num;
        ESP_LOGI(TAG, "Contador de fotos sincronizado en %lu", s_photo_counter);
    }

    s_mounted = true;
    ESP_LOGI(TAG, "MicroSD montada correctamente en %s", SD_MOUNT_POINT);
    return ESP_OK;
}

bool sd_storage_is_ready(void) {
    return s_mounted && (s_card != NULL);
}

// =============================================================================
// Guardar foto
// =============================================================================
esp_err_t sd_save_photo(const uint8_t* jpeg_data, size_t jpeg_size, char* out_filename) {
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    if (!jpeg_data || jpeg_size == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

    // Generar nombre secuencial simple
    char filename[SD_MAX_FILENAME_LEN];
    s_photo_counter++;
    snprintf(filename, sizeof(filename), "%s_%02lu.jpg", SD_FILENAME_PREFIX, s_photo_counter);

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_DCIM_DIR, filename);

    ESP_LOGI(TAG, "Guardando foto: %s (%zu bytes)", full_path, jpeg_size);

    // Abrir archivo y escribir
    FILE* f = fopen(full_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Error abriendo archivo para escribir: %s", strerror(errno));
        xSemaphoreGive(s_sd_mutex);
        return ESP_FAIL;
    }

    size_t written = fwrite(jpeg_data, 1, jpeg_size, f);
    fclose(f);

    xSemaphoreGive(s_sd_mutex);

    if (written != jpeg_size) {
        ESP_LOGE(TAG, "Error de escritura: %zu/%zu bytes escritos", written, jpeg_size);
        return ESP_FAIL;
    }

    if (out_filename) {
        strncpy(out_filename, filename, SD_MAX_FILENAME_LEN - 1);
        out_filename[SD_MAX_FILENAME_LEN - 1] = '\0';
    }

    ESP_LOGI(TAG, "Foto guardada: %s", filename);
    return ESP_OK;
}

// =============================================================================
// Listar fotos
// =============================================================================
esp_err_t sd_list_photos(photo_list_t* list) {
    if (!s_mounted || !list) return ESP_ERR_INVALID_STATE;

    list->photos      = NULL;
    list->count       = 0;
    list->total_bytes = 0;

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

    const char* dirs_to_check[] = { SD_DCIM_DIR, SD_MOUNT_POINT };
    uint32_t total_count = 0;

    // Primera pasada: contar archivos en ambos directorios
    for (int d = 0; d < 2; d++) {
        DIR* dir = opendir(dirs_to_check[d]);
        if (!dir) continue;
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) continue;
            
            const char* name = entry->d_name;
            if (name[0] == '.') continue;
            
            size_t len = strlen(name);
            if (len > 4 && (strcasecmp(name + len - 4, ".jpg") == 0 || strcasecmp(name + len - 5, ".jpeg") == 0)) {
                total_count++;
            }
        }
        closedir(dir);
    }

    if (total_count == 0) {
        xSemaphoreGive(s_sd_mutex);
        return ESP_OK;
    }

    // Asignar array
    list->photos = (photo_info_t*)malloc(total_count * sizeof(photo_info_t));
    if (!list->photos) {
        xSemaphoreGive(s_sd_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Segunda pasada: rellenar info
    uint32_t idx = 0;
    for (int d = 0; d < 2; d++) {
        DIR* dir = opendir(dirs_to_check[d]);
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL && idx < total_count) {
            if (entry->d_type == DT_DIR) continue;
            
            const char* name = entry->d_name;
            if (name[0] == '.') continue;
            
            size_t len = strlen(name);
            if (!(len > 4 && (strcasecmp(name + len - 4, ".jpg") == 0 || strcasecmp(name + len - 5, ".jpeg") == 0))) {
                continue;
            }

            photo_info_t* info = &list->photos[idx];
            strncpy(info->filename, name, sizeof(info->filename) - 1);
            info->filename[sizeof(info->filename) - 1] = '\0';

            snprintf(info->full_path, sizeof(info->full_path), "%s/%s", dirs_to_check[d], name);

            struct stat st;
            if (stat(info->full_path, &st) == 0) {
                info->size_bytes = (uint32_t)st.st_size;
                list->total_bytes += st.st_size;

                struct tm t;
                localtime_r(&st.st_mtime, &t);
                snprintf(info->timestamp, sizeof(info->timestamp),
                         "%04d-%02d-%02d %02d:%02d:%02d",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec);
            } else {
                info->size_bytes = 0;
                strncpy(info->timestamp, "0000-00-00 00:00:00", sizeof(info->timestamp));
            }

            idx++;
        }
        closedir(dir);
    }

    xSemaphoreGive(s_sd_mutex);

    list->count = idx;
    ESP_LOGI(TAG, "Lista: %lu fotos, %.2f MB total", list->count, list->total_bytes / 1024.0f / 1024.0f);
    return ESP_OK;
}

void sd_free_photo_list(photo_list_t* list) {
    if (!list) return;
    if (list->photos) {
        free(list->photos);
        list->photos = NULL;
    }
    list->count = 0;
    list->total_bytes = 0;
}

char* sd_list_files_json(void) {
    DIR* dir = opendir(SD_DCIM_DIR);

    ESP_LOGI(TAG, "Abriendo: %s", SD_DCIM_DIR);

    if (!dir) {
        ESP_LOGE(TAG, "ERROR: no se puede abrir el directorio");
        return strdup("[]");
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "Archivo: '%s'  tipo=%d", entry->d_name, entry->d_type);
    }

    closedir(dir); // He cambiado rewinddir por closedir para evitar una fuga de memoria

    photo_list_t list = {0};
    if (sd_list_photos(&list) != ESP_OK) {
        return strdup("[]");
    }

    // Calcular tamaño aproximado del JSON
    // Formato: [{"name":"FOTO...","size":12345,"date":"2023-..."}]
    size_t json_size = 10; // "[]" + null terminator + padding
    for (uint32_t i = 0; i < list.count; i++) {
        json_size += 100 + strlen(list.photos[i].filename) + strlen(list.photos[i].timestamp);
    }

    char* json = (char*)malloc(json_size);
    if (!json) {
        sd_free_photo_list(&list);
        return NULL;
    }

    strcpy(json, "[");
    for (uint32_t i = 0; i < list.count; i++) {
        char item[128];
        snprintf(item, sizeof(item), "{\"name\":\"%s\",\"size\":%lu,\"date\":\"%s\"}%s", 
                 list.photos[i].filename, 
                 (unsigned long)list.photos[i].size_bytes, 
                 list.photos[i].timestamp,
                 (i < list.count - 1) ? "," : "");
        strcat(json, item);
    }
    strcat(json, "]");

    sd_free_photo_list(&list);
    return json;
}


// =============================================================================
// Leer foto
// =============================================================================
FILE* sd_open_file(const char* filename, const char* mode) {
    if (!s_mounted) return NULL;
    char path[SD_MAX_FILENAME_LEN + 32];
    snprintf(path, sizeof(path), "%s/%s", SD_DCIM_DIR, filename);
    FILE* f = fopen(path, mode);
    if (!f && (mode[0] == 'r')) {
        snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, filename);
        f = fopen(path, mode);
    }
    return f;
}

esp_err_t sd_read_photo(const char* filename, uint8_t** data, size_t* size) {
    if (!s_mounted || !filename || !data || !size) return ESP_ERR_INVALID_ARG;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_DCIM_DIR, filename);

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

    struct stat st;
    if (stat(full_path, &st) != 0) {
        xSemaphoreGive(s_sd_mutex);
        ESP_LOGW(TAG, "Archivo no encontrado: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }

    *size = (size_t)st.st_size;
    *data = (uint8_t*)malloc(*size);
    if (!*data) {
        xSemaphoreGive(s_sd_mutex);
        ESP_LOGE(TAG, "Sin memoria para leer %zu bytes", *size);
        return ESP_ERR_NO_MEM;
    }

    FILE* f = fopen(full_path, "rb");
    if (!f) {
        free(*data);
        *data = NULL;
        xSemaphoreGive(s_sd_mutex);
        ESP_LOGE(TAG, "Error abriendo archivo: %s", strerror(errno));
        return ESP_FAIL;
    }

    size_t read = fread(*data, 1, *size, f);
    fclose(f);
    xSemaphoreGive(s_sd_mutex);

    if (read != *size) {
        free(*data);
        *data = NULL;
        ESP_LOGE(TAG, "Error de lectura: %zu/%zu bytes leídos", read, *size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Foto leída: %s (%zu bytes)", filename, *size);
    return ESP_OK;
}

// =============================================================================
// Borrar foto
// =============================================================================
esp_err_t sd_delete_photo(const char* filename) {
    if (!s_mounted || !filename) return ESP_ERR_INVALID_ARG;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_DCIM_DIR, filename);

    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    int ret = remove(full_path);
    if (ret != 0 && errno == ENOENT) {
        // Fallback: intentar en la raíz de la tarjeta SD
        snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, filename);
        ret = remove(full_path);
    }
    xSemaphoreGive(s_sd_mutex);

    if (ret != 0) {
        if (errno == ENOENT) {
            ESP_LOGW(TAG, "Archivo no encontrado para borrar: %s", filename);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Error borrando archivo: %s", strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Foto borrada: %s", filename);
    return ESP_OK;
}

// =============================================================================
// Información de espacio
// =============================================================================
esp_err_t sd_get_space_info(uint64_t* free_bytes, uint64_t* total_bytes) {
    if (!s_mounted || !free_bytes || !total_bytes) return ESP_ERR_INVALID_STATE;

    FATFS* fs;
    DWORD  fre_clust;
    FRESULT res = f_getfree("0:", &fre_clust, &fs);

    if (res != FR_OK) {
        ESP_LOGE(TAG, "Error obteniendo espacio libre: %d", res);
        return ESP_FAIL;
    }

    uint64_t total_sectors = (fs->n_fatent - 2ULL) * fs->csize;
    uint64_t free_sectors  = (uint64_t)fre_clust * fs->csize;
    uint32_t sector_size   = 512;  // Bytes por sector estándar FAT

    *total_bytes = total_sectors * sector_size;
    *free_bytes  = free_sectors  * sector_size;

    ESP_LOGI(TAG, "SD: %.2f GB libre de %.2f GB total",
             *free_bytes  / 1024.0 / 1024.0 / 1024.0,
             *total_bytes / 1024.0 / 1024.0 / 1024.0);

    return ESP_OK;
}

// =============================================================================
// Generar JSON para BLE
// =============================================================================
int sd_generate_file_list_json(char* json_buf, size_t buf_size) {
    if (!json_buf || buf_size == 0) return -1;

    photo_list_t list = {};
    if (sd_list_photos(&list) != ESP_OK) {
        snprintf(json_buf, buf_size, "{\"error\":\"sd_error\",\"count\":0}");
        return strlen(json_buf);
    }

    uint64_t free_bytes  = 0;
    uint64_t total_bytes = 0;
    sd_get_space_info(&free_bytes, &total_bytes);

    // Construir JSON
    int written = snprintf(json_buf, buf_size,
        "{\"count\":%lu,\"free_mb\":%llu,\"photos\":[",
        list.count,
        free_bytes / 1024 / 1024);

    for (uint32_t i = 0; i < list.count && written < (int)buf_size - 10; i++) {
        written += snprintf(json_buf + written, buf_size - written,
            "%s{\"name\":\"%s\",\"size\":%lu,\"time\":\"%s\"}",
            (i > 0) ? "," : "",
            list.photos[i].filename,
            list.photos[i].size_bytes,
            list.photos[i].timestamp);
    }

    written += snprintf(json_buf + written, buf_size - written, "]}");

    sd_free_photo_list(&list);
    return written;
}

// =============================================================================
// Deinicialización
// =============================================================================
void sd_storage_deinit(void) {
    if (!s_mounted) return;

    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card    = NULL;
    s_mounted = false;

    if (s_sd_mutex) {
        vSemaphoreDelete(s_sd_mutex);
        s_sd_mutex = NULL;
    }

    ESP_LOGI(TAG, "MicroSD desmontada");
}
