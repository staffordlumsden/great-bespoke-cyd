set(CM_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../../../../common/terminal-core")
idf_component_register(
    SRCS "${CM_MAIN}/bluetooth_keyboard.cpp"
         "${CM_MAIN}/keyboard_mapping.cpp"
         "${CM_MAIN}/keyboard_report.cpp"
         "wipe.cpp"
    INCLUDE_DIRS "${CM_MAIN}"
    REQUIRES bt esp_hid nvs_flash)
