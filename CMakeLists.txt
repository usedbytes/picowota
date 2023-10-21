# Derived from the Pico SDK, which carries the following
# LICENSE.txt:
# Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
#    disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picowota C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(picowota
	main.c
	tcp_comm.c
	dhcpserver/dhcpserver.c
)

function(target_cl_options option)
    target_compile_options(picowota PRIVATE ${option})
    target_link_options(picowota PRIVATE ${option})
endfunction()

target_cl_options("-Wall")
target_cl_options("-Os")
target_cl_options("-ffunction-sections")
target_cl_options("-fdata-sections")
target_link_options(picowota PRIVATE "LINKER:--gc-sections,--print-memory-usage")

pico_add_extra_outputs(picowota)

target_include_directories(picowota PRIVATE
	${CMAKE_CURRENT_LIST_DIR} # Needed so that lwip can find lwipopts.h
	${CMAKE_CURRENT_LIST_DIR}/dhcpserver)

pico_enable_stdio_usb(picowota 1)

add_subdirectory(picowota_reboot)

target_link_libraries(picowota
	cmsis_core
	hardware_dma
	hardware_flash
	hardware_resets
	hardware_structs
	pico_cyw43_arch_lwip_poll
	pico_stdlib
	pico_sync
	pico_util
	picowota_reboot
)

# Retrieves build variables from the environment if present
function(picowota_retrieve_variable name hidden)
	if (DEFINED ENV{${name}} AND (NOT ${name}))
		set(${name} $ENV{${name}} PARENT_SCOPE)
		if (hidden)
			set(log_value "hidden")
		else()
			set(log_value "'$ENV{${name}}'")
		endif()
		message("Using ${name} from environment (${log_value})")
	endif()
endfunction()

picowota_retrieve_variable(PICOWOTA_WIFI_SSID false)
picowota_retrieve_variable(PICOWOTA_WIFI_PASS true)
picowota_retrieve_variable(PICOWOTA_WIFI_AP false)

if ((NOT PICOWOTA_WIFI_SSID) OR (NOT PICOWOTA_WIFI_PASS))
        message(FATAL_ERROR
		"WiFi SSID/Pass not set, please set PICOWOTA_WIFI_SSID/PICOWOTA_WIFI_PASS."
	)
endif ()

# TODO: This causes a full rebuild if they change, configure_file might
# be better.
target_compile_definitions(picowota PUBLIC PICOWOTA_WIFI_SSID=${PICOWOTA_WIFI_SSID})
target_compile_definitions(picowota PUBLIC PICOWOTA_WIFI_PASS=${PICOWOTA_WIFI_PASS})

# Use the WiFi AP mode upon request
if (PICOWOTA_WIFI_AP)
	target_compile_definitions(picowota PUBLIC PICOWOTA_WIFI_AP=1)
	message("Building in WiFi AP mode.")
endif()

# Provide a helper to build a standalone target
function(picowota_build_standalone NAME)
	get_target_property(PICOWOTA_SRC_DIR picowota SOURCE_DIR)
	pico_set_linker_script(${NAME} ${PICOWOTA_SRC_DIR}/standalone.ld)
	pico_add_bin_output(${NAME})
endfunction()

# Provide a helper to build a combined target
# The build process is roughly:
# 1. Build the bootloader, using a special linker script which leaves
#    two sections to be filled in with the header (.app_hdr) and
#    app binary (.app_bin)
# 2. Build the app binary, using a special linker script to set the load
#    address properly and skip boot2.
# 3. Calculate the checksum of the app binary
# 4. Update the header and binary sections in the ELF from 1.
function(picowota_build_combined NAME)
	set(APP_BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.bin)
	set(APP_HDR_BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_hdr.bin)
	set(COMBINED picowota_${NAME})
	get_target_property(PICOWOTA_SRC_DIR picowota SOURCE_DIR)
	get_target_property(PICOWOTA_BIN_DIR picowota BINARY_DIR)

	# The app must be built with the correct linker script (and a .bin)
	picowota_build_standalone(${NAME})

	# Build the bootloader with the sections to fill in
	pico_set_linker_script(picowota ${PICOWOTA_SRC_DIR}/bootloader_shell.ld)

	# TODO: The hard-coded address here is a bit nasty
	add_custom_target(${NAME}_hdr DEPENDS ${APP_BIN})
	add_custom_command(TARGET ${NAME}_hdr DEPENDS ${APP_BIN}
		COMMAND ${PICOWOTA_SRC_DIR}/gen_imghdr.py -a 0x1005B000 ${APP_BIN} ${APP_HDR_BIN}
	)

	add_custom_target(${COMBINED} ALL)
	add_dependencies(${COMBINED} picowota ${NAME}_hdr ${NAME})
	add_custom_command(TARGET ${COMBINED} DEPENDS ${APP_HDR_BIN} ${APP_BIN}
		COMMAND ${CMAKE_OBJCOPY}
			--update-section .app_hdr=${APP_HDR_BIN}
			--update-section .app_bin=${APP_BIN} ${PICOWOTA_BIN_DIR}/picowota.elf ${COMBINED}.elf
	)

	add_custom_command(TARGET ${COMBINED} POST_BUILD
		COMMAND ${CMAKE_OBJCOPY} -Obinary ${COMBINED}.elf ${COMBINED}.bin
	)

	if (NOT ELF2UF2_FOUND)
		set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PICO_SDK_PATH}/tools)
		find_package(ELF2UF2)
	endif()
	if (ELF2UF2_FOUND)
		add_custom_command(TARGET ${COMBINED} POST_BUILD
			COMMAND ELF2UF2 ${COMBINED}.elf ${COMBINED}.uf2
	)
	endif()
endfunction()
