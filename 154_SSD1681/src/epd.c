#include <stdint.h>
#include "etime.h"
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwr_154.h"
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

    epd_temperature = EPD_BWR_154_read_temp();

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

    epd_temperature = EPD_BWR_154_Display(image, red_image, size, full_or_partial);

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    EPD_BWR_154_set_sleep();

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
    d = &epd_buffer[((epd_width - 1) * (epd_height / 8)) + (y / 8)];
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
            d[-(x * (epd_height / 8))] &= ~ucDstMask;
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
    if (mask & 0x01) obdRectangle(display, x + 6, y,      x + 31, y + 6,  1, 1);
    if (mask & 0x02) obdRectangle(display, x + 32, y + 6,  x + 38, y + 35, 1, 1);
    if (mask & 0x04) obdRectangle(display, x + 32, y + 45, x + 38, y + 74, 1, 1);
    if (mask & 0x08) obdRectangle(display, x + 6, y + 74, x + 31, y + 80, 1, 1);
    if (mask & 0x10) obdRectangle(display, x,     y + 45, x + 6,  y + 74, 1, 1);
    if (mask & 0x20) obdRectangle(display, x,     y + 6,  x + 6,  y + 35, 1, 1);
    if (mask & 0x40) obdRectangle(display, x + 6, y + 36, x + 31, y + 44, 1, 1);
}

_attribute_ram_code_ void epd_display(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    int i;
    uint8_t battery_level;
    uint8_t battery_width;
    static const char *weekday[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };

    if (epd_update_state)
        return;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0);

    char buff[100];
    battery_level = get_battery_level(battery_mv);

    // Quiet header: the calendar gets one clean line.
    sprintf(buff, "%s  %02d.%02d.%04d",
            weekday[_time.tm_week % 7],
            _time.tm_day, _time.tm_month, _time.tm_year);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         18, 19, (char *)buff, 1);
    obdRectangle(&obd, 10, 27, 189, 28, 1, 1);

    // Enlarged seven-segment numerals use the space freed by the debug voltage.
    draw_watch_digit(&obd, 4, 35, _time.tm_hour / 10);
    draw_watch_digit(&obd, 44, 35, _time.tm_hour % 10);
    obdRectangle(&obd, 94, 58, 99, 63, 1, 1);
    obdRectangle(&obd, 94, 86, 99, 91, 1, 1);
    draw_watch_digit(&obd, 109, 35, _time.tm_min / 10);
    draw_watch_digit(&obd, 149, 35, _time.tm_min % 10);

    // Battery and temperature now share the full-width status row.
    obdRectangle(&obd, 10, 124, 189, 125, 1, 1);
    obdRectangle(&obd, 21, 141, 48, 156, 1, 0);
    obdRectangle(&obd, 18, 145, 21, 152, 1, 1);
    battery_width = (battery_level * 24) / 100;
    if (battery_width)
        obdRectangle(&obd, 23, 143, 22 + battery_width, 154, 1, 1);

    sprintf(buff, "%d%%", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         54, 156, (char *)buff, 1);

    sprintf(buff, "%d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         132, 156, (char *)buff, 1);

    // Bluetooth state is hidden for now to keep battery and temperature centered.
    // obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
    //                      158, 163, ble_get_connected() ? "BT" : "OFF", 1);

    sprintf(buff, "MAOWATCH-%02X", mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         43, 187, (char *)buff, 1);

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
    uint16_t battery_level;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[100];
    battery_level = get_battery_level(battery_mv);

    sprintf(buff, "S24_%02X%02X%02X", mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 1, 17, (char *)buff, 1);

    if (ble_get_connected())
    {
        sprintf(buff, "78%s", "234");
    }
    else
    {
        sprintf(buff, "78%s", "56");
    }

    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16_zh, 120, 21, (char *)buff, 1);

    obdRectangle(&obd, 158, 8, 161, 12, 1, 1);
    obdRectangle(&obd, 161, 2, 199, 22, 1, 1);

    sprintf(buff, "%d", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 165, 18, (char *)buff, 0);

    obdRectangle(&obd, 0, 25, 199, 27, 1, 1);

    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 15, 85, (char *)buff, 1);

    sprintf(buff, "   %d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 115, 112, (char *)buff, 1);

    obdRectangle(&obd, 110, 120, 199, 122, 1, 1);

    sprintf(buff, " %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 115, 143, (char *)buff, 1);

    obdRectangle(&obd, 108, 98, 110, 150, 1, 1);
    obdRectangle(&obd, 0, 150, 199, 152, 1, 1);

    sprintf(buff, "%d-%02d-%02d", _time.tm_year, _time.tm_month, _time.tm_day);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 5, 175, (char *)buff, 1);

    if (_time.tm_week == 7)
    {
        sprintf(buff, "9:%c", _time.tm_week + 0x20 + 6);
    }
    else
    {
        sprintf(buff, "9:%c", _time.tm_week + 0x20);
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16_zh, 115, 175, (char *)buff, 1);

    if (_time.tm_hour > 7 && _time.tm_hour < 20)
    {
        sprintf(buff, "%s", "EFGH");
    }
    else
    {
        sprintf(buff, "%s", "ABCD");
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16_zh, 145, 197, (char *)buff, 1);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    EPD_Display(epd_buffer, NULL, epd_width * epd_height / 8, full_or_partial);
}
