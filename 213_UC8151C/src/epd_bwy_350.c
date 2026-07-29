#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwy_350.h"

// UC8151C-compatible controller path from the working BWY350 driver.
enum PSR_FLAGS
{
    RES_128x296 = 0x80,
    LUT_OTP = 0x00,
    FORMAT_BWR = 0x00,
    SCAN_UP = 0x08,
    SHIFT_RIGHT = 0x04,
    BOOSTER_ON = 0x02,
    RESET_NONE = 0x01
};

#define SCAN_DIRECTION (SCAN_UP | RES_128x296 | FORMAT_BWR | \
                        BOOSTER_ON | RESET_NONE | LUT_OTP | SHIFT_RIGHT)

static uint8_t EPD_BWY_350_power_on_and_read_temp(void)
{
    uint8_t temperature;

    // Explicitly enable the internal sensor with zero calibration offset.
    EPD_WriteCmd(0x41);
    EPD_WriteData(0x00);

    // Power-on performs the UC8151C one-shot temperature conversion.
    EPD_WriteCmd(0x04);
    WaitMs(1);
    EPD_CheckStatus(100);

    EPD_WriteCmd(0x40);
    EPD_CheckStatus(100);
    temperature = EPD_SPI_read();
    EPD_SPI_read();
    return temperature;
}

_attribute_ram_code_ uint8_t EPD_BWY_350_read_temp(void)
{
    uint8_t temperature;

    temperature = EPD_BWY_350_power_on_and_read_temp();

    EPD_WriteCmd(0x02);
    EPD_WriteCmd(0x07);
    EPD_WriteData(0xa5);
    return temperature;
}

_attribute_ram_code_ uint8_t EPD_BWY_350_Display(
    unsigned char *image, unsigned char *red_image, int size,
    uint8_t full_or_partial)
{
    uint8_t temperature;
    int i;

    temperature = EPD_BWY_350_power_on_and_read_temp();

    // Use the controller's native OTP geometry and waveform selection.
    EPD_WriteCmd(0x00);
    EPD_WriteData(SCAN_DIRECTION);

    // The panel's OTP contains its temperature-compensated three-color
    // waveform. PTIN/PTL only restrict the driven area; they do not select a
    // fast waveform. GxEPD2 likewise uses the normal slow OTP refresh for
    // UC8151 colour panels. Its experimental black/white LUT needs valid old
    // and new frame buffers, which are lost when this firmware resets and
    // power-cycles the controller before every update. Do not select that LUT
    // without adding persistent previous-frame storage and panel-specific
    // waveform validation: using it as an ordinary colour partial refresh
    // causes the observed red/black contamination.
    (void)full_or_partial;
    EPD_WriteCmd(0x10);
    for (i = 0; i < size; i++)
        EPD_WriteData(0x00);

    EPD_WriteCmd(0x13);
    for (i = 0; i < size; i++)
        EPD_WriteData(0x00);

    // Load the black/white image. The red plane is optional.
    EPD_LoadImage(image, size, 0x10);
    if (red_image)
        EPD_LoadImage(red_image, size, 0x13);

    EPD_WriteCmd(0x12);
    return temperature;
}

_attribute_ram_code_ void EPD_BWY_350_set_sleep(void)
{
    EPD_WriteCmd(0x02);
    EPD_WriteCmd(0x07);
    EPD_WriteData(0xa5);
}
