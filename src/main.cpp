#include <Arduino.h>

const int MAXPIN = 45;

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
