#include <Arduino.h>

const int NUMPINS = 44;
const int PINPAIRS = NUMPINS / 2;

void setup() {
  Serial.begin(9600);
  for (int i = 0; i <= NUMPINS; ++i) {
    pinMode(i, INPUT);
  }
}

void loop() {
  for (int i = 0; i <= PINPAIRS; ++i) {
    Serial.printf("%2d = 0x%4x  | %2d = 0x%4x \n", i, analogRead(i),
                  i + PINPAIRS, analogRead(i + PINPAIRS));
  }
  Serial.println();
  delay(1000);
}
