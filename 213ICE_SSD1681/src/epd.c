#include <stdint.h>
#include "etime.h"
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213_ice.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "battery.h"

#include "OneBitDisplay.h"
#include "TIFF_G4.h"
extern const uint8_t ucMirror[];
#include "font_60.h"
#include "font16.h"
#include "font16zh.h"
#include "font30.h"

RAM uint8_t epd_update_state = 0;

RAM uint8_t epd_scene = 1;
RAM uint8_t epd_wait_update = 0;

RAM uint8_t hour_refresh = 100;
RAM uint8_t minute_refresh = 100;

RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

RAM uint8_t epd_buffer[epd_buffer_size];
RAM uint8_t epd_temp[epd_buffer_size]; // for OneBitDisplay to draw into
OBDISP obd;                            // virtual display structure
TIFFIMAGE tiff;

// With this we can force a display if it wasnt detected correctly
void set_EPD_scene(uint8_t scene)
{
    epd_scene = scene;
    set_EPD_wait_flush();
}

void set_EPD_wait_flush()
{
    epd_wait_update = 1;
}

_attribute_ram_code_ uint8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read)
        return epd_temperature;

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    epd_temperature = EPD_BW_213_ICE_read_temp();

    EPD_POWER_OFF();

    epd_temperature_is_read = 1;

    return epd_temperature;
}

_attribute_ram_code_ void EPD_Display(unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial)
{
    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    (void)red_image;
    epd_temperature = EPD_BW_213_ICE_Display(image, size, full_or_partial);

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    EPD_BW_213_ICE_set_sleep();

    EPD_POWER_OFF();
    epd_update_state = 0;
}

_attribute_ram_code_ uint8_t epd_state_handler(void)
{
    switch (epd_update_state)
    {
    case 0:
        // Nothing todo
        break;
    case 1: // check if refresh is done and sleep epd if so
        if (EPD_IS_BUSY())
            epd_set_sleep();
        break;
    }
    return epd_update_state;
}

_attribute_ram_code_ void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height)
{
    int x, y;
    uint8_t *s, *d;
    for (y = 0; y < (height / 8); y++)
    { // byte rows
        d = &pDst[y];
        s = &pSrc[y * width];
        for (x = 0; x < width; x++)
        {
            d[x * (height / 8)] = ~ucMirror[s[width - 1 - x]]; // invert and flip
        } // for x
    } // for y
}

_attribute_ram_code_ void TIFFDraw(TIFFDRAW *pDraw)
{
    uint8_t uc = 0, ucSrcMask, ucDstMask, *s, *d;
    int x, y;

    s = pDraw->pPixels;
    y = pDraw->y;
    d = &epd_buffer[((epd_width - 1) * epd_height_bytes) + (y / 8)];
    ucDstMask = 0x80 >> (y & 7);
    ucSrcMask = 0;
    for (x = 0; x < pDraw->iWidth; x++)
    {
        // Slower to draw this way, but it allows us to use a single buffer
        // instead of drawing and then converting the pixels to be the EPD format
        if (ucSrcMask == 0)
        { // load next source byte
            ucSrcMask = 0x80;
            uc = *s++;
        }
        if (!(uc & ucSrcMask))
        { // black pixel
            d[-(x * epd_height_bytes)] &= ~ucDstMask;
        }
        ucSrcMask >>= 1;
    }
}

_attribute_ram_code_ void epd_display_tiff(uint8_t *pData, int iSize)
{
    // test G4 decoder
    epd_clear();
    TIFF_openRAW(&tiff, epd_width, epd_height, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, epd_width, epd_height, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);
}

extern uint8_t mac_public[6];

static void draw_watch_digit(OBDISP *display, int x, int y, uint8_t digit)
{
    static const uint8_t segments[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f
    };
    uint8_t mask = segments[digit % 10];

    // Segment order: top, upper-right, lower-right, bottom,
    // lower-left, upper-left, middle.
    if (mask & 0x01) obdRectangle(display, x + 4, y,      x + 19, y + 3,  1, 1);
    if (mask & 0x02) obdRectangle(display, x + 20, y + 3,  x + 23, y + 21, 1, 1);
    if (mask & 0x04) obdRectangle(display, x + 20, y + 27, x + 23, y + 45, 1, 1);
    if (mask & 0x08) obdRectangle(display, x + 4, y + 45, x + 19, y + 48, 1, 1);
    if (mask & 0x10) obdRectangle(display, x,     y + 27, x + 3,  y + 45, 1, 1);
    if (mask & 0x20) obdRectangle(display, x,     y + 3,  x + 3,  y + 21, 1, 1);
    if (mask & 0x40) obdRectangle(display, x + 4, y + 22, x + 19, y + 26, 1, 1);
}

