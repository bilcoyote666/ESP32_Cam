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
static esp_timer_handle_t s_debounce_timer  = NULL;
static esp_timer_handle_t s_long_press_timer = NULL;
static volatile bool      s_long_press_fired = false;

// =============================================================================
// Timers de debounce y pulsación larga
// =============================================================================

// Llamado después del debounce: determina si el botón sigue presionado
static void debounce_timer_cb(void* arg) {
    // Leer el estado actual del botón (nivel LOW = presionado con pull-up)
    int level = gpio_get_level((gpio_num_t)PIN_BTN_CAPTURE);

    if (level == 0) {
        // Botón presionado — iniciar timer de pulsación larga
        s_long_press_fired = false;
        esp_timer_start_once(s_long_press_timer, BTN_LONG_PRESS_MS * 1000ULL);
        ESP_LOGD(TAG, "Botón presionado");
    } else {
        // Botón soltado
        esp_timer_stop(s_long_press_timer);

        if (!s_long_press_fired) {
            // Era una pulsación corta
            ESP_LOGI(TAG, "Pulsación CORTA detectada");
            if (s_callback) s_callback(BTN_EVENT_SHORT_PRESS, s_user_data);
        }
        // Si s_long_press_fired == true, el evento largo ya se emitió
    }
}

// Llamado si el botón se mantiene presionado > BTN_LONG_PRESS_MS
static void long_press_timer_cb(void* arg) {
    s_long_press_fired = true;
    ESP_LOGI(TAG, "Pulsación LARGA detectada");
    if (s_callback) s_callback(BTN_EVENT_LONG_PRESS, s_user_data);
}

// =============================================================================
// ISR del GPIO (llamado en cada flanco del botón)
// =============================================================================
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    // Reiniciar el timer de debounce desde la ISR
    // Usamos esp_timer_stop/start que son ISR-safe
    esp_timer_stop(s_debounce_timer);
    esp_timer_start_once(s_debounce_timer, BTN_DEBOUNCE_MS * 1000ULL);
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
    io_conf.intr_type    = GPIO_INTR_ANYEDGE;   // Interrupción en ambos flancos
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_BTN_CAPTURE);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;   // Pull-up interno: botón a GND

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Crear timer de debounce (one-shot)
    esp_timer_create_args_t debounce_args = {
        .callback        = debounce_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "btn_debounce",
        .skip_unhandled_events = true,
    };
    ret = esp_timer_create(&debounce_args, &s_debounce_timer);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Error creando timer debounce"); return ret; }

    // Crear timer de pulsación larga (one-shot)
    esp_timer_create_args_t longpress_args = {
        .callback        = long_press_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "btn_longpress",
        .skip_unhandled_events = true,
    };
    ret = esp_timer_create(&longpress_args, &s_long_press_timer);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Error creando timer long press"); return ret; }

    // Instalar servicio de ISR (si no está ya instalado)
    gpio_install_isr_service(0);

    // Registrar handler específico para este GPIO
    ret = gpio_isr_handler_add((gpio_num_t)PIN_BTN_CAPTURE, gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error añadiendo ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Botón inicializado (corto < %dms, largo >= %dms)",
             BTN_LONG_PRESS_MS, BTN_LONG_PRESS_MS);
    return ESP_OK;
}

bool button_is_pressed(void) {
    return gpio_get_level((gpio_num_t)PIN_BTN_CAPTURE) == 0;
}

void button_deinit(void) {
    gpio_isr_handler_remove((gpio_num_t)PIN_BTN_CAPTURE);
    if (s_debounce_timer)  { esp_timer_stop(s_debounce_timer);  esp_timer_delete(s_debounce_timer);  }
    if (s_long_press_timer){ esp_timer_stop(s_long_press_timer); esp_timer_delete(s_long_press_timer); }
    s_callback  = NULL;
    s_user_data = NULL;
    ESP_LOGI(TAG, "Botón deinicializado");
}
