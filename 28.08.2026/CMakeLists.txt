cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD vcc-gnd_yd-rp2040_4m)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(luftfugl C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(LUFTFUGL_DEBUG "Build interactive debug monitor" ON)
option(LUFTFUGL_TRACE_INPUT "Trace every received console character" OFF)
option(LUFTFUGL_TRACE_OUTPUT "Trace debug output ring activity" OFF)

add_executable(luftfugl
    src/main.c
    src/motor.c
    src/encoder.c
    src/controller.c
    src/console.c
    src/led.c
    src/buzzer.c
    src/co2.c
    src/power_monitor.c
)

pico_generate_pio_header(luftfugl ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

if(LUFTFUGL_DEBUG)
    target_sources(luftfugl PRIVATE src/debug.c src/config.c)
    target_compile_definitions(luftfugl PRIVATE
        LUFTFUGL_DEBUG=1
        LUFTFUGL_MONITOR=1
    )
endif()
if(LUFTFUGL_TRACE_INPUT)
    target_compile_definitions(luftfugl PRIVATE LUFTFUGL_TRACE_INPUT=1)
endif()

if(LUFTFUGL_TRACE_OUTPUT)
    target_compile_definitions(luftfugl PRIVATE LUFTFUGL_TRACE_OUTPUT=1)
endif()
target_include_directories(luftfugl PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(luftfugl PRIVATE -Wall -Wextra)

target_link_libraries(luftfugl
    pico_stdlib
    hardware_pwm
    hardware_adc
    hardware_timer
    hardware_watchdog
    hardware_pio
    hardware_i2c
    hardware_flash
)

# Console is a raw UART driver, not SDK stdio. See agent.md §15.3.
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

add_executable(adc_uart_test test/adc_uart_test.c)
target_include_directories(adc_uart_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(adc_uart_test PRIVATE -Wall -Wextra)
target_link_libraries(adc_uart_test pico_stdlib hardware_adc hardware_uart)
pico_enable_stdio_uart(adc_uart_test 0)
pico_enable_stdio_usb(adc_uart_test 0)
pico_add_extra_outputs(adc_uart_test)

add_executable(pot_motor_test test/pot_motor_test.c src/motor.c)
target_include_directories(pot_motor_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(pot_motor_test PRIVATE -Wall -Wextra)
target_link_libraries(pot_motor_test pico_stdlib hardware_adc hardware_pwm hardware_uart)
pico_enable_stdio_uart(pot_motor_test 0)
pico_enable_stdio_usb(pot_motor_test 0)
pico_add_extra_outputs(pot_motor_test)
