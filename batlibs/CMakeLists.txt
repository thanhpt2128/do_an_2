idf_component_register(
    SRCS "battery_cc.c" "mode_control.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer nvs_flash driver
)
