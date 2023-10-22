#ifndef DISPLAY_MINIMAL_H_
#define DISPLAY_MINIMAL_H_

#include <u8g2.h>
#include "pico/cyw43_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

u8g2_t *get_display_handler(void);

void display_minimal_init(void);

void write_bl_info_AP(const char *wifi_ssid, const char *wifi_pass);
void write_bl_info_STA(const char *wifi_ssid, bool status);

#ifdef __cplusplus
}
#endif


#endif  // DISPLAY_H_