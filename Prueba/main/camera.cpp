/**
 * @file camera.cpp
 * @brief Implementación del driver OV5640 con modo dual y autoenfoque
 */
#include "camera.h"
#include "config.h"

#include "esp_log.h"
#include "esp_camera.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "CAMERA";

// Estado interno
static camera_mode_t s_current_mode = CAMERA_MODE_DETECT;
static bool          s_initialized  = false;

// =============================================================================
// Configuración base del sensor OV5640
// =============================================================================
static const camera_config_t s_cam_config = {
    .pin_pwdn       = CAM_PIN_PWDN,
    .pin_reset      = CAM_PIN_RESET,
    .pin_xclk       = CAM_PIN_XCLK,
    .pin_sccb_sda   = CAM_PIN_SIOD,
    .pin_sccb_scl   = CAM_PIN_SIOC,

    .pin_d7         = CAM_PIN_D7,
    .pin_d6         = CAM_PIN_D6,
    .pin_d5         = CAM_PIN_D5,
    .pin_d4         = CAM_PIN_D4,
    .pin_d3         = CAM_PIN_D3,
    .pin_d2         = CAM_PIN_D2,
    .pin_d1         = CAM_PIN_D1,
    .pin_d0         = CAM_PIN_D0,

    .pin_vsync      = CAM_PIN_VSYNC,
    .pin_href       = CAM_PIN_HREF,
    .pin_pclk       = CAM_PIN_PCLK,

    .xclk_freq_hz   = CAM_XCLK_FREQ_HZ,
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,

    // Modo inicial: Inicializamos con el tamaño MAXIMO para que reserve memoria suficiente en PSRAM
    .pixel_format   = PIXFORMAT_JPEG,
    .frame_size     = CAM_FRAMESIZE_CAPTURE, // Usar MAX resolución al inicializar
    .jpeg_quality   = CAM_JPEG_QUALITY_CAPTURE,
    .fb_count       = CAM_FB_COUNT_DETECT,
    .fb_location    = CAMERA_FB_IN_PSRAM,   // Frame buffers en PSRAM
    .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
};

// =============================================================================
// Autoenfoque OV5640 — Registros I2C internos del sensor
// =============================================================================
// El OV5640 tiene un procesador ARM Cortex-M0 interno (MCU) que gestiona el AF.
// Los comandos se envían escribiendo en registros específicos del sensor.

#define OV5640_REG_CMD_MAIN      0x3022  // Registro de comando principal AF
#define OV5640_REG_CMD_ACK       0x3023  // ACK del comando
#define OV5640_REG_CMD_PARA0     0x3024  // Parámetro 0
#define OV5640_REG_FW_STATUS     0x3029  // Estado del firmware AF

#define OV5640_CMD_TRIGGER_AF    0x03    // Lanzar AF
#define OV5640_CMD_PAUSE_AF      0x06    // Pausar AF
#define OV5640_CMD_RELEASE_AF    0x08    // Liberar AF

#define OV5640_AF_FOCUSED        0x10    // Estado: enfocado
#define OV5640_AF_FOCUSING       0x00    // Estado: enfocando
#define OV5640_AF_IDLE           0x20    // Estado: en reposo

// Función auxiliar para escribir registros del sensor via I2C SCCB
static esp_err_t ov5640_write_reg(uint16_t reg, uint8_t val) {
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) return ESP_FAIL;

    // Usar el bus SCCB del driver de cámara
    // El sensor OV5640 está en dirección 0x3C (por defecto)
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x3C << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, 3, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Función auxiliar para leer registros del sensor
static esp_err_t ov5640_read_reg(uint16_t reg, uint8_t* val) {
    uint8_t addr_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x3C << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, addr_buf, 2, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x3C << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// =============================================================================
// Implementación de la API pública
// =============================================================================

esp_err_t camera_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Cámara ya inicializada");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando OV5640...");

    esp_err_t err = esp_camera_init(&s_cam_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando cámara: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }

    // Configurar ajustes iniciales del sensor
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) {
        ESP_LOGE(TAG, "No se puede obtener el sensor");
        return ESP_FAIL;
    }

    // Verificar que es OV5640
    ESP_LOGI(TAG, "Sensor detectado: PID=0x%04X", sensor->id.PID);

    // Configuración óptima para OV5640
    sensor->set_brightness(sensor, 0);       // Brillo neutro
    sensor->set_contrast(sensor, 0);         // Contraste neutro
    sensor->set_saturation(sensor, 0);       // Saturación neutra
    sensor->set_sharpness(sensor, 0);        // Nitidez neutra
    sensor->set_whitebal(sensor, 1);         // Auto white balance ON
    sensor->set_awb_gain(sensor, 1);         // AWB gain ON
    sensor->set_exposure_ctrl(sensor, 1);    // AEC ON (auto exposición)
    sensor->set_aec2(sensor, 1);             // AEC DSP ON (procesamiento avanzado para mejor luz)
    sensor->set_gain_ctrl(sensor, 1);        // AGC ON (auto ganancia)
    sensor->set_bpc(sensor, 0);              // Black pixel correction OFF
    sensor->set_wpc(sensor, 1);              // White pixel correction ON
    sensor->set_raw_gma(sensor, 1);          // Gamma correction ON
    sensor->set_lenc(sensor, 1);             // Lens correction ON (uniformidad)
    sensor->set_hmirror(sensor, 0);          // Sin espejo horizontal
    sensor->set_vflip(sensor, 0);            // Sin volteo vertical
    sensor->set_dcw(sensor, 1);              // Downscale crop window ON
    sensor->set_colorbar(sensor, 0);         // Sin barra de color de prueba

    // Forzar el modo a DETECT (QVGA) ya que iniciamos en modo CAPTURE
    sensor->set_framesize(sensor, CAM_FRAMESIZE_DETECT);
    sensor->set_quality(sensor, CAM_JPEG_QUALITY_DETECT);

    s_current_mode = CAMERA_MODE_DETECT;
    s_initialized  = true;

    ESP_LOGI(TAG, "OV5640 inicializado correctamente en modo DETECT (QVGA)");
    return ESP_OK;
}

