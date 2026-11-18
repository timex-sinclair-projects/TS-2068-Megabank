cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
set(PICO_BOARD megabank CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/../bringup/boards)

include(${CMAKE_CURRENT_LIST_DIR}/../bringup/pico_sdk_import.cmake)

project(megabank_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_executable(megabank main.c)
pico_generate_pio_header(megabank ${CMAKE_CURRENT_LIST_DIR}/megabank.pio)

target_link_libraries(megabank
	pico_stdlib
	pico_multicore
	hardware_adc
	hardware_dma
	hardware_pio
)

pico_enable_stdio_usb(megabank 1)
pico_enable_stdio_uart(megabank 0)
pico_add_extra_outputs(megabank)
