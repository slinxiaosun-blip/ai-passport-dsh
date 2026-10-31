# 截图服务默认关掉：整屏缓冲要 150KB 静态 BSS，而本机可用堆只有 166KB、
# 最大连续块 113KB。带上它之后笔记固件连 24KB 的包缓冲都申请不到。
# 需要截图时单独编一版（-DAP_SCREENSHOT=ON，不带 Wi-Fi）。
option(AP_SCREENSHOT "编译串口截图服务（会占 150KB DRAM）" OFF)
if(AP_SCREENSHOT)
    set(APP_SCREENSHOT_SRC "app_screenshot.c")
else()
    set(APP_SCREENSHOT_SRC "")
endif()

# 应用入口固定为 AI Passport × DSH 联机界面（passport_app_main）。
# 2026-10-31：notes（笔记阅读器）与 lightning（闪电说外挂）两个 app 已移除，
# 源码在 .workbuddy/removed-2026-10-31/ 留底，需要时再捡回来。

idf_component_register(
    SRCS "main.c"
         "app_proto.c"
         "app_link.c"
         "app_question_logic.c"
         "app_settings_logic.c"
         "app_settings.c"
         "app_pair_logic.c"
         "app_pair.c"
         "app_conn_logic.c"
         "app_clock_logic.c"
         "app_ui.c"
         "app_screens.c"
         "app_alert.c"
         "app_voice.c"
         "app.c"
         "fonts/bitfont_8.c"
         "fonts/bitfont_16.c"
         "fonts/app_ui_font_cjk.c"
         "fonts/app_ui_font_cjk16.c"
         "fonts/app_ui_font_xl.c"
         "fonts/app_ui_font_timer.c"
         "utf8_clamp.c"
         ${APP_SCREENSHOT_SRC}
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_wifi.c"
         "demo_ble.c"
         "demo_low_power.c"
    INCLUDE_DIRS "."
    # 串口截屏默认关闭（整屏缓冲 150KB 放不下，见 main/Kconfig.projbuild）

    REQUIRES bsp bt esp_driver_usb_serial_jtag esp_event esp_hw_support esp_netif esp_timer esp_wifi esp_http_server nvs_flash spiffs
)

# ── 固件版本 ────────────────────────────────────────────────────────────────
# 基底版本：改 version.txt 一行即可（不要写死在代码里，否则必然忘记同步）。
# 覆盖方式：cmake -DAP_VERSION=1.0.0-rc1（整串替换，不再追加 git 描述）。
# 完整串 = <基底>+g<git 短哈希>[-dirty]；取不到 git 时只留基底。
if(NOT AP_VERSION)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../version.txt)
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../version.txt AP_VERSION_RAW)
        string(STRIP "${AP_VERSION_RAW}" AP_VERSION)
    endif()
    if(NOT AP_VERSION)
        set(AP_VERSION "0.0.0")
    endif()
    execute_process(
        COMMAND git describe --always --dirty --tags
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        OUTPUT_VARIABLE AP_GIT_DESC
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(AP_GIT_DESC)
        set(AP_VERSION "${AP_VERSION}+g${AP_GIT_DESC}")
    endif()
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE AP_FIRMWARE_VERSION="${AP_VERSION}")
message(STATUS "固件版本：${AP_VERSION}（基底来自 version.txt，可用 -DAP_VERSION=… 覆盖）")
message(STATUS "应用入口：AI Passport × DSH（桌面挂件界面）")
