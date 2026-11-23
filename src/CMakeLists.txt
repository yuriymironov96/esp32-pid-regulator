idf_component_register(
    SRCS "main.c" "pid.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_ledc esp_timer
)
