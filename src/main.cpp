#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include "Partido.h"
#include "Audio.h"

#define PIN_CS       5
#define MODULOS      4
#define PIN_SENSOR1  34
#define PIN_SENSOR2  35

MD_Parola pantalla(MD_MAX72XX::FC16_HW, 23, 18, PIN_CS, MODULOS);
Partido partido;

static char bufDisplay[32];

void scrollear(const char* texto) {
    pantalla.displayScroll(texto, PA_CENTER, PA_SCROLL_LEFT, 40);
    while (!pantalla.displayAnimate()) { yield(); }  // yield() alimenta el WDT
}

void mostrarResultado() {
    partido.getResultado(bufDisplay, sizeof(bufDisplay));
    pantalla.displayText(bufDisplay, PA_CENTER, 0, 2000, PA_PRINT);
    while (!pantalla.displayAnimate()) { yield(); }  // yield() alimenta el WDT
}

void nuevaPartida() {
    partido.resetear();
    mostrarResultado();
}

void mostrarGanador() {
    int8_t w = partido.ganador();
    if (w == 0)      scrollear("  GANO EQUIPO 1!  ");
    else if (w == 1) scrollear("  GANO EQUIPO 2!  ");
    else             scrollear("  EMPATE!  ");
    nuevaPartida();
}

void registrarGol(uint8_t equipo) {
    Serial.print(">>> GOL equipo ");
    Serial.println(equipo);
    partido.sumarGol(equipo);
    Serial.println(">>> sumarGol OK");
    reproducir(Pista::GOL);
    Serial.println(">>> reproducir OK");
    scrollear("  GOL!  ");
    Serial.println(">>> scrollear OK");
    mostrarResultado();
    if (partido.termino()) mostrarGanador();
}

void setup() {
    Serial.begin(115200);
    Serial.println("[1] Serial OK");

    pinMode(PIN_SENSOR1, INPUT);
    pinMode(PIN_SENSOR2, INPUT);
    Serial.println("[2] Sensores OK");

    delay(500);
    audioInit();
    Serial.println("[3] Audio init OK");

    reproducir(Pista::GOL);
    Serial.println("[4] Reproducir OK");

    pantalla.begin();
    Serial.println("[5] Display begin OK");

    pantalla.setIntensity(5);
    pantalla.displayClear();
    Serial.println("[6] Display clear OK");

    nuevaPartida();
    Serial.println("[7] Setup completo");
}

void loop() {
    audioPoll();

    static unsigned long ultimoGol = 0;
    static bool prev1 = HIGH, prev2 = HIGH;

    bool cur1 = digitalRead(PIN_SENSOR1);
    bool cur2 = digitalRead(PIN_SENSOR2);

    if (millis() - ultimoGol >= 2000) {
        if (cur1 == LOW && prev1 == HIGH) {
            ultimoGol = millis();
            prev1 = LOW;
            registrarGol(0);
            return;
        } else if (cur2 == LOW && prev2 == HIGH) {
            ultimoGol = millis();
            prev2 = LOW;
            registrarGol(1);
            return;
        }
    }

    prev1 = cur1;
    prev2 = cur2;
}
