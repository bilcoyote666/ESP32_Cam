/**
 * @file config.h
 * @brief Configuración central — CámaraESP32 con OV5640
 *
 * Placa: Freenove ESP32-S3-WROOM CAM (N16R8)
 * Cámara: OV5640 5MP con autoenfoque
 * MicroSD: SDMMC 4-bit
 *
 * IMPORTANTE: Verifica los pines contra el esquemático de tu placa
 * específica antes de flashear. Los pines aquí son para la versión
 * Freenove ESP32-S3-WROOM documentada en su repositorio oficial.
 */
#pragma once

#include <stdint.h>

// =============================================================================
// VERSIÓN DEL FIRMWARE
// =============================================================================
#define FIRMWARE_VERSION     "1.0.0"
#define DEVICE_NAME          "CamaraESP32"

// =============================================================================
// PINES — CÁMARA OV5640 (interfaz DVP 24-pin)
// =============================================================================
// Fuente: Freenove ESP32-S3-WROOM CAM documentation
// https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board

#define CAM_PIN_PWDN         (-1)   // Power down — no conectado en esta placa
#define CAM_PIN_RESET        (-1)   // Reset — no conectado (reset interno)
#define CAM_PIN_XCLK         15     // Clock externo hacia el sensor
#define CAM_PIN_SIOD         4      // I2C SDA — control del sensor
#define CAM_PIN_SIOC         5      // I2C SCL — control del sensor

// Líneas de datos DVP (8 bits)
#define CAM_PIN_D7           16
#define CAM_PIN_D6           17
#define CAM_PIN_D5           18
#define CAM_PIN_D4           12
#define CAM_PIN_D3           10
#define CAM_PIN_D2           8
#define CAM_PIN_D1           9
#define CAM_PIN_D0           11

// Sincronización DVP
#define CAM_PIN_VSYNC        6      // Vertical sync
#define CAM_PIN_HREF         7      // Horizontal reference
#define CAM_PIN_PCLK         13     // Pixel clock

// Frecuencia clock del sensor (OV5640 acepta 6-27 MHz)
#define CAM_XCLK_FREQ_HZ     20000000  // 20 MHz — óptimo para OV5640

// =============================================================================
// PINES — MICROSD (SDMMC 4-bit, máxima velocidad)
// =============================================================================
// Modo SDMMC 4-bit: hasta ~20 MB/s de escritura
#define SD_MMC_PIN_CLK       39
#define SD_MMC_PIN_CMD       38
#define SD_MMC_PIN_D0        40     // Datos bit 0
#define SD_MMC_PIN_D1        41     // Datos bit 1 (solo 4-bit mode)
#define SD_MMC_PIN_D2        42     // Datos bit 2 (solo 4-bit mode)
#define SD_MMC_PIN_D3        2      // Datos bit 3 / CS en modo SPI

// =============================================================================
// PINES — BOTÓN Y LED
// =============================================================================
#define PIN_BTN_CAPTURE      0      // GPIO0 — botón BOOT disponible en placa
#define PIN_LED_STATUS       48     // LED RGB/WS2812 integrado en la placa

// Tiempos del botón (milisegundos)
#define BTN_DEBOUNCE_MS      20     // Tiempo de debounce
#define BTN_LONG_PRESS_MS    3000   // Pulsación larga → modo burst

// =============================================================================
// CONFIGURACIÓN DE CÁMARA
// =============================================================================

// Resoluciones disponibles (framesize_t de esp_camera)
// Para face detection — rápido, poca RAM
#define CAM_FRAMESIZE_DETECT  FRAMESIZE_QVGA   // 320x240

// Para captura final — máxima calidad
// FRAMESIZE_UXGA = 1600x1200 (2MP, ~170KB JPEG)
// FRAMESIZE_QSXGA = 2560x1920 (5MP, ~400KB JPEG) — requiere más PSRAM
#define CAM_FRAMESIZE_CAPTURE FRAMESIZE_UXGA   // 1600x1200 recomendado

// Calidad JPEG (10=mejor calidad, 63=peor calidad/más pequeño)
#define CAM_JPEG_QUALITY_DETECT   60   // Preview rápido
#define CAM_JPEG_QUALITY_CAPTURE  12   // Máxima calidad para guardar

// Número de frame buffers en PSRAM
#define CAM_FB_COUNT_DETECT       2    // Double-buffering para fluidez
#define CAM_FB_COUNT_CAPTURE      1    // 1 buffer para captura

// Tamaño máximo esperado de JPEG capturado (para validación)
#define CAM_MAX_JPEG_SIZE_BYTES   (600 * 1024)  // 600 KB máximo

// Timeout de autoenfoque OV5640 (milisegundos)
#define CAM_AF_TIMEOUT_MS         800   // Máximo tiempo esperando AF

// =============================================================================
// CONFIGURACIÓN DE DETECCIÓN DE CARAS
// =============================================================================
#define FACE_DETECT_THRESHOLD     0.65f  // Confianza mínima (0.0 - 1.0)
#define FACE_DETECT_NMS_THRESHOLD 0.3f   // Non-Maximum Suppression
#define FACE_DETECT_FPS_TARGET    8      // FPS objetivo en modo preview

// Tiempo mínimo entre disparos automáticos (evita ráfaga involuntaria)
#define FACE_DETECT_MIN_INTERVAL_MS  2000  // 2 segundos entre fotos auto

// =============================================================================
// CONFIGURACIÓN MICROSD
// =============================================================================
#define SD_MOUNT_POINT       "/sdcard"
#define SD_DCIM_DIR          "/sdcard/DCIM"
#define SD_MAX_FILES         20      // Máx archivos abiertos simultáneamente
#define SD_ALLOC_UNIT_SIZE   (16 * 1024)  // Tamaño de unidad de asignación

