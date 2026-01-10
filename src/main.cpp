#include <Arduino.h>

const int NUMPINS = 44;
const int PINPAIRS = NUMPINS / 2;

void setup() {
  Serial.begin(9600);
  for (int i = 0; i <= NUMPINS; ++i) {
    pinMode(i, OUTPUT);
  }
}

void loop() {
  int loop_count = 0;

  for (int i = 0; i <= NUMPINS; ++i) {
    Serial.printf("flashing pin# %d\n", i);
    digitalWrite(i, HIGH);
    delay(500);
    digitalWrite(i, LOW);
    delay(500);
  }
  Serial.printf("one cycle complete, count = %d\n", loop_count++);
  delay(1000);
}
