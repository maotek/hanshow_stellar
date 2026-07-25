#include <stdint.h>
#include "etime.h"
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_ssd1608.h"
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
RAM uint8_t epd_controller_ready = 0;

RAM uint8_t epd_scene = 1;
RAM uint8_t epd_wait_update = 0;

RAM uint8_t hour_refresh = 100;
RAM uint8_t minute_refresh = 100;

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

_attribute_ram_code_ void EPD_Display(unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial)
{
    uint8_t do_full_refresh;

    EPD_init();

    // A partial update is valid only when the SSD1608 still contains the RAM
    // state established by a preceding refresh.
    do_full_refresh = full_or_partial || !epd_controller_ready;
    if (do_full_refresh)
    {
        EPD_POWER_ON();
        WaitMs(5);
        gpio_write(EPD_RESET, 0);
        WaitMs(10);
        gpio_write(EPD_RESET, 1);
        WaitMs(10);
    }

    (void)red_image;
    EPD_SSD1608_Display(image, size, do_full_refresh);

    epd_controller_ready = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    EPD_SSD1608_set_sleep();

    // Keep the SSD1608 logic supply on so its old/new RAM survives. Only the
    // high-voltage charge pump and oscillator were stopped above.
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
        0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    uint8_t mask = segments[digit % 10];

    // Segment order: top, upper-right, lower-right, bottom,
    // lower-left, upper-left, middle.
    if (mask & 0x01)
        obdRectangle(display, x + 5, y, x + 27, y + 5, 1, 1);
    if (mask & 0x02)
        obdRectangle(display, x + 28, y + 5, x + 33, y + 29, 1, 1);
    if (mask & 0x04)
        obdRectangle(display, x + 28, y + 36, x + 33, y + 60, 1, 1);
    if (mask & 0x08)
        obdRectangle(display, x + 5, y + 60, x + 27, y + 65, 1, 1);
    if (mask & 0x10)
        obdRectangle(display, x, y + 36, x + 5, y + 60, 1, 1);
    if (mask & 0x20)
        obdRectangle(display, x, y + 5, x + 5, y + 29, 1, 1);
    if (mask & 0x40)
        obdRectangle(display, x + 5, y + 30, x + 27, y + 35, 1, 1);
}

_attribute_ram_code_ void epd_display(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    uint8_t battery_level;
    uint8_t battery_width;
    static const char *weekday[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

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
                         18, 22, (char *)buff, 1);
    obdRectangle(&obd, 16, 31, 183, 32, 1, 1);

    // Large custom seven-segment numerals use most of the square display.
    draw_watch_digit(&obd, 13, 48, _time.tm_hour / 10);
    draw_watch_digit(&obd, 51, 48, _time.tm_hour % 10);
    obdRectangle(&obd, 92, 70, 97, 75, 1, 1);
    obdRectangle(&obd, 92, 91, 97, 96, 1, 1);
    draw_watch_digit(&obd, 104, 48, _time.tm_min / 10);
    draw_watch_digit(&obd, 142, 48, _time.tm_min % 10);

    // A restrained status row keeps useful information without crowding.
    obdRectangle(&obd, 16, 132, 183, 133, 1, 1);
    obdRectangle(&obd, 70, 148, 97, 163, 1, 0);
    obdRectangle(&obd, 67, 152, 70, 159, 1, 1);
    battery_width = (battery_level * 24) / 100;
    if (battery_width)
        obdRectangle(&obd, 72, 150, 71 + battery_width, 161, 1, 1);

    sprintf(buff, "%d%%", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         103, 163, (char *)buff, 1);

    // Bluetooth state is hidden for now to keep the battery centered.
    // obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
    //                      158, 163, ble_get_connected() ? "BT" : "OFF", 1);

    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16,
                         55, 193, "MAOWATCH", 1);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);
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

    sprintf(buff, " %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 115, 130, (char *)buff, 1);

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