esp_err_t camera_set_mode(camera_mode_t mode) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_current_mode == mode) return ESP_OK;

    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) return ESP_FAIL;

    if (mode == CAMERA_MODE_DETECT) {
        ESP_LOGI(TAG, "Cambiando a modo DETECT (QVGA)");
        sensor->set_framesize(sensor, CAM_FRAMESIZE_DETECT);
        sensor->set_quality(sensor, CAM_JPEG_QUALITY_DETECT);
    } else {
        ESP_LOGI(TAG, "Cambiando a modo CAPTURE (%s)",
                 (CAM_FRAMESIZE_CAPTURE == FRAMESIZE_UXGA) ? "UXGA 1600x1200" : "5MP");
        sensor->set_framesize(sensor, CAM_FRAMESIZE_CAPTURE);
        sensor->set_quality(sensor, CAM_JPEG_QUALITY_CAPTURE);
    }

    // Pequeña pausa para que el sensor estabilice
    vTaskDelay(pdMS_TO_TICKS(150));

    // Descartar los primeros frames después del cambio (pueden ser corruptos)
    camera_fb_t* discard_fb = esp_camera_fb_get();
    if (discard_fb) esp_camera_fb_return(discard_fb);

    s_current_mode = mode;
    return ESP_OK;
}

camera_mode_t camera_get_mode(void) {
    return s_current_mode;
}

camera_fb_t* camera_capture_frame(void) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "Cámara no inicializada");
        return nullptr;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Error al capturar frame");
        return nullptr;
    }

    ESP_LOGD(TAG, "Frame capturado: %zu bytes (%dx%d)",
             fb->len, fb->width, fb->height);
    return fb;
}

void camera_free_frame(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

af_result_t camera_trigger_autofocus(void) {
    if (!s_initialized) return AF_RESULT_FAILED;

    ESP_LOGI(TAG, "Lanzando AF en OV5640...");
    
    // Limpiar ACK previo
    ov5640_write_reg(OV5640_REG_CMD_ACK, 0x00);
    // Enviar comando para disparar AF
    ov5640_write_reg(OV5640_REG_CMD_MAIN, OV5640_CMD_TRIGGER_AF);
    
    // Esperar confirmación (ACK) del comando
    uint8_t ack = 0;
    int wait_ms = 0;
    while (ack != OV5640_CMD_TRIGGER_AF && wait_ms < CAM_AF_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_ms += 10;
        ov5640_read_reg(OV5640_REG_CMD_ACK, &ack);
    }
    
    if (ack != OV5640_CMD_TRIGGER_AF) {
        ESP_LOGW(TAG, "AF Timeout esperando ACK");
        return AF_RESULT_TIMEOUT;
    }
    
    // Esperar a que termine de enfocar
    uint8_t status = OV5640_AF_FOCUSING;
    wait_ms = 0;
    while (status != OV5640_AF_FOCUSED && status != OV5640_AF_IDLE && wait_ms < CAM_AF_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_ms += 50;
        ov5640_read_reg(OV5640_REG_FW_STATUS, &status);
    }
    
    if (status == OV5640_AF_FOCUSED) {
        return AF_RESULT_SUCCESS;
    } else {
        return AF_RESULT_TIMEOUT;
    }
}

void camera_stop_autofocus(void) {
    if (!s_initialized) return;
    // Nada que hacer para OV2640
}

void camera_set_brightness(int level) {
    if (!s_initialized) return;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) sensor->set_brightness(sensor, level);
}

void camera_set_auto_exposure(bool enable) {
    if (!s_initialized) return;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) sensor->set_exposure_ctrl(sensor, enable ? 1 : 0);
}

void camera_set_auto_white_balance(bool enable) {
    if (!s_initialized) return;
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) sensor->set_whitebal(sensor, enable ? 1 : 0);
}

void camera_deinit(void) {
    if (!s_initialized) return;
    esp_camera_deinit();
    s_initialized = false;
    ESP_LOGI(TAG, "Cámara deinicializada");
}
