# Create an INTERFACE library for our C module.
add_library(usermod_picowotareboot INTERFACE)

# Add our source files to the lib
target_sources(usermod_picowotareboot INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/reboot.c
    ${CMAKE_CURRENT_LIST_DIR}/reboot_mp.c
)

# Add the current directory as an include directory.
target_include_directories(usermod_picowotareboot INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_picowotareboot)