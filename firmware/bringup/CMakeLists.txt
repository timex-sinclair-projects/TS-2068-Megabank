cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
set(PICO_BOARD megabank CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(megabank_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_executable(bringup main.c)

target_link_libraries(bringup
	pico_stdlib
	pico_unique_id
	pico_flash
	pico_multicore
	hardware_adc
	hardware_flash
)

pico_enable_stdio_usb(bringup 1)
pico_enable_stdio_uart(bringup 0)
pico_add_extra_outputs(bringup)
