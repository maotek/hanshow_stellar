#pragma once

#include <stdint.h>

uint8_t EPD_SSD1608_Display(unsigned char *image, int size, uint8_t full_or_partial);
void EPD_SSD1608_set_sleep(void);