_attribute_ram_code_ void epd_display(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    int i;
    uint8_t battery_level;
    uint8_t battery_width;
    if (epd_update_state)
        return;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0);

    char buff[100];
    battery_level = get_battery_level(battery_mv);

    // Compact header leaves a comfortable margin on the 2.13-inch panel.
    sprintf(buff, "MAOWATCH ICE SSD1681");
    // obdWriteString uses 8-pixel text rows, not pixel Y coordinates.
    obdWriteString(&obd, 0, 26, 1, (char *)buff, FONT_8x8, 0, 1);

    sprintf(buff, "%02d.%02d.%04d  %02X%02X%02X%02X%02X%02X",
            _time.tm_day, _time.tm_month, _time.tm_year,
            mac_public[5], mac_public[4], mac_public[3],
            mac_public[2], mac_public[1], mac_public[0]);
    obdWriteString(&obd, 0, 10, 2, (char *)buff, FONT_8x8, 0, 1);

    obdRectangle(&obd, 6, 26, 205, 27, 1, 1);

    // Smaller seven-segment numerals keep the clock clear of the panel edges.
    draw_watch_digit(&obd, 31, 29, _time.tm_hour / 10);
    draw_watch_digit(&obd, 60, 29, _time.tm_hour % 10);
    obdRectangle(&obd, 102, 41, 106, 45, 1, 1);
    obdRectangle(&obd, 102, 59, 106, 63, 1, 1);
    draw_watch_digit(&obd, 124, 29, _time.tm_min / 10);
    draw_watch_digit(&obd, 153, 29, _time.tm_min % 10);

    // Battery and temperature have separate halves of the status row.
    obdRectangle(&obd, 6, 80, 205, 81, 1, 1);
    obdRectangle(&obd, 12, 87, 39, 102, 1, 0);
    obdRectangle(&obd, 9, 91, 12, 98, 1, 1);
    battery_width = (battery_level * 24) / 100;
    if (battery_width)
        obdRectangle(&obd, 14, 89, 13 + battery_width, 100, 1, 1);

    sprintf(buff, "%d%%", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         45, 102, (char *)buff, 1);

    // Bluetooth state is hidden for now to keep battery and temperature centered.
    // obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
    //                      158, 163, ble_get_connected() ? "BT" : "OFF", 1);

    sprintf(buff, "%d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         166, 102, (char *)buff, 1);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    // Keep the known-good drawing path and invert only the controller-ready
    // monochrome plane. The separate red plane remains clear in the driver.
    for (i = 0; i < epd_buffer_size; i++)
        epd_buffer[i] ^= 0xff;

    EPD_Display(epd_buffer, NULL, epd_buffer_size, full_or_partial);
}

_attribute_ram_code_ void epd_display_char(uint8_t data)
{
    int i;
    for (i = 0; i < epd_buffer_size; i++)
    {
        epd_buffer[i] = data;
    }
    EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0x00, epd_buffer_size);
    memset(epd_temp, 0x00, epd_buffer_size);
}
void update_time_scene(struct date_time _time, uint16_t battery_mv, int16_t temperature, void (*scene)(struct date_time, uint16_t, int16_t, uint8_t))
{
    // default scene: show default time, battery, ble address, temperature
    if (epd_update_state)
        return;

    if (epd_wait_update)
    {
        scene(_time, battery_mv, temperature, 1);
        epd_wait_update = 0;
    }

    else if (_time.tm_min != minute_refresh)
    {
        minute_refresh = _time.tm_min;
        if (_time.tm_hour != hour_refresh)
        {
            hour_refresh = _time.tm_hour;
            scene(_time, battery_mv, temperature, 1);
        }
        else
        {
            scene(_time, battery_mv, temperature, 0);
        }
    }
}

void epd_update(struct date_time _time, uint16_t battery_mv, int16_t temperature)
{
    switch (epd_scene)
    {
    case 1:
        update_time_scene(_time, battery_mv, temperature, epd_display);
        break;
    case 2:
        update_time_scene(_time, battery_mv, temperature, epd_display_time_with_date);
        break;
    default:
        break;
    }
}

void epd_display_time_with_date(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    /* The old alternate scene was laid out for a much taller panel. */
    epd_display(_time, battery_mv, temperature, full_or_partial);
}
