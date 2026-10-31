# KyNangSo board sources; tests are built separately.
set(KYNANGSO_BOARD_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/kynangso_robot.cc"
    "${CMAKE_CURRENT_LIST_DIR}/core/robot_controller.cc"
    "${CMAKE_CURRENT_LIST_DIR}/core/robot_settings.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/oled/oled_display_controller.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/oled/secondary_display.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/tft/kynangso_robot_display.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/tft/robot_eyes.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/tft/robot_screen_layout.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/tft/robot_spectrum.cc"
    "${CMAKE_CURRENT_LIST_DIR}/display/tft/robot_time_font.c"
    "${CMAKE_CURRENT_LIST_DIR}/display/tft/robot_time_view.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/live_tv/robot_live_tv.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/live_tv/robot_tv_controller.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/media/robot_music_session.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/motion/robot_drive.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/payment/robot_qr_code.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/storage/sd_storage.cc"
    "${CMAKE_CURRENT_LIST_DIR}/features/time/robot_time_service.cc"
    "${CMAKE_CURRENT_LIST_DIR}/integration/robot_bridge_provisioning.cc"
    "${CMAKE_CURRENT_LIST_DIR}/integration/robot_mcp_tools.cc"
    "${CMAKE_CURRENT_LIST_DIR}/integration/web_chat_bridge.cc"
    "${CMAKE_CURRENT_LIST_DIR}/web/server/sd_web_server.cc"
    "${CMAKE_CURRENT_LIST_DIR}/web/server/tv_web_server.cc"
    "${CMAKE_CURRENT_LIST_DIR}/web/server/web_server.cc"
)
list(APPEND BOARD_SOURCES ${KYNANGSO_BOARD_SOURCES})
list(APPEND INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
