#include <Arduino.h>

const int MAXPIN = 45;

enum PinoutDefs : uint8_t {
  MIDI_IN_PIN = 2,
  MIDI_OUT_PIN = 3,

  // Port C - Data inputs/outputs...?
  PC0_PIN = 4,
  PC1_PIN = 5,
  PC2_PIN = 6,
  PC3_PIN = 7,

  TIMEMODE_LED = PC0_PIN,
  BLACKKEY5_LED = PC1_PIN, // aka PC1
  PITCHMODE_LED = PC2_PIN, // aka PC2
  FUNCTION_LED = PC3_PIN, // aka PC3

  PI1_PIN = 8, // Pitch data latch strobe
  PI2_PIN = 9, // Gate signal

  // Ports D & F - memory address to pitch data (unused?)
  PD0_PIN = 10,
  PD1_PIN = 11,
  PD2_PIN = 12,
  PD3_PIN = 13,
  PF0_PIN = 14,
  PF1_PIN = 15,
  PF2_PIN = 16,
  PF3_PIN = 17,

  // Port E - memory address (unused?)
  PE3_PIN = 1,
  PE2_PIN = 0,
  PE1_PIN = 19,
  PE0_PIN = 18,

  // Port B - Switch board INPUTS
  PB3_PIN = 27,
  PB2_PIN = 26,
  PB1_PIN = 25,
  PB0_PIN = 24,

  // Port A - switched inputs to STATUS (TEMPO CLOCK, START/STOP, TAP)
  PA3_PIN = 23,
  PA2_PIN = 22,
  PA1_PIN = 21,
  PA0_PIN = 20,

  // Port H - switched outputs to STATUS, BUFFER, & GATE
  // ...I think these are the mux selectors for PG and PB
  PH0_PIN = 38,
  PH1_PIN = 39,
  PH2_PIN = 40,
  PH3_PIN = 41,

  // Port G - drive signals to switch board LEDs
  PG0_LED_MUX = 42, // [1], [DEL], [DOWN], [5]
  PG1_LED_MUX = 43, // [2], [INS], [UP], [6]
  PG2_LED_MUX = 44, // [3], [F#], [ACCENT], [7]
  PG3_LED_MUX = 45, // [4], [G#], [SLIDE], [8]
};


void setup() {
  Serial.begin(9600);
  for (int i = 0; i <= MAXPIN; ++i) {
    pinMode(i, OUTPUT);
  }
}

void loop() {
  int pinnum = 0;
  if (Serial.available()) {
    pinnum = Serial.parseInt();
    if (pinnum < 0) pinnum = 0;
    if (pinnum > MAXPIN) pinnum = MAXPIN;

    Serial.printf("Checking pin# %d\n", pinnum);

    digitalWrite(pinnum, HIGH);
    delay(500);
    digitalWrite(pinnum, LOW);
  }
  //delay(1000);
}
