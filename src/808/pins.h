// Copyright (c) 2026, Nicholas J. Michalek
//
// MIT License
//
// these symbols should help make sense of the TR-808 CPU pinouts


/*
 * The PH pins are used to select which buttons/LEDs to engage using PG, PA, and PB.
 *
 * PA are receiving status info, switch positions, a few buttons.
 * PB are switched inputs for the buttons in the switch board.
 * PG are switched outputs for the LEDs.
 * PC are for original CMOS memory, unused.
 *
 * PD, PE, and PF are all the trigger outputs.
 *
 * PI1 is ???
 * PI2 is ???
 */

#pragma once
#include <Arduino.h>

// pinout with Teensy++ 2.0 fitted
// - enum symbol names correspond to the D650C CPU pins
// - comments indicate Teensy Port designations
enum TppPinout : uint8_t {
  MIDI_IN_PIN = 2, // PD2
  MIDI_OUT_PIN = 3, // PD3

  // Port C - CMOS memory addressing, unused
  PC0_PIN = 4, // PD4
  PC1_PIN = 5, // PD5
  PC2_PIN = 6, // PD6
  PC3_PIN = 7, // PD7

  // Port I - ???
  PI1_PIN = 8, // PE0 - memory pin?
  PI2_PIN = 9, // PE1 - trigger-pulse for all instruments

  // Port D, E & F - drum select pins (trigger with PI2)
  PD0_PIN = 10, // PC0 - closed hat
  PD1_PIN = 11, // PC1 - open hat
  PD2_PIN = 12, // PC2 - cymbal
  PD3_PIN = 13, // PC3 - cowbell

  PE0_PIN = 18, // PE6 - clap - also 1st/2nd part LEDs
  PE1_PIN = 19, // PE7 - rimshot - also A/B LEDs
  PE2_PIN = 0, // PD0 - hi tom
  PE3_PIN = 1, // PD1 - mid tom

  PF0_PIN = 14, // PC4 - low tom
  PF1_PIN = 15, // PC5 - snare drum
  PF2_PIN = 16, // PC6 - bass drum (KICK!)
  PF3_PIN = 17, // PC7 - Accent

  // Port B - Switch board INPUTS (buttons)
  PB3_PIN = 27, // PB7
  PB2_PIN = 26, // PB6
  PB1_PIN = 25, // PB5
  PB0_PIN = 24, // PB4

  // Port A - extra switched inputs to STATUS (TEMPO CLOCK, START/STOP, TAP)
  PA3_PIN = 23, // PB3
  PA2_PIN = 22, // PB2
  PA1_PIN = 21, // PB1
  PA0_PIN = 20, // PB0

  // Port H - mux selectors for PG, PA, and PB
  PH0_PIN = 38, // PF0
  PH1_PIN = 39, // PF1
  PH2_PIN = 40, // PF2
  PH3_PIN = 41, // PF3

  // Port G - drive signals to switch board LEDs
  PG0_PIN = 42, // PF4 - [1], [5], [9], [13]
  PG1_PIN = 43, // PF5 - [2], [6], [10], [14]
  PG2_PIN = 44, // PF6 - [3], [7], [11], [15]
  PG3_PIN = 45, // PF7 - [4], [8], [12], [16]
  
  // ALIASES
  AC_PIN = PF3_PIN,
  BD_PIN = PF2_PIN,
  SD_PIN = PF1_PIN,
  LT_PIN = PF0_PIN,
  MT_PIN = PE3_PIN,
  HT_PIN = PE2_PIN,
  RS_PIN = PE1_PIN,
  CP_PIN = PE0_PIN,
  CB_PIN = PD3_PIN,
  CY_PIN = PD2_PIN,
  OH_PIN = PD1_PIN,
  CH_PIN = PD0_PIN,

  TRIG_PIN = PI2_PIN,
  PART_LED_PIN = PE0_PIN,
  AB_LED_PIN = PE1_PIN,
};
// in order
const uint8_t inst_pins[12] = {
  AC_PIN, BD_PIN, SD_PIN, LT_PIN,
  MT_PIN, HT_PIN, RS_PIN, CP_PIN,
  CB_PIN, CY_PIN, OH_PIN, CH_PIN,
};

