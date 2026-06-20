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

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
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
// Inicialización SDMMC
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

    ESP_LOGI(TAG, "Montando MicroSD en modo SDMMC 4-bit...");

    // Configurar host SDMMC
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz — máxima velocidad

    // Configurar pines SDMMC
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // Modo 4-bit
    slot_config.clk   = (gpio_num_t)SD_MMC_PIN_CLK;
    slot_config.cmd   = (gpio_num_t)SD_MMC_PIN_CMD;
    slot_config.d0    = (gpio_num_t)SD_MMC_PIN_D0;
    slot_config.d1    = (gpio_num_t)SD_MMC_PIN_D1;
    slot_config.d2    = (gpio_num_t)SD_MMC_PIN_D2;
    slot_config.d3    = (gpio_num_t)SD_MMC_PIN_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // Pull-ups internos

    // Opciones de montaje FAT
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,       // NO formatear si falla
        .max_files              = SD_MAX_FILES,
        .allocation_unit_size   = SD_ALLOC_UNIT_SIZE,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card
    );

    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Fallo al montar FAT32. ¿Tarjeta formateada?");
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

    // Generar nombre con timestamp
    time_t now = 0;
    struct tm timeinfo = {};
    time(&now);
    localtime_r(&now, &timeinfo);

    char filename[SD_MAX_FILENAME_LEN];
    s_photo_counter++;
    snprintf(filename, sizeof(filename),
             "%s_%04d%02d%02d_%02d%02d%02d_%03lu.jpg",
             SD_FILENAME_PREFIX,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec,
             s_photo_counter);

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

    DIR* dir = opendir(SD_DCIM_DIR);
    if (!dir) {
        ESP_LOGE(TAG, "No se puede abrir directorio DCIM: %s", strerror(errno));
        xSemaphoreGive(s_sd_mutex);
        return ESP_FAIL;
    }

    // Primera pasada: contar archivos
    uint32_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            const char* name = entry->d_name;
            size_t len = strlen(name);
            // Solo archivos .jpg o .jpeg
            if (len > 4 &&
                (strcasecmp(name + len - 4, ".jpg")  == 0 ||
                 strcasecmp(name + len - 5, ".jpeg") == 0)) {
                count++;
            }
        }
    }

    if (count == 0) {
        closedir(dir);
        xSemaphoreGive(s_sd_mutex);
        return ESP_OK;
    }

    // Asignar array
    list->photos = (photo_info_t*)malloc(count * sizeof(photo_info_t));
    if (!list->photos) {
        closedir(dir);
        xSemaphoreGive(s_sd_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Segunda pasada: rellenar info
    rewinddir(dir);
    uint32_t idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < count) {
        if (entry->d_type != DT_REG) continue;
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (!(len > 4 && (strcasecmp(name + len - 4, ".jpg") == 0 ||
                          strcasecmp(name + len - 5, ".jpeg") == 0))) {
            continue;
        }

        photo_info_t* info = &list->photos[idx];
        strncpy(info->filename, name, sizeof(info->filename) - 1);
        info->filename[sizeof(info->filename) - 1] = '\0';

        snprintf(info->full_path, sizeof(info->full_path),
                 "%s/%s", SD_DCIM_DIR, name);

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
    xSemaphoreGive(s_sd_mutex);

    list->count = idx;
    ESP_LOGI(TAG, "Lista: %lu fotos, %.2f MB total",
             list->count, list->total_bytes / 1024.0f / 1024.0f);
    return ESP_OK;
}

void sd_free_photo_list(photo_list_t* list) {
    if (list && list->photos) {
        free(list->photos);
        list->photos = NULL;
        list->count  = 0;
    }
}

// =============================================================================
// Leer foto
// =============================================================================
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
