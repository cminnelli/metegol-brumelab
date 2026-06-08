#include <Arduino.h>
#include "Audio.h"

#define PIN_SENSOR1 34
#define PIN_SENSOR2 35

void setup() {
    Serial.begin(115200);
    Serial.println("[1] Serial OK");

    pinMode(PIN_SENSOR1, INPUT);
    pinMode(PIN_SENSOR2, INPUT);

    // Inicia SP1 (G26/G27) + SP2 (G16/G17) con boot wait compartido
    audioAmbienteBegin();  // Serial1
    audioInit();           // Serial2
    audioInitAll();        // wait 2s escuchando ambos, luego configura los dos
    Serial.println("[2] SP1+SP2 listos");
}

void loop() {
    audioPoll();
    audioAmbientePoll();

    static bool prev1 = HIGH, prev2 = HIGH;
    static unsigned long ultimoGol = 0;
    bool cur1 = digitalRead(PIN_SENSOR1);
    bool cur2 = digitalRead(PIN_SENSOR2);

    if (millis() - ultimoGol >= 2000) {
        if (cur1 == LOW && prev1 == HIGH) {
            ultimoGol = millis();
            Serial.println("[SENSOR 1] Gol!");
            reproducir(Pista::GOL);  // 0001.mp3
        } else if (cur2 == LOW && prev2 == HIGH) {
            ultimoGol = millis();
            Serial.println("[SENSOR 2] Gol!");
            reproducir(Pista::GOL2);  // 0002.mp3
        }
    }

    prev1 = cur1;
    prev2 = cur2;
}
