idf_component_register(
    SRCS "main.c"
         "../assets/fonts/passport_font_14.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_radio.c"
         "app_wifi.c"
         "tasks_model.c"
         "tasks_client.c"
         "passport_protocol.c"
         "passport_ble.c"
         "passport_voice.c"
         "demo_tasks.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_http_client esp_netif esp_timer esp_wifi json nvs_flash
)
