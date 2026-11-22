set(PIXELS_BOARD_PROFILE "c6_lcd_147" CACHE STRING
    "Pixels Dice Hub display board profile")

if(PIXELS_BOARD_PROFILE STREQUAL "c6_lcd_147")
  set(PIXELS_BOARD_SOURCE "boards/c6_lcd_147.cpp")
  set(PIXELS_BOARD_REQUIRES
    esp_lcd_jd9853
    esp_lcd_touch_axs5106
  )
elseif(PIXELS_BOARD_PROFILE STREQUAL "s3_amoled_164")
  set(PIXELS_BOARD_SOURCE
    "boards/s3_amoled_164.cpp"
    "boards/s3_amoled_164_i2c.c"
  )
  set(PIXELS_BOARD_REQUIRES
    esp_lcd_sh8601
    esp_lcd_touch
  )
else()
  message(FATAL_ERROR "Unsupported PIXELS_BOARD_PROFILE: ${PIXELS_BOARD_PROFILE}")
endif()

# Development-only screenshot/touch-injection API (see dev_tools.h).
option(PIXELS_DEV_TOOLS "Build the port-8080 development API" OFF)
set(PIXELS_DEV_SOURCES "")
if(PIXELS_DEV_TOOLS)
  set(PIXELS_DEV_SOURCES "dev_tools.cpp")
endif()

idf_component_register(
  SRCS
    "app_main.cpp"
    "board.cpp"
    "${PIXELS_BOARD_SOURCE}"
    "dice_calculator.cpp"
    "dice_model.cpp"
    "pixels_ble.cpp"
    "power.cpp"
    "preferences.cpp"
    "status_led.cpp"
    "theme.cpp"
    "ui.cpp"
    "web_server.cpp"
    ${PIXELS_DEV_SOURCES}
  INCLUDE_DIRS "."
  REQUIRES
    bt
    driver
    esp_adc
    esp_driver_usb_serial_jtag
    esp_lcd
    ${PIXELS_BOARD_REQUIRES}
    esp_event
    esp_http_server
    esp_netif
    esp_timer
    esp_wifi
    nvs_flash
    pixels_protocol
)

if(PIXELS_DEV_TOOLS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE PIXELS_DEV_TOOLS=1)
endif()
