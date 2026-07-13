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

// =============================================================================
// Timer de polling (20ms)
// =============================================================================
static void poll_timer_cb(void* arg) {
    static int s_btn_state = 1;
    static int s_press_time = 0;
    
    int level = gpio_get_level((gpio_num_t)PIN_BTN_CAPTURE);
    if (level == 0) { // Presionado (LOW)
        if (s_btn_state == 1) { // Recién presionado
            s_btn_state = 0;
            s_press_time = 0;
            s_long_press_fired = false;
            s_extra_long_press_fired = false;
        } else {
            s_press_time += 20;
            if (s_press_time >= BTN_EXTRA_LONG_PRESS_MS && !s_extra_long_press_fired) {
                s_extra_long_press_fired = true;
                ESP_LOGI(TAG, "Pulsación MUY LARGA detectada (10s)");
                if (s_callback) s_callback(BTN_EVENT_EXTRA_LONG_PRESS, s_user_data);
            } else if (s_press_time >= BTN_LONG_PRESS_MS && !s_long_press_fired && !s_extra_long_press_fired) {
                s_long_press_fired = true;
                ESP_LOGI(TAG, "Pulsación LARGA detectada");
                if (s_callback) s_callback(BTN_EVENT_LONG_PRESS, s_user_data);
            }
        }
    } else { // Soltado (HIGH)
        if (s_btn_state == 0) { // Recién soltado
            s_btn_state = 1;
            if (!s_long_press_fired && !s_extra_long_press_fired && s_press_time >= BTN_DEBOUNCE_MS) {
                ESP_LOGI(TAG, "Pulsación CORTA detectada");
                if (s_callback) s_callback(BTN_EVENT_SHORT_PRESS, s_user_data);
            }
        }
    }
}

// =============================================================================
// API Pública
// =============================================================================
esp_err_t button_init(button_event_cb_t callback, void* user_data) {
    ESP_LOGI(TAG, "Inicializando botón en GPIO%d", PIN_BTN_CAPTURE);

    s_callback  = callback;
    s_user_data = user_data;

    // Configurar GPIO como entrada con pull-up
    gpio_config_t io_conf = {};
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_BTN_CAPTURE);
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

    esp_timer_start_periodic(s_poll_timer, 20 * 1000ULL);

    ESP_LOGI(TAG, "Botón inicializado (corto < %dms, largo >= %dms)",
             BTN_LONG_PRESS_MS, BTN_LONG_PRESS_MS);
    return ESP_OK;
}

bool button_is_pressed(void) {
    return gpio_get_level((gpio_num_t)PIN_BTN_CAPTURE) == 0;
}

void button_deinit(void) {
    if (s_poll_timer)  { esp_timer_stop(s_poll_timer);  esp_timer_delete(s_poll_timer);  }
    s_callback  = NULL;
    s_user_data = NULL;
    ESP_LOGI(TAG, "Botón deinicializado");
}
