/*
 * Minimal Serial MIDI SysEx Bootloader
 * Target: AT90USB1286
 * Clock : 4 MHz
 */

#ifndef F_CPU
#define F_CPU 4000000UL
#endif

#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>

#define APP_ADDRESS 0x0000
#define PAGE_SIZE SPM_PAGESIZE
#define SYSEX_MAX (PAGE_SIZE*2)
#define MY_UBRR ((F_CPU / (16UL * 31250UL)) - 1UL)
#define BOOT_MAGIC 0xB7

static uint8_t page_buffer[PAGE_SIZE];
static uint8_t sysex_buf[SYSEX_MAX];

static volatile uint16_t sysex_index = 0;
static volatile uint8_t in_sysex = 0;

static void uart_init(void) {
  UBRR1H = (uint8_t)(MY_UBRR >> 8);
  UBRR1L = (uint8_t)(MY_UBRR);

  UCSR1A = 0;
  UCSR1B = (1 << RXEN1);                  // RX only
  UCSR1C = (1 << UCSZ11) | (1 << UCSZ10); // 8N1
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
  if (pgm_read_word(APP_ADDRESS) != 0xFFFF) {
    ((void (*)(void))APP_ADDRESS)();
  }
  // otherwise, execution continues...
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

    PORTF = 0x0E | (count++ << 4); // cycle LEDs while writing

    // if they match, cksum will become zero
    cksum ^= decode_7bit(&data[8], packed_len, page_buffer);
    if (cksum) {
      // uh-oh bad checksum, slow blink forever
      while (1) {
        PORTF ^= 0xF0;
        _delay_ms(200);
      }
    }
    flash_write_page(page);
  } else if (cmd == 0x02) {
    jump_to_app();
  }
}

static void handle_midi(uint8_t b) {
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

static void reflash_mode(void) {
  // poll serial MIDI forever
  while (1) {
    if (UCSR1A & (1 << RXC1))
      handle_midi(UDR1);
  }
}

static void hello(void) {
  // indicate bootloader mode:
  // walk steps 1-4 twice, then hold step 1 solid
  for (uint8_t i = 0; i < 8; ++i) {
    PORTF = 0x0E | (1 << (4 + (i & 0x3)));
    _delay_ms(100);
  }
  PORTF = 0x1E; // step 1 solid
}

int main(void) {
  GPIOR1 = MCUSR; // stash WDRF for app
  MCUSR = 0; // clear WDRF
  wdt_disable();

  // -nostartfiles: .bss is never zeroed, so parser state is set explicitly
  // (this also covers entry by jump from the app, where RAM is app leftovers)
  sysex_index = 0;
  in_sysex = 0;
  count = 0;

  DDRF = 0xFF; // pin outputs for switch matrix
  DDRB = 0x00; // button inputs
  uart_init();

  const uint8_t magic = (BOOT_MAGIC == GPIOR0); // flag from app?
  PORTF = 0x00; // reset all
  PORTF = 0x0F; // select pins off (HIGH)
  if (magic) reflash_mode(); // let's get to the point

  // Setup CPU clock divider relative to 16 MHz crystal
  // This is already configured by the app for 4 MHz, but a cold boot still needs it.
  CLKPR = (1 << CLKPCE); // Enable change sequence
  CLKPR = (1 << CLKPS1); // Division by 4 factor

  _delay_ms(40); // idk how long this needs to be?

  // check for button combo to stop the jump
  if ((PINB & (1 << 1))) { // hold WRITE/NEXT/TAP to stay in bootloader
    hello();
    reflash_mode();
  }

  jump_to_app();
  hello();
  reflash_mode(); // in case there is no app
}
