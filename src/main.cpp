#include <Arduino.h>
#include "Audio.h"

// ── TEST MODO SOLO AUDIO ──────────────────────────────────────────────────────
// Display y sensores desactivados. Solo prueba el DFPlayer.
// Al arrancar reproduce 0001.mp3.
// ─────────────────────────────────────────────────────────────────────────────

// #include <MD_Parola.h>
// #include <MD_MAX72XX.h>
// #include <SPI.h>
// #include "Partido.h"

// #define PIN_CS       5
// #define MODULOS      4
// #define PIN_SENSOR1  34
// #define PIN_SENSOR2  35

// MD_Parola pantalla(MD_MAX72XX::FC16_HW, 23, 18, PIN_CS, MODULOS);
// Partido partido;

void setup() {
    Serial.begin(115200);
    Serial.println("[1] Serial OK");

    audioInit();
    Serial.println("[2] Audio init OK");

    reproducir(Pista::GOL);
    Serial.println("[3] Reproduciendo 0001.mp3...");
}

void loop() {
    audioPoll();
}
