cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "main.cpp"
        "camera.cpp"
        "face_detect.cpp"
        "sd_storage.cpp"
        "ble_transfer.cpp"
        "bt_classic.cpp"
        "button.cpp"
        "led.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_camera
        driver
        fatfs
        sdmmc
        nvs_flash
        bt
        nimble
        esp_event
        esp_timer
        freertos
        log
)
