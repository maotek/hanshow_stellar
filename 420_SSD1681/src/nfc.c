#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "nfc.h"
#include "main.h"

/*
 * FM11NC08I: ISO 14443-4 / NFC Forum Type 4 Tag over the I2C FIFO.
 * The Telink I2C driver uses the 8-bit address, including the R/W bit.
 */
#define FM11_I2C_ID            0xae
#define FM11_FIFO              0xfff0
#define FM11_FIFO_FLUSH        0xfff1
#define FM11_FIFO_WORD_COUNT   0xfff2
#define FM11_RF_TX_ENABLE      0xfff4
#define FM11_MAIN_IRQ          0xfff7

#define FM11_IRQ_RX_DONE       0x10
#define FM11_MAX_FRAME         64

typedef enum {
    NFC_FILE_NONE,
    NFC_FILE_CC,
    NFC_FILE_NDEF
} nfc_file_t;

static nfc_file_t selected_file;

/* NFC Forum Type 4 capability container: one read-only NDEF file, E104. */
static const uint8_t capability_container[] = {
    0x00, 0x0f, 0x20, 0x00, 0x3b, 0x00, 0x34,
    0x04, 0x06, 0xe1, 0x04, 0x00, 0x40, 0x00, 0xff
};

/* NLEN + one UTF-8 Text record containing "MAOWATCH". */
static const uint8_t ndef_file[] = {
    0x00, 0x0f,
    0xd1, 0x01, 0x0b, 0x54, 0x02, 0x65, 0x6e,
    0x4d, 0x41, 0x4f, 0x57, 0x41, 0x54, 0x43, 0x48
};

static void fm11_select(void)
{
    gpio_write(NFC_CS, 0);
    sleep_us(100);
    i2c_set_id(FM11_I2C_ID);
}

static void fm11_release(void)
{
    gpio_write(NFC_CS, 1);
}

static uint8_t fm11_read_reg(uint16_t address)
{
    uint8_t value = 0;
    i2c_set_id(FM11_I2C_ID);
    i2c_read_series(address, 2, &value, 1);
    return value;
}

static void fm11_write(uint16_t address, const uint8_t *data, uint8_t length)
{
    i2c_set_id(FM11_I2C_ID);
    i2c_write_series(address, 2, (uint8_t *)data, length);
}

static void fm11_write_reg(uint16_t address, uint8_t value)
{
    fm11_write(address, &value, 1);
}

static void fm11_send(const uint8_t *data, uint8_t length)
{
    fm11_write(FM11_FIFO, data, length);
    fm11_write_reg(FM11_RF_TX_ENABLE, 0x55);
}

static void fm11_send_status(uint8_t pcb, uint8_t sw1, uint8_t sw2)
{
    uint8_t response[3] = {pcb, sw1, sw2};
    fm11_send(response, sizeof(response));
}

static void fm11_select_file(const uint8_t *frame, uint8_t length)
{
    if (length >= 8 && frame[3] == 0x00 && frame[5] == 2) {
        if (frame[6] == 0xe1 && frame[7] == 0x03) {
            selected_file = NFC_FILE_CC;
            fm11_send_status(frame[0], 0x90, 0x00);
            return;
        }
        if (frame[6] == 0xe1 && frame[7] == 0x04) {
            selected_file = NFC_FILE_NDEF;
            fm11_send_status(frame[0], 0x90, 0x00);
            return;
        }
    }

    /* Selecting the NFC Forum NDEF application is accepted here. */
    if (length >= 6 && frame[3] == 0x04) {
        selected_file = NFC_FILE_NONE;
        fm11_send_status(frame[0], 0x90, 0x00);
        return;
    }

    selected_file = NFC_FILE_NONE;
    fm11_send_status(frame[0], 0x6a, 0x82);
}

static void fm11_read_binary(const uint8_t *frame, uint8_t length)
{
    uint8_t response[FM11_MAX_FRAME];
    const uint8_t *file;
    uint16_t file_length;
    uint16_t offset;
    uint8_t requested;

    if (length < 6) {
        fm11_send_status(frame[0], 0x67, 0x00);
        return;
    }

    if (selected_file == NFC_FILE_CC) {
        file = capability_container;
        file_length = sizeof(capability_container);
    } else if (selected_file == NFC_FILE_NDEF) {
        file = ndef_file;
        file_length = sizeof(ndef_file);
    } else {
        fm11_send_status(frame[0], 0x69, 0x86);
        return;
    }

    offset = ((uint16_t)frame[3] << 8) | frame[4];
    requested = frame[5];
    if (offset >= file_length || requested > FM11_MAX_FRAME - 3 ||
        (uint16_t)(offset + requested) > file_length) {
        fm11_send_status(frame[0], 0x6b, 0x00);
        return;
    }

    response[0] = frame[0];
    memcpy(&response[1], &file[offset], requested);
    response[requested + 1] = 0x90;
    response[requested + 2] = 0x00;
    fm11_send(response, requested + 3);
}

static void fm11_process_frame(uint8_t *frame, uint8_t length)
{
    if (length < 3) {
        return;
    }

    switch (frame[2]) {
    case 0xa4: /* SELECT */
        fm11_select_file(frame, length);
        break;
    case 0xb0: /* READ BINARY */
        fm11_read_binary(frame, length);
        break;
    default:
        fm11_send_status(frame[0], 0x6d, 0x00);
        break;
    }
}

_attribute_ram_code_ void init_nfc(void)
{
    uint8_t value;

    gpio_write(NFC_CS, 1);
    gpio_set_func(NFC_CS, AS_GPIO);
    gpio_set_output_en(NFC_CS, 1);
    gpio_set_input_en(NFC_CS, 0);
    gpio_setup_up_down_resistor(NFC_CS, PM_PIN_PULLUP_10K);

    gpio_set_func(NFC_IRQ, AS_GPIO);
    gpio_set_output_en(NFC_IRQ, 0);
    gpio_set_input_en(NFC_IRQ, 1);
    gpio_setup_up_down_resistor(NFC_IRQ, PM_PIN_PULLUP_10K);

    fm11_select();

    /*
     * Hanshow's FM11NC08I is configured through 03B5.  Keep the board's
     * original 0xA0 mode byte; 0x90 is used by a different reference board
     * and prevents this antenna/tag configuration from enumerating reliably.
     */
    value = fm11_read_reg(0x03b5);
    if (value != 0xa0) {
        fm11_write_reg(0x03b5, 0xa0);
        sleep_us(10000);
    }

    fm11_write_reg(FM11_FIFO_FLUSH, 0xff);
    selected_file = NFC_FILE_NONE;
    fm11_release();

    cpu_set_gpio_wakeup(NFC_IRQ, 0, 1);
}

_attribute_ram_code_ void nfc_handler(void)
{
    uint8_t frame[FM11_MAX_FRAME];
    uint8_t irq;
    uint8_t count;

    if (gpio_read(NFC_IRQ)) {
        return;
    }

    fm11_select();
    irq = fm11_read_reg(FM11_MAIN_IRQ);

    if (irq & FM11_IRQ_RX_DONE) {
        count = fm11_read_reg(FM11_FIFO_WORD_COUNT) & 0x3f;
        if (count > 2 && count <= FM11_MAX_FRAME) {
            i2c_set_id(FM11_I2C_ID);
            i2c_read_series(FM11_FIFO, 2, frame, count);
            /* The FM11NC08 FIFO appends the two RF CRC bytes. */
            fm11_process_frame(frame, count - 2);
        } else {
            fm11_write_reg(FM11_FIFO_FLUSH, 0xff);
        }
    }

    fm11_release();
}
