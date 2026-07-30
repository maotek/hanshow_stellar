#pragma once

#include <stdint.h>

uint8_t EPD_BW_213_ICE_read_temp(void);
uint8_t EPD_BW_213_ICE_Display(unsigned char *image, int size,
                               uint8_t full_or_partial);
void EPD_BW_213_ICE_set_sleep(void);
