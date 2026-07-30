#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213_ice.h"
#include "drivers.h"

/* Hanshow Stellar Ice: SSD1675/SSD1680-family monochrome 212 x 104 panel. */
#define ICE_PARTIAL_LUT_LENGTH 50

static uint8_t ice_partial_lut[] = {
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ICE_PARTIAL_LUT_LENGTH, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void ice_setup(void)
{
    EPD_WriteCmd(0x12);
    EPD_CheckStatus_inverted(100);
    EPD_WriteCmd(0x74); EPD_WriteData(0x54);
    EPD_WriteCmd(0x7e); EPD_WriteData(0x3b);
    EPD_WriteCmd(0x2b); EPD_WriteData(0x04); EPD_WriteData(0x63);
    EPD_WriteCmd(0x0c);
    EPD_WriteData(0x8b); EPD_WriteData(0x9c);
    EPD_WriteData(0x96); EPD_WriteData(0x0f);
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x28); EPD_WriteData(0x01); EPD_WriteData(0x01);
    EPD_WriteCmd(0x11); EPD_WriteData(0x01);
    EPD_WriteCmd(0x18); EPD_WriteData(0x80);
    EPD_WriteCmd(0x44); EPD_WriteData(0x00); EPD_WriteData(0x0c);
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x28); EPD_WriteData(0x01);
    EPD_WriteData(0x54); EPD_WriteData(0x00);
    EPD_WriteCmd(0x3c); EPD_WriteData(0x01);
    EPD_WriteCmd(0x22); EPD_WriteData(0xa1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);
}

static uint8_t ice_read_temperature(void)
{
    uint8_t temperature;
    EPD_WriteCmd(0x1b);
    temperature = EPD_SPI_read();
    EPD_SPI_read();
    WaitMs(5);
    EPD_WriteCmd(0x22); EPD_WriteData(0xb1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);
    EPD_WriteCmd(0x21); EPD_WriteData(0x03);
    return temperature;
}

_attribute_ram_code_ uint8_t EPD_BW_213_ICE_read_temp(void)
{
    uint8_t temperature;
    ice_setup();
    temperature = ice_read_temperature();
    EPD_WriteCmd(0x10); EPD_WriteData(0x01);
    return temperature;
}

_attribute_ram_code_ uint8_t EPD_BW_213_ICE_Display(
    unsigned char *image, int size, uint8_t full_or_partial)
{
    uint8_t temperature;
    int i;
    ice_setup();
    temperature = ice_read_temperature();
    EPD_WriteCmd(0x4e); EPD_WriteData(0x00);
    EPD_WriteCmd(0x4f); EPD_WriteData(0x28); EPD_WriteData(0x01);
    EPD_LoadImage(image, size, 0x24);
    EPD_WriteCmd(0x22); EPD_WriteData(0x40);
    if (!full_or_partial) {
        EPD_WriteCmd(0x32);
        for (i = 0; i < sizeof(ice_partial_lut); i++)
            EPD_WriteData(ice_partial_lut[i]);
    }
    EPD_WriteCmd(0x22); EPD_WriteData(0xc7);
    EPD_WriteCmd(0x20);
    return temperature;
}

_attribute_ram_code_ void EPD_BW_213_ICE_set_sleep(void)
{
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);
}