const uint8_t INPUTS[] = {
  // Teensy Port B
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
const uint8_t OUTPUTS[] = {
  // Teensy Port D
  PC0_PIN, PC1_PIN, PC2_PIN, PC3_PIN,
  PE2_PIN, PE3_PIN,

  // Teensy Port E
  PE0_PIN, PE1_PIN,
  PI1_PIN, PI2_PIN,

  // Teensy Port C
  PD0_PIN, PD1_PIN, PD2_PIN, PD3_PIN,
  PF0_PIN, PF1_PIN, PF2_PIN, PF3_PIN,
  // Teensy Port F
  PG0_PIN, PG1_PIN, PG2_PIN, PG3_PIN,
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,

};

// switched inputs, polled sequentially
enum InputIndex : uint8_t {
  // -- Switch Board matrix
  STEP1_KEY, // 0 - PB0 with PH0 low
  STEP2_KEY,
  STEP3_KEY,
  STEP4_KEY, // 3 - PB3 with PH0 low

  STEP5_KEY, // 4 - PB0 with PH1 low
  STEP6_KEY,
  STEP7_KEY,
  STEP8_KEY, // 7 - PB3 with PH1 low

  STEP9_KEY, // 8 - PB0 + PH2 low
  STEP10_KEY,
  STEP11_KEY,
  STEP12_KEY, // 11 - PB3 + PH2 low

  STEP13_KEY, // 12 - PB0 with PH3 low
  STEP14_KEY,
  STEP15_KEY,
  STEP16_KEY, // 15 - PB3 with PH3 low

  // -- muxed pins outside the switch matrix
  MODE_BIT0, // 16 - PA0 with PH0 low
  MODE_BIT1,
  MODE_BIT2,
  CLEAR_KEY, // 19 - PA3 with PH0 low

  PRESCALE_BIT0, // 20 - PA0 with PH1 low
  PRESCALE_BIT1, // 21 - PA1
  ABVAR_BIT0, // 22 - PA2
  ABVAR_BIT1, // 23 PA3 with PH1 low

  INST_SEL_BIT0, // 24 - PA0 with PH2 low
  INST_SEL_BIT1,
  INST_SEL_BIT2,
  INST_SEL_BIT3, // 27 - PA3 with PH2 low

  AUTOFILL_BIT0, // 28 - PA0 w/ PH3 low, unused
  AUTOFILL_BIT1,
  AUTOFILL_BIT2,
  IFVARIATION_B_SWITCH, // 31 - PA3 w/ PH3 low

  // Extra status pins - read with PH0-PH3 all held high
  RUN, // PA0
  TAP_FILL_IN, // should be same as TAP_NEXT in 303/606 ?
  RESET_START,
  CLOCK, // PA4

  // I don't think these 4 actually do anything... PB0-PB3
  //PBUTTON0, PBUTTON1, PBUTTON2, PBUTTON3,

  INPUT_COUNT,
  EXTRA_PIN_OFFSET = RUN,
};

// 4x4x2 + 4 extra status bits
static_assert(INPUT_COUNT == 36, "missing/extra input index...");

//
// --- byte codes for switch positions ---
//
enum PrescaleCode : uint8_t {
  PSCODE_1 = 0x3,
  PSCODE_2 = 0x1,
  PSCODE_3 = 0x2,
  PSCODE_4 = 0x0,
};
enum AB_SwitchCode : uint8_t {
  VAR_A  = 0x0,
  VAR_AB = 0x2,
  VAR_B  = 0x1,
};
enum InstSelectCode : uint8_t {
  // basically in reverse order, 11 to 0
  CH_CODE = 0b0000,
  OH_CODE = 0b0001,
  CY_CODE = 0b0010,
  CB_CODE = 0b0011,
  CP_CODE = 0b0100,
  RS_CODE = 0b0101,
  HT_CODE = 0b0110,
  MT_CODE = 0b0111,
  LT_CODE = 0b1000,
  SD_CODE = 0b1001,
  BD_CODE = 0b1010,
  AC_CODE = 0b1011,
};
// in correct order
static const uint8_t inst_codes[] = {
  AC_CODE, BD_CODE, SD_CODE, LT_CODE,
  MT_CODE, HT_CODE, RS_CODE, CP_CODE,
  CB_CODE, CY_CODE, OH_CODE, CH_CODE,
};
enum AutoFillCode : uint8_t {
  MANUAL_CODE = 0b000,
  FILL2_CODE  = 0b001,
  FILL4_CODE  = 0b010,
  FILL8_CODE  = 0b011,
  FILL12_CODE = 0b100,
  FILL16_CODE = 0b101,
};
enum ModeSwitchCode : uint8_t {
  PATCLR_CODE  = 0b101,
  PART1_CODE   = 0b110,
  PART2_CODE   = 0b111,
  MANPLAY_CODE = 0b001,
  PLAY_CODE    = 0b011,
  COMPOSE_CODE = 0b000, //todo: needs hardware mod
};

//
// --- useful pin correlations ---
//
const uint8_t select_pin[4] = {
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,
};
const uint8_t button_pins[4] = {
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
const uint8_t status_pins[4] = {
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
};

// The PG and PH pins are all part of PORTF on the Teensy, which can simply be written as one byte.
// (I don't think this works right tho?)
const uint8_t led_bytes[16] = {
  // PG  PH
  0b00011110,
  0b00101110,
  0b01001110,
  0b10001110,

  0b00011101,
  0b00101101,
  0b01001101,
  0b10001101,

  0b00011011,
  0b00101011,
  0b01001011,
  0b10001011,

  0b00010111,
  0b00100111,
  0b01000111,
  0b10000111,
};

// MIDI notes per instrument; follows General MIDI drum mapping
static const uint8_t INST_NOTE[] = {
  0,    // AC - unused, derived from velocity
  36,   // BD  Bass Drum 1
  38,   // SD  Acoustic Snare
  41,   // LT  Low Floor Tom
  45,   // MT  Low Tom
  48,   // HT  Hi-Mid Tom
  37,   // RS  Side Stick
  39,   // CP  Hand Clap
  56,   // CB  Cowbell
  49,   // CY  Crash Cymbal 1
  46,   // OH  Open Hi-Hat
  42,   // CH  Closed Hi-Hat
};
