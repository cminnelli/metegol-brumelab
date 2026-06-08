#include <Arduino.h>
#include "Audio.h"

#define PIN_SENSOR1 34
#define PIN_SENSOR2 35

void setup() {
    Serial.begin(115200);
    Serial.println("[1] Serial OK");
    pinMode(PIN_SENSOR1, INPUT);
    pinMode(PIN_SENSOR2, INPUT);
    audioInit();
    Serial.println("[2] Audio init OK");
}

void loop() {
    audioPoll();

    static bool prev1 = HIGH, prev2 = HIGH;
    bool cur1 = digitalRead(PIN_SENSOR1);
    bool cur2 = digitalRead(PIN_SENSOR2);

    if (cur1 == LOW && prev1 == HIGH) {
        Serial.println("[SENSOR 1] Gol detectado!");
        reproducir(Pista::GOL);
    }
    if (cur2 == LOW && prev2 == HIGH) {
        Serial.println("[SENSOR 2] Gol detectado!");
        reproducir(Pista::GOL);
    }

    prev1 = cur1;
    prev2 = cur2;
}
