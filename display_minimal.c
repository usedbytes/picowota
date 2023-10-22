#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <u8g2.h>


#include "display_minimal.h"


// Local variables
u8g2_t display_handler;

u8g2_t * get_display_handler(void) {
    return &display_handler;
}


void write_bl_info_AP(const char *wifi_ssid, const char *wifi_pass){
    u8g2_SetMaxClipWindow(&display_handler);
    u8g2_SetFont(&display_handler, u8g2_font_5x8_tr);
    u8g2_DrawStr(&display_handler, 3, 15, "Bootloader in AP Mode");
    u8g2_DrawLine(&display_handler, 3, 19, 125, 19);
    u8g2_DrawStr(&display_handler, 3, 30, wifi_ssid);
    u8g2_DrawStr(&display_handler, 3, 40, wifi_pass);
    u8g2_DrawStr(&display_handler, 3, 50, "192.168.4.1:4242");
    u8g2_UpdateDisplay(&display_handler);
}

void write_bl_info_STA(const char *wifi_ssid, bool status){
    u8g2_SetMaxClipWindow(&display_handler);
    u8g2_SetFont(&display_handler, u8g2_font_5x8_tr);
    u8g2_DrawStr(&display_handler, 3, 15, "Bootloader in STA Mode");
    u8g2_DrawLine(&display_handler, 3, 19, 125, 19);

    if (status) {
        u8g2_DrawStr(&display_handler, 3, 30, "Successfully connected to:");
        u8g2_DrawStr(&display_handler, 3, 40, wifi_ssid);
        u8g2_DrawStr(&display_handler, 3, 50, "Use serial-flash to");
        u8g2_DrawStr(&display_handler, 3, 60, "update new app.elf.");
    } 
    else {
        u8g2_DrawStr(&display_handler, 3, 30, "FAILED connecting to:");
        u8g2_DrawStr(&display_handler, 3, 40, wifi_ssid);
    }
    
    u8g2_UpdateDisplay(&display_handler);
}

/* u8g2 buffer structure can be decoded according to the description here: 
    https://github.com/olikraus/u8g2/wiki/u8g2reference#memory-structure-for-controller-with-u8x8-support

    Here is the Python script helping to explain how u8g2 buffer are arranged.

        with open(f, 'rb') as fp:
            display_buffer = fp.read()

        tile_width = 0x10  # 16 tiles per tile row

        for tile_row_idx in range(8):
            for bit in range(8):
                # Each tile row includes 16 * 8 bytes
                for byte_idx in range(tile_width * 8):
                    data_offset = byte_idx + tile_row_idx * tile_width * 8
                    data = display_buffer[data_offset]
                    if (1 << bit) & data:
                        print(' * ', end='')
                    else:
                        print('   ', end='')

                print()

*/