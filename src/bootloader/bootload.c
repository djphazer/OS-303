/*
 * Minimal Serial MIDI SysEx Bootloader
 * Target: AT90USB1286
 * Clock : 16 MHz
 */

// #define F_CPU 16000000UL

#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#include "sync.h"

#define APP_ADDRESS 0x0000
#define PAGE_SIZE 256
#define SYSEX_MAX 400

static uint8_t page_buffer[PAGE_SIZE];
static uint8_t sysex_buf[SYSEX_MAX];

static volatile uint16_t sysex_index = 0;
static volatile uint8_t in_sysex = 0;

static void uart_init(void) {
  // 31250 baud @ 16 MHz → UBRR = 31
  UBRR1H = 0;
  UBRR1L = 31;

  UCSR1A = 0;
  UCSR1B = (1 << RXEN1);                  // RX only
  UCSR1C = (1 << UCSZ11) | (1 << UCSZ10); // 8N1
}

static uint8_t uart_rx(void) {
  while (!(UCSR1A & (1 << RXC1)))
    ;
  return UDR1;
}

static void decode_7bit(uint8_t *in, uint16_t len, uint8_t *out) {
  uint16_t i = 0, o = 0;

  while (i < len) {
    uint8_t msb = in[i++];

    for (uint8_t b = 0; b < 7 && i < len; b++) {
      uint8_t v = in[i++];
      out[o++] = v | (((msb >> b) & 1) << 7);
      if (o >= PAGE_SIZE)
        return;
    }
  }
}

static void flash_write_page(uint16_t page) {
  uint32_t addr = (uint32_t)page * PAGE_SIZE;

  uint8_t sreg = SREG;
  cli();

  eeprom_busy_wait();

  boot_page_erase(addr);
  boot_spm_busy_wait();

  for (uint16_t i = 0; i < PAGE_SIZE; i += 2) {
    uint16_t w = page_buffer[i] | (page_buffer[i + 1] << 8);
    boot_page_fill(addr + i, w);
  }

  boot_page_write(addr);
  boot_spm_busy_wait();
  boot_rww_enable();

  SREG = sreg;
}

static void jump_to_app(void) {
  cli();

  // If application not blank
  if (*(uint16_t *)APP_ADDRESS != 0xFFFF) {
    ((void (*)(void))APP_ADDRESS)();
  }

  // Otherwise stay here forever?
  //while (1) ;
  // nah, we'll just dump back into the updater
}

static void process_sysex(uint8_t *data, uint16_t len) {
  if (len < 2)
    return;

  if (data[0] != 0x7D)
    return;

  uint8_t cmd = data[1];

  if (cmd == 0x01 && len > 6) {
    uint16_t page = ((uint16_t)data[2] << 8) | data[3];
    uint8_t packed_len = data[4];

    if (packed_len + 5 > len)
      return;

    PORTD &= 0x0F; // turn LEDs off while writing
    decode_7bit(&data[5], packed_len, page_buffer);
    flash_write_page(page);
  } else if (cmd == 0x02) {
    jump_to_app();
  }
  PORTD |= 0xF0; // turn LEDs back on
}

static void midi_task(void) {
  uint8_t b = uart_rx();

  if (b == 0xF0) {
    in_sysex = 1;
    sysex_index = 0;
    return;
  }

  if (!in_sysex)
    return;

  if (b == 0xF7) {
    process_sysex(sysex_buf, sysex_index);
    in_sysex = 0;
    return;
  }

  if (b < 0x80 && sysex_index < SYSEX_MAX)
    sysex_buf[sysex_index++] = b;
}

int main(void) {
  DDRF = 0xFF; // select pin outputs
  DDRB = 0x00; // button inputs
  DDRD |= 0xF0; // direct LED outputs (but also the MIDI serial lines)
  uart_init();

  // check for button combo to stop the jump
  PORTF = 0x00;
  PORTF = 0x0F;
  _delay_ms(1);
  boot_sync_flag = 1;
  if (PINB & (1 << 1)) { // hold WRITE/NEXT/TAP to stay in bootloader
    // indicate bootloader mode: TIME, PITCH, FUNCTION, and A# key LEDS, all on
    PORTD |= 0xF0;
    while (1) {
      midi_task();
    }
  }

  jump_to_app();
}
