add_library(picowota_reboot INTERFACE)

target_include_directories(picowota_reboot INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(picowota_reboot INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/reboot.c
)

target_link_libraries(picowota_reboot INTERFACE
	cmsis_core
	hardware_structs
)
