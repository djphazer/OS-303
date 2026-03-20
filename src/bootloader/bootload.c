/*
 * Minimal Serial MIDI SysEx Bootloader
 * Target: AT90USB1286
 * Clock : 16 MHz
 */

// #define F_CPU 16000000UL

#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>

#define APP_ADDRESS 0x0000
#define PAGE_SIZE SPM_PAGESIZE
#define SYSEX_MAX (PAGE_SIZE*2)

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

/* returns checksum of decoded bytes */
static uint8_t decode_7bit(uint8_t *in, uint16_t len, uint8_t *out) {
  uint16_t i = 0, o = 0;
  uint8_t check = 0;

  while (i < len) {
    uint8_t msb = in[i++];

    for (uint8_t b = 0; b < 7 && i < len; b++) {
      uint8_t v = in[i++];
      out[o] = v | (((msb >> b) & 1) << 7);
      check ^= out[o++];
      if (o >= PAGE_SIZE)
        return check;
    }
  }
  return check;
}

static void flash_write_page(uint16_t page) {
  uint32_t addr = (uint32_t)page * PAGE_SIZE;

  uint8_t sreg = SREG;
  cli();

  eeprom_busy_wait();

  boot_page_erase(addr);
  boot_spm_busy_wait();

  for (uint16_t i = 0; i < PAGE_SIZE; i += 2) {
    uint16_t w = (uint16_t)(page_buffer[i]) | ((uint16_t)(page_buffer[i + 1]) << 8);
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

static uint8_t count = 0;
static void process_sysex(uint8_t *data, uint16_t len) {
  if (len < 2)
    return;

  if (data[0] != 0x7D)
    return;

  uint8_t cmd = data[1];

  if (cmd == 0x01 && len > 9) {
    uint16_t page = ((uint16_t)data[2] << 7) | data[3];
    uint16_t packed_len = ((uint16_t)data[4] << 7) | data[5];
    uint8_t cksum = (data[6] << 4) | data[7];

    if (packed_len + 8 > len)
      return;

    PORTD = (PORTD & 0x0F) | (count++ << 4); // cycle LEDs while writing

    // if they match, cksum will become zero
    cksum ^= decode_7bit(&data[8], packed_len, page_buffer);
    if (cksum) {
      // uh-oh bad checksum, slow blink forever
      while (1) {
        PORTD ^= 0xF0;
        _delay_ms(200);
      }
    }
    flash_write_page(page);
  } else if (cmd == 0x02) {
    jump_to_app();
  }
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
  wdt_disable();
  DDRF = 0xFF; // select pin outputs
  DDRB = 0x00; // button inputs
  DDRD |= 0xF0; // direct LED outputs (but also the MIDI serial lines)
  uart_init();

  // check for button combo to stop the jump
  PORTF = 0x00;
  PORTF = 0x0F;
  _delay_ms(40);
  if (PINB & (1 << 1)) { // hold WRITE/NEXT/TAP to stay in bootloader
    // indicate bootloader mode: TIME, PITCH, FUNCTION, and A# key LEDS, all on
    PORTD |= 0xF0;
    // flash each LED twice for sanity check
    for (uint8_t i = 0; i < 4; ++i) {
      _delay_ms(100);
      PORTD ^= (1 << (4 + i));
      _delay_ms(100);
      PORTD ^= (1 << (4 + i));
      _delay_ms(100);
      PORTD ^= (1 << (4 + i));
      _delay_ms(100);
      PORTD ^= (1 << (4 + i));
    }

    while (1) {
      midi_task();
    }
  }

  jump_to_app();
}
