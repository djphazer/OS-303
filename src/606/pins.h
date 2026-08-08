// Copyright (c) 2026, Nicholas J. Michalek
//
// MIT License
//
// these symbols should help make sense of the TR-606 CPU pinouts


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

// AT90USB1286 pinout
// - enum symbol names correspond to the D650C CPU pins
// - comments indicate AT90 Port designations
enum AT90Pinout : uint8_t {
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

  // Port D - memory pins, unused
  PD0_PIN = 10, // PC0
  PD1_PIN = 11, // PC1
  PD2_PIN = 12, // PC2
  PD3_PIN = 13, // PC3

  // Port E & F - drum select pins (trigger with PI2)
  PE0_PIN = 18, // PE6 - closed hat; also for the Pattern Group LED II
  PE1_PIN = 19, // PE7 - open hat
  PE2_PIN = 0, // PD0 - cymbal
  PE3_PIN = 1, // PD1 - hi tom

  PF0_PIN = 14, // PC4 - lo tom
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
  HT_PIN = PE3_PIN,
  CY_PIN = PE2_PIN,
  OH_PIN = PE1_PIN,
  CH_PIN = PE0_PIN,

  PATGROUP_LED_PIN = PE0_PIN,

  TRIG_PIN = PI2_PIN,
};
// in order
const uint8_t inst_pins[12] = {
  AC_PIN, BD_PIN, SD_PIN, LT_PIN,
  HT_PIN, CY_PIN, OH_PIN, CH_PIN,
};

const uint8_t INPUTS[] = {
  // AT90 Port B
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
const uint8_t OUTPUTS[] = {
  // AT90 Port D
  PC0_PIN, PC1_PIN, PC2_PIN, PC3_PIN,
  PE2_PIN, PE3_PIN,

  // AT90 Port E
  PE0_PIN, PE1_PIN,
  PI1_PIN, PI2_PIN,

  // AT90 Port C
  PD0_PIN, PD1_PIN, PD2_PIN, PD3_PIN,
  PF0_PIN, PF1_PIN, PF2_PIN, PF3_PIN,
  // AT90 Port F
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
  WRITE_MODE, // 16 - PA0 with PH0 low
  TRACK_MODE,
  DUMMY0,
  DUMMY1, // 19 - PA3 with PH0 low

  PRESCALE_BIT0, // 20 - PA0 with PH1 low
  PRESCALE_BIT1, // 21 - PA1
  DUMMY2, // 22 - PA2
  DUMMY3, // 23 PA3 with PH1 low

  INST_SEL_BIT0, // 24 - PA0 with PH2 low
  INST_SEL_BIT1,
  INST_SEL_BIT2,
  DUMMY4, // 27 - PA3 with PH2 low

  CLEAR_KEY, // 28 - PA0 w/ PH3 low, unused
  FUNCTION_KEY,
  GROUP_KEY,
  DUMMY5, // 31 - PA3 w/ PH3 low

  // Extra status pins - read with PH0-PH3 all held high
  RUN, // PA0
  TAP_WRITE,
  NOTHING, // ? pin 5 of the DIN jack
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
  PSCODE_1 = 0x0,
  PSCODE_2 = 0x1,
  PSCODE_3 = 0x2,
  PSCODE_4 = 0x3,
};
enum InstSelectCode : uint8_t {
  AC_CODE = 0b1011,
  BD_CODE = 0b1010,
  SD_CODE = 0b1001,
  LT_CODE = 0b1000,
  HT_CODE = 0b0110,
  CY_CODE = 0b0010,
  OH_CODE = 0b0001,
  CH_CODE = 0b0000,
};
// in correct order
static const uint8_t inst_codes[] = {
  AC_CODE, BD_CODE, SD_CODE, LT_CODE,
  HT_CODE, CY_CODE, OH_CODE, CH_CODE,
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

// MIDI notes per instrument; follows General MIDI drum mapping
static const uint8_t INST_NOTE[] = {
  0,    // AC - unused, derived from velocity
  36,   // BD  Bass Drum 1
  38,   // SD  Acoustic Snare
  41,   // LT  Low Floor Tom
  48,   // HT  Hi-Mid Tom
  49,   // CY  Crash Cymbal 1
  46,   // OH  Open Hi-Hat
  42,   // CH  Closed Hi-Hat
};
