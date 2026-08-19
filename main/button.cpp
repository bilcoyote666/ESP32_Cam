/**
 * @file button.cpp
 * @brief Implementación del botón con debounce por interrupción GPIO y timer
 */
#include "button.h"
#include "config.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "BUTTON";

static button_event_cb_t s_callback    = NULL;
static void*             s_user_data   = NULL;
static esp_timer_handle_t s_poll_timer = NULL;
static volatile bool      s_long_press_fired = false;
static volatile bool      s_extra_long_press_fired = false;

#define BTN_EXTRA_LONG_PRESS_MS 10000 // 10 segundos

static inline bool is_button_down(void) {
    if (gpio_get_level(GPIO_NUM_0) == 0) return true;
    if (PIN_BTN_CAPTURE != -1 && gpio_get_level((gpio_num_t)PIN_BTN_CAPTURE) == 0) return true;
    return false;
}

// =============================================================================
// Polling del botón cada 10ms con respuesta inmediata y filtro de arranque
// =============================================================================
static void poll_timer_cb(void* arg) {
    static uint32_t s_boot_grace_ticks = 100; // 1 segundo de gracia tras el arranque
    if (s_boot_grace_ticks > 0) {
        s_boot_grace_ticks--;
        return;
    }

    // 1. Botón físico externo en PIN_BTN_CAPTURE (GPIO1 / D0)
    static int s_ext_press_ms = 0;
    static bool s_ext_fired = false;
    static bool s_ext_long_fired = false;

    if (PIN_BTN_CAPTURE != -1) {
        int level = gpio_get_level((gpio_num_t)PIN_BTN_CAPTURE);
        if (level == 0) { // Pulsado a GND
            s_ext_press_ms += 10;
            if (s_ext_press_ms >= BTN_DEBOUNCE_MS && !s_ext_fired) {
                s_ext_fired = true;
                ESP_LOGI(TAG, "📸 Disparador físico (GPIO%d) pulsado", PIN_BTN_CAPTURE);
                if (s_callback) s_callback(BTN_EVENT_SHORT_PRESS, s_user_data);
            } else if (s_ext_press_ms >= BTN_LONG_PRESS_MS && !s_ext_long_fired) {
                s_ext_long_fired = true;
                ESP_LOGI(TAG, "📸 Disparador físico (GPIO%d) pulsación larga -> Ráfaga", PIN_BTN_CAPTURE);
                if (s_callback) s_callback(BTN_EVENT_LONG_PRESS, s_user_data);
            }
        } else { // Soltado
            s_ext_press_ms = 0;
            s_ext_fired = false;
            s_ext_long_fired = false;
        }
    }

    // 2. Botón integrado BOOT (GPIO0)
    static int s_boot_press_ms = 0;
    static bool s_boot_fired = false;
    static bool s_boot_long_fired = false;
    static bool s_boot_saw_high = false;

    int boot_level = gpio_get_level(GPIO_NUM_0);
    if (boot_level == 1) {
        s_boot_saw_high = true; // Confirmamos que no está forzado por USB
        s_boot_press_ms = 0;
        s_boot_fired = false;
        s_boot_long_fired = false;
    } else if (s_boot_saw_high) { // Solo si ya estuvo en reposo (HIGH)
        s_boot_press_ms += 10;
        if (s_boot_press_ms >= BTN_DEBOUNCE_MS && !s_boot_fired) {
            s_boot_fired = true;
            ESP_LOGI(TAG, "📸 Botón BOOT (GPIO0) pulsado");
            if (s_callback) s_callback(BTN_EVENT_SHORT_PRESS, s_user_data);
        } else if (s_boot_press_ms >= BTN_LONG_PRESS_MS && !s_boot_long_fired) {
            s_boot_long_fired = true;
            ESP_LOGI(TAG, "📸 Botón BOOT (GPIO0) pulsación larga -> Ráfaga");
            if (s_callback) s_callback(BTN_EVENT_LONG_PRESS, s_user_data);
        }
    }
}

// =============================================================================
// API Pública
// =============================================================================
esp_err_t button_init(button_event_cb_t callback, void* user_data) {
    ESP_LOGI(TAG, "Inicializando botones (BOOT GPIO0 y externo GPIO%d)", PIN_BTN_CAPTURE);

    s_callback  = callback;
    s_user_data = user_data;

    // Configurar GPIOs como entrada con pull-up
    gpio_config_t io_conf = {};
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_INPUT;
    uint64_t mask = (1ULL << GPIO_NUM_0);
    if (PIN_BTN_CAPTURE != -1) {
        mask |= (1ULL << PIN_BTN_CAPTURE);
    }
    io_conf.pin_bit_mask = mask;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;   // Pull-up interno: botón a GND

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Crear timer de polling periódico (20ms)
    esp_timer_create_args_t poll_args = {
        .callback        = poll_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "btn_poll",
        .skip_unhandled_events = true,
    };
    ret = esp_timer_create(&poll_args, &s_poll_timer);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Error creando timer poll"); return ret; }

    esp_timer_start_periodic(s_poll_timer, 10 * 1000ULL);

    ESP_LOGI(TAG, "Botón inicializado (corto < %dms, largo >= %dms)",
             BTN_LONG_PRESS_MS, BTN_LONG_PRESS_MS);
    return ESP_OK;
}

bool button_is_pressed(void) {
    return is_button_down();
}

void button_deinit(void) {
    if (s_poll_timer)  { esp_timer_stop(s_poll_timer);  esp_timer_delete(s_poll_timer);  }
    s_callback  = NULL;
    s_user_data = NULL;
    ESP_LOGI(TAG, "Botón deinicializado");
}
