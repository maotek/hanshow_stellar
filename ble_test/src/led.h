#pragma once

/* RxTx command: [LED_CMD_SET_MASK, mask], R=bit0, G=bit1, B=bit2. */
#define LED_CMD_SET_MASK 0xE3

void init_led(void);
void set_led_color(uint8_t color);
void set_led_mask(uint8_t mask);
uint8_t get_led_mask(void);
