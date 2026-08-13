cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD luftfugl_rp2040)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(luftfugl C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(LUFTFUGL_DEBUG "Build interactive debug monitor" ON)
option(LUFTFUGL_TRACE_INPUT "Trace every received console character" OFF)

add_executable(luftfugl
    src/main.c
    src/motor.c
    src/encoder.c
    src/controller.c
    src/console.c
    src/debug.c
    src/config.c
)

# Development firmware currently exposes the complete monitor in both builds.
# Keep the option so the shipped-build boundary can be restored later.
target_compile_definitions(luftfugl PRIVATE LUFTFUGL_MONITOR=1 LUFTFUGL_DEBUG=1)
if(LUFTFUGL_TRACE_INPUT)
    target_compile_definitions(luftfugl PRIVATE LUFTFUGL_TRACE_INPUT=1)
endif()

target_include_directories(luftfugl PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(luftfugl PRIVATE -Wall -Wextra)

target_link_libraries(luftfugl
    pico_stdlib
    hardware_pwm
    hardware_adc
    hardware_timer
    hardware_watchdog
)

# Console is a raw UART0 driver, not SDK stdio. See agent.md §15.3.
pico_enable_stdio_uart(luftfugl 0)
pico_enable_stdio_usb(luftfugl 0)

pico_add_extra_outputs(luftfugl)

add_executable(minimal test/minimal.c)
target_compile_options(minimal PRIVATE -Wall -Wextra)
target_link_libraries(minimal pico_stdlib hardware_uart)
pico_enable_stdio_uart(minimal 0)
pico_enable_stdio_usb(minimal 0)
pico_add_extra_outputs(minimal)

add_executable(blink test/blink.c)
target_compile_options(blink PRIVATE -Wall -Wextra)
target_link_libraries(blink pico_stdlib)
pico_enable_stdio_uart(blink 0)
pico_enable_stdio_usb(blink 0)
pico_add_extra_outputs(blink)

add_executable(uart_echo2 test/uart_echo2.c)
target_link_libraries(uart_echo2 pico_stdlib hardware_uart)
pico_enable_stdio_uart(uart_echo2 0)
pico_enable_stdio_usb(uart_echo2 0)
pico_add_extra_outputs(uart_echo2)
