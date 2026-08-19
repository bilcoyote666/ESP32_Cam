/**
 * @file led.cpp
 * @brief Implementación del LED de estado con patrones no-bloqueantes
 *
 * Usa esp_timer de alta resolución para los patrones de parpadeo.
 * El LED del Freenove ESP32-S3-WROOM es un WS2812 en GPIO48.
 * Para simplicidad, aquí usamos GPIO básico (HIGH/LOW).
 * Si tu placa usa WS2812 (LED RGB), sustituir por driver RMT.
 */
#include "led.h"
#include "config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "LED";

// Estado interno
static led_pattern_t      s_current_pattern = LED_PATTERN_OFF;
static esp_timer_handle_t s_blink_timer     = NULL;
static int                s_blink_count     = 0;  // Para patrones con N flashes
static bool               s_led_state       = false;

// =============================================================================
// Control físico del LED
// =============================================================================
static inline void led_on(void)  { if(PIN_LED_STATUS==-1) return; gpio_set_level((gpio_num_t)PIN_LED_STATUS, 1); s_led_state = true;  }
static inline void led_off(void) { if(PIN_LED_STATUS==-1) return; gpio_set_level((gpio_num_t)PIN_LED_STATUS, 0); s_led_state = false; }
static inline void led_toggle(void) {
    if(PIN_LED_STATUS==-1) return;
    s_led_state = !s_led_state;
    gpio_set_level((gpio_num_t)PIN_LED_STATUS, s_led_state ? 1 : 0);
}

// =============================================================================
// Callback del timer de parpadeo
// Gestiona cada patrón como una máquina de estados
// =============================================================================
static void blink_timer_cb(void* arg) {
    switch (s_current_pattern) {
        case LED_PATTERN_OFF:
            led_off();
            esp_timer_stop(s_blink_timer);
            break;

        case LED_PATTERN_READY:
        case LED_PATTERN_DETECTING:
            // Encendido fijo constante para indicar que la cámara está conectada y lista
            led_on();
            esp_timer_stop(s_blink_timer);
            break;

        case LED_PATTERN_CAPTURING:
            // Destello rápido para la captura
            if (s_blink_count < 4) {
                led_toggle();
                s_blink_count++;
                esp_timer_start_once(s_blink_timer, 60 * 1000ULL);  // 60ms
            } else {
                led_on();
                s_blink_count = 0;
                s_current_pattern = LED_PATTERN_READY;
                esp_timer_stop(s_blink_timer);
            }
            break;

        case LED_PATTERN_SAVING:
            // Parpadeo rápido durante guardado / arranque
            led_toggle();
            esp_timer_start_once(s_blink_timer, 100 * 1000ULL);
            break;

        case LED_PATTERN_TRANSFERRING:
            // Doble parpadeo cada 2s
            if (s_blink_count < 4) {
                led_toggle();
                s_blink_count++;
                esp_timer_start_once(s_blink_timer, 150 * 1000ULL);
            } else {
                led_on();
                s_blink_count = 0;
                esp_timer_start_once(s_blink_timer, 1700 * 1000ULL);
            }
            break;

        case LED_PATTERN_BLE_CONNECTED:
            // Encendido continuo
            led_on();
            esp_timer_stop(s_blink_timer);
            break;

        case LED_PATTERN_ERROR_SD:
            // 3 flashes rápidos + pausa 2s
            if (s_blink_count < 6) {
                led_toggle();
                s_blink_count++;
                esp_timer_start_once(s_blink_timer, 120 * 1000ULL);
            } else {
                led_on();
                s_blink_count = 0;
                esp_timer_start_once(s_blink_timer, 2000 * 1000ULL);
            }
            break;

        case LED_PATTERN_ERROR_CAM:
            // Parpadeo lento de aviso (NO apagar totalmente para que se vea que tiene energía)
            led_toggle();
            esp_timer_start_once(s_blink_timer, 300 * 1000ULL);
            break;

        default:
            led_on();
            break;
    }
}

// =============================================================================
// API Pública
// =============================================================================
esp_err_t led_init(void) {
    // 1. Configurar LED de estado si está definido
    if (PIN_LED_STATUS != -1) {
        ESP_LOGI(TAG, "Inicializando LED de estado en GPIO%d", PIN_LED_STATUS);
        gpio_config_t io_conf = {};
        io_conf.intr_type    = GPIO_INTR_DISABLE;
        io_conf.mode         = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << PIN_LED_STATUS);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;

        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error configurando GPIO LED estado");
            return ret;
        }
        gpio_set_drive_capability((gpio_num_t)PIN_LED_STATUS, GPIO_DRIVE_CAP_3);
        led_on(); // Encender inmediatamente al iniciar

        // Crear timer para parpadeo de estado
        esp_timer_create_args_t timer_args = {
            .callback        = blink_timer_cb,
            .arg             = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name            = "led_blink",
            .skip_unhandled_events = true,
        };
        ret = esp_timer_create(&timer_args, &s_blink_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error creando timer LED");
            return ret;
        }
    }

    // 2. Configurar LED de flash si está definido
    if (PIN_LED_FLASH != -1) {
        ESP_LOGI(TAG, "Inicializando LED de Flash en GPIO%d", PIN_LED_FLASH);
        gpio_config_t flash_conf = {};
        flash_conf.intr_type    = GPIO_INTR_DISABLE;
        flash_conf.mode         = GPIO_MODE_OUTPUT;
        flash_conf.pin_bit_mask = (1ULL << PIN_LED_FLASH);
        flash_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        flash_conf.pull_up_en   = GPIO_PULLUP_DISABLE;

        esp_err_t ret = gpio_config(&flash_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error configurando GPIO LED Flash");
            return ret;
        }
        gpio_set_drive_capability((gpio_num_t)PIN_LED_FLASH, GPIO_DRIVE_CAP_3);
        if (PIN_LED_FLASH != -1) {
            gpio_set_level((gpio_num_t)PIN_LED_FLASH, 0);
        }
    }

    ESP_LOGI(TAG, "LEDs inicializados correctamente");
    return ESP_OK;
}

