#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_ssd1608.h"
#include "drivers.h"

// SSD1608 monochrome 200 x 200 EPD controller.
//
// The SSD1608 has one display RAM (command 0x24). Command 0x26, used by
// SSD168x tri-colour controllers for a second colour plane, is reserved here.
#define SSD1608_WIDTH_BYTES 25
#define SSD1608_HEIGHT      200

static uint8_t LUTDefault_full[] = {
    0x50, 0xaa, 0x55, 0xaa, 0x11,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x88, 0x88, 0x18,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
};

static uint8_t LUTDefault_part[] = {
    0x10, 0x18, 0x18, 0x08, 0x18,
    0x18, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x14, 0x44, 0x12,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static _attribute_ram_code_ void EPD_SSD1608_set_ram_pointer(void);

static _attribute_ram_code_ void EPD_SSD1608_init(uint8_t reset_controller)
{
    // GxEPD2 keeps the controller RAM alive between partial updates. Reset
    // only for the first/full refresh, because a reset breaks that history.
    if (reset_controller)
        EPD_WriteCmd(0x12);

    // Enable the controller clock and charge pump.
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xc0);
    EPD_WriteCmd(0x20);
    WaitMs(80);

    // Gate count = 200, normal gate scan.
    EPD_WriteCmd(0x01);
    EPD_WriteData((SSD1608_HEIGHT - 1) & 0xff);
    EPD_WriteData((SSD1608_HEIGHT - 1) >> 8);
    EPD_WriteData(0x00);

    EPD_WriteCmd(0x0c);
    EPD_WriteData(0xd7);
    EPD_WriteData(0xd6);
    EPD_WriteData(0x9d);

    EPD_WriteCmd(0x2c);
    EPD_WriteData(0xa8);

    EPD_WriteCmd(0x3a);
    EPD_WriteData(0x1a);

    EPD_WriteCmd(0x3b);
    EPD_WriteData(0x08);

    // X and Y both increment after a RAM write.
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x03);

    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(SSD1608_WIDTH_BYTES - 1);

    EPD_WriteCmd(0x45);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData((SSD1608_HEIGHT - 1) & 0xff);
    EPD_WriteData((SSD1608_HEIGHT - 1) >> 8);

    EPD_SSD1608_set_ram_pointer();

    EPD_WriteCmd(0x3c);
    EPD_WriteData(0x05);
}

static _attribute_ram_code_ void EPD_SSD1608_set_ram_pointer(void)
{
    EPD_WriteCmd(0x4e);
    EPD_WriteData(0x00);

    EPD_WriteCmd(0x4f);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
}

_attribute_ram_code_ uint8_t EPD_SSD1608_Display(
    unsigned char *image, int size, uint8_t full_or_partial)
{
    uint8_t *lut;
    uint32_t i;

    EPD_SSD1608_init(full_or_partial);

    EPD_SSD1608_set_ram_pointer();
    EPD_LoadImage(image, size, 0x24);

    lut = full_or_partial ? LUTDefault_full : LUTDefault_part;
    EPD_WriteCmd(0x32);
    for (i = 0; i < 30; i++)
        EPD_WriteData(lut[i]);

    EPD_WriteCmd(0x3c);
    EPD_WriteData(0x05);

    EPD_WriteCmd(0x22);
    EPD_WriteData(full_or_partial ? 0xc4 : 0x04);
    EPD_WriteCmd(0x20);

    // NOP/terminate command used by the reference sequence.
    EPD_WriteCmd(0xff);

    return 0;
}

_attribute_ram_code_ void EPD_SSD1608_set_sleep(void)
{
    // GxEPD-style power-off: stop the charge pump and oscillator but keep
    // VCI and display RAM powered for the next differential refresh.
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xc3);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);
}
