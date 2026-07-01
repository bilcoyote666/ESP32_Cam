cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "main.cpp"
        "camera.cpp"
        "face_detect.cpp"
        "sd_storage.cpp"
        "wifi_ap.cpp"
        "http_server.cpp"
        "button.cpp"
        "led.cpp"
        "ble_transfer.cpp"
        "auth.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp32-camera
        driver
        fatfs
        sdmmc
        nvs_flash
        esp_wifi
        lwip
        esp_http_server
        bt
        esp_event
        esp_timer
        freertos
        log
)

# Embeber los archivos de la aplicación web directamente en el binario
target_add_binary_data(${COMPONENT_LIB} "../web-app/index.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../web-app/styles.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../web-app/app.js" TEXT)