void led_set_pattern(led_pattern_t pattern) {
    if (s_current_pattern == pattern) return;
    if (!s_blink_timer) return;

    // Parar el timer actual
    esp_timer_stop(s_blink_timer);

    s_current_pattern = pattern;
    s_blink_count     = 0;

    if (pattern == LED_PATTERN_READY || pattern == LED_PATTERN_DETECTING) {
        led_on();
    } else if (pattern == LED_PATTERN_OFF) {
        led_off();
    } else {
        // Iniciar el nuevo patrón
        esp_timer_start_once(s_blink_timer, 10 * 1000ULL);  // 10ms para el primer tick
    }

    ESP_LOGD(TAG, "Patrón LED: %d", (int)pattern);
}

void led_update_from_state(system_state_t state) {
    switch (state) {
        case SYS_STATE_INIT:         led_set_pattern(LED_PATTERN_READY);        break;
        case SYS_STATE_READY:        led_set_pattern(LED_PATTERN_READY);        break;
        case SYS_STATE_DETECTING:    led_set_pattern(LED_PATTERN_DETECTING);    break;
        case SYS_STATE_FOCUSING:     led_set_pattern(LED_PATTERN_SAVING);       break;
        case SYS_STATE_CAPTURING:    led_set_pattern(LED_PATTERN_CAPTURING);    break;
        case SYS_STATE_SAVING:       led_set_pattern(LED_PATTERN_SAVING);       break;
        case SYS_STATE_TRANSFERRING: led_set_pattern(LED_PATTERN_TRANSFERRING); break;
        case SYS_STATE_ERROR_CAM:    led_set_pattern(LED_PATTERN_ERROR_CAM);    break;
        case SYS_STATE_ERROR_SD:     led_set_pattern(LED_PATTERN_ERROR_SD);     break;
        default:                     led_set_pattern(LED_PATTERN_READY);        break;
    }
}

void led_flash_confirm(uint8_t flashes) {
    // Destello de confirmación rápido (Status LED y Flash LED)
    for (uint8_t i = 0; i < flashes; i++) {
        if (PIN_LED_STATUS != -1) gpio_set_level((gpio_num_t)PIN_LED_STATUS, 1);
        if (PIN_LED_FLASH != -1)  gpio_set_level((gpio_num_t)PIN_LED_FLASH, 1);
        vTaskDelay(pdMS_TO_TICKS(80));
        if (PIN_LED_STATUS != -1) gpio_set_level((gpio_num_t)PIN_LED_STATUS, 0);
        if (PIN_LED_FLASH != -1)  gpio_set_level((gpio_num_t)PIN_LED_FLASH, 0);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    // Restaurar LED de estado encendido para indicar listo
    if (PIN_LED_STATUS != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_STATUS, 1);
        s_led_state = true;
    }
}

void led_flash_on(void) {
    if (PIN_LED_FLASH != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_FLASH, 1);
    }
    if (PIN_LED_STATUS != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_STATUS, 1);
        s_led_state = true;
    }
}

void led_flash_off(void) {
    if (PIN_LED_FLASH != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_FLASH, 0);
    }
    if (PIN_LED_STATUS != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_STATUS, 0);
        s_led_state = false;
    }
}

void led_trigger_flash(uint32_t duration_ms) {
    if (PIN_LED_FLASH != -1) gpio_set_level((gpio_num_t)PIN_LED_FLASH, 1);
    if (PIN_LED_STATUS != -1) gpio_set_level((gpio_num_t)PIN_LED_STATUS, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    if (PIN_LED_FLASH != -1) gpio_set_level((gpio_num_t)PIN_LED_FLASH, 0);
    if (PIN_LED_STATUS != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_STATUS, 1);
        s_led_state = true;
    }
}

void led_deinit(void) {
    if (s_blink_timer) {
        esp_timer_stop(s_blink_timer);
        esp_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
    led_off();
    if (PIN_LED_FLASH != -1) {
        gpio_set_level((gpio_num_t)PIN_LED_FLASH, 0);
    }
    ESP_LOGI(TAG, "LEDs deinicializados");
}

