cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picowota C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(picowota
	main.c
	creds.c
	tcp_comm.c
)

pico_enable_stdio_usb(picowota 1)

pico_add_extra_outputs(picowota)

target_include_directories(picowota PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(picowota
	cmsis_core
	hardware_dma
	hardware_flash
	hardware_resets
	hardware_structs
	pico_cyw43_arch_lwip_poll
	pico_stdlib
)
