/**
 * @file config.h
 * @brief Configuración central — CámaraESP32 con OV5640
 *
 * Placa: Seeed Studio XIAO ESP32S3 Sense
 * Cámara: OV2640
 * MicroSD: SPI
 */
#pragma once

#include <stdint.h>

// =============================================================================
// VERSIÓN DEL FIRMWARE
// =============================================================================
#define FIRMWARE_VERSION     "1.0.0"
#define DEVICE_NAME          "Raboseta Cam"

// =============================================================================
// PINES — CÁMARA OV5640 (interfaz DVP 24-pin)
// =============================================================================
// XIAO ESP32S3 Sense camera pinout
#define CAM_PIN_PWDN         (-1)
#define CAM_PIN_RESET        (-1)
#define CAM_PIN_XCLK         10
#define CAM_PIN_SIOD         40
#define CAM_PIN_SIOC         39

// Líneas de datos DVP (8 bits)
#define CAM_PIN_D7           48
#define CAM_PIN_D6           11
#define CAM_PIN_D5           12
#define CAM_PIN_D4           14
#define CAM_PIN_D3           16
#define CAM_PIN_D2           18
#define CAM_PIN_D1           17
#define CAM_PIN_D0           15

// Sincronización DVP
#define CAM_PIN_VSYNC        38
#define CAM_PIN_HREF         47
#define CAM_PIN_PCLK         13

// Frecuencia clock del sensor (OV5640 acepta 6-27 MHz)
#define CAM_XCLK_FREQ_HZ     20000000  // 20 MHz — óptimo para OV5640

// Modo SPI para XIAO ESP32S3 Sense
#define SD_SPI_PIN_CS        21
#define SD_SPI_PIN_MOSI      9
#define SD_SPI_PIN_MISO      8
#define SD_SPI_PIN_SCK       7

// =============================================================================
// PINES — BOTÓN Y LED
// =============================================================================
#define PIN_BTN_CAPTURE      1      // GPIO1 — Pin externo D0
#define PIN_LED_STATUS       2      // GPIO2 — Pin externo D1

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
#define CAM_FRAMESIZE_CAPTURE FRAMESIZE_UXGA  // 1600x1200 (OV2640 máxima resolución)

// Calidad JPEG (10=mejor calidad, 63=peor calidad/más pequeño)
#define CAM_JPEG_QUALITY_DETECT   60   // Preview rápido
#define CAM_JPEG_QUALITY_CAPTURE  10   // Máxima calidad posible para guardar

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
#define SD_FILENAME_PREFIX   "Foto"
#define SD_MAX_FILENAME_LEN  64

// =============================================================================
// CONFIGURACIÓN WIFI (Captive Portal)
// =============================================================================
#define WIFI_AP_SSID         DEVICE_NAME

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
