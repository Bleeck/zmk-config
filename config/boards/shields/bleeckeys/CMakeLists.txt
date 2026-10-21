if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM AND CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(status_screen.c)
endif()

if(CONFIG_ZMK_BATTERY_REPORTING)
  zephyr_library_sources(battery.c)
endif()