// Nombre de archivo: FOTO_YYYYMMDD_HHMMSS_NNN.jpg
#define SD_FILENAME_PREFIX   "FOTO"
#define SD_MAX_FILENAME_LEN  64

// =============================================================================
// CONFIGURACIÓN BLE (NimBLE)
// =============================================================================
#define BLE_DEVICE_NAME      DEVICE_NAME
#define BLE_MTU_SIZE         512    // MTU negociado (bytes por paquete)
#define BLE_CHUNK_SIZE       480    // Tamaño de chunk de datos (< MTU)
#define BLE_TX_TIMEOUT_MS    5000   // Timeout por chunk

// UUIDs del servicio GATT personalizado
// Servicio principal: Camera File Transfer Service
#define BLE_SERVICE_UUID         "12345678-1234-1234-1234-123456789ABC"
#define BLE_CHAR_FILE_LIST       "12345678-1234-1234-1234-123456789AB1"  // Read/Notify: JSON lista
#define BLE_CHAR_FILE_REQUEST    "12345678-1234-1234-1234-123456789AB2"  // Write: nombre archivo
#define BLE_CHAR_FILE_DATA       "12345678-1234-1234-1234-123456789AB3"  // Notify: chunks JPEG
#define BLE_CHAR_CONTROL         "12345678-1234-1234-1234-123456789AB4"  // Write: comandos
#define BLE_CHAR_STATUS          "12345678-1234-1234-1234-123456789AB5"  // Read/Notify: estado

// Comandos de control BLE (escritura en BLE_CHAR_CONTROL)
#define BLE_CMD_LIST_FILES       0x01  // Pedir lista de archivos
#define BLE_CMD_GET_FILE         0x02  // Pedir transferencia (usar FILE_REQUEST para nombre)
#define BLE_CMD_DELETE_FILE      0x03  // Borrar archivo (usar FILE_REQUEST para nombre)
#define BLE_CMD_CANCEL           0x04  // Cancelar transferencia en curso
#define BLE_CMD_CAPTURE_NOW      0x05  // Disparar foto desde BLE
#define BLE_CMD_GET_STATUS       0x06  // Pedir estado del dispositivo

// =============================================================================
// CONFIGURACIÓN BT CLASSIC (SPP)
// =============================================================================
#define BT_CLASSIC_DEVICE_NAME   DEVICE_NAME
#define BT_SPP_CMD_LIST          "LIST\r\n"
#define BT_SPP_CMD_GET           "GET:"    // Seguido de nombre de archivo
#define BT_SPP_CMD_DELETE        "DEL:"    // Seguido de nombre de archivo
#define BT_SPP_CMD_STATUS        "STATUS\r\n"
#define BT_SPP_CMD_CAPTURE       "CAPTURE\r\n"

// =============================================================================
// CONFIGURACIÓN DE TAREAS FREERTOS
// =============================================================================
// Core 0: Face detection (AI, intensivo en CPU)
// Core 1: Captura/SD/BLE (I/O bound)

#define TASK_FACE_DETECT_STACK   (16 * 1024)  // 16KB — modelo MTMN pesado
#define TASK_FACE_DETECT_PRIO    5
#define TASK_FACE_DETECT_CORE    0

#define TASK_CAPTURE_STACK       (8 * 1024)   // 8KB
#define TASK_CAPTURE_PRIO        6
#define TASK_CAPTURE_CORE        1

#define TASK_BLE_STACK           (8 * 1024)   // 8KB
#define TASK_BLE_PRIO            4
#define TASK_BLE_CORE            1

#define TASK_BT_CLASSIC_STACK    (6 * 1024)   // 6KB
#define TASK_BT_CLASSIC_PRIO     3
#define TASK_BT_CLASSIC_CORE     1

// =============================================================================
// ESTADOS GLOBALES DEL SISTEMA
// =============================================================================
typedef enum {
    SYS_STATE_INIT        = 0,   // Inicializando hardware
    SYS_STATE_READY       = 1,   // Listo, en modo preview
    SYS_STATE_DETECTING   = 2,   // Detectando caras (loop activo)
    SYS_STATE_FOCUSING    = 3,   // Esperando autoenfoque
    SYS_STATE_CAPTURING   = 4,   // Capturando foto
    SYS_STATE_SAVING      = 5,   // Guardando en MicroSD
    SYS_STATE_TRANSFERRING= 6,   // Transfiriendo vía BLE/SPP
    SYS_STATE_ERROR_CAM   = 7,   // Error de cámara
    SYS_STATE_ERROR_SD    = 8,   // Error de MicroSD
    SYS_STATE_ERROR_BLE   = 9,   // Error de Bluetooth
} system_state_t;

// =============================================================================
// MENSAJE ENTRE TAREAS (FreeRTOS Queue)
// =============================================================================
typedef enum {
    MSG_CAPTURE_BTN   = 0,   // Captura disparada por botón
    MSG_CAPTURE_FACE  = 1,   // Captura disparada por cara detectada
    MSG_CAPTURE_BLE   = 2,   // Captura disparada por comando BLE
    MSG_BURST_START   = 3,   // Inicio modo ráfaga (pulsación larga)
    MSG_BURST_STOP    = 4,   // Fin modo ráfaga
} capture_trigger_t;

typedef struct {
    capture_trigger_t trigger;
    uint8_t           burst_count;  // Número de fotos en ráfaga
} capture_msg_t;
