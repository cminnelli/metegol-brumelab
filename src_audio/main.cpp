#include <Arduino.h>
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Partido.h"
#include "config.h"

#define PIN_SENSOR1 34
#define PIN_SENSOR2 35

Partido partido;

static void registrarGol(uint8_t equipo) {
    partido.sumarGol(equipo);

    if (equipo == 0) {
        Serial.println("[JUEGO] Gol equipo 1");
        vozPlay(Pista::GOL1);
    } else {
        Serial.println("[JUEGO] Gol equipo 2");
        vozPlay(Pista::GOL2);
    }

    char buf[16];
    partido.getResultado(buf, sizeof(buf));
    Serial.printf("[JUEGO] Marcador: %s\n", buf);

    if (partido.termino()) {
        int8_t w = partido.ganador();
        if (w == 0)      Serial.println("[JUEGO] ¡Ganó equipo 1!");
        else if (w == 1) Serial.println("[JUEGO] ¡Ganó equipo 2!");
        else             Serial.println("[JUEGO] ¡Empate!");
        partido.resetear();
        Serial.println("[JUEGO] Partido reiniciado");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[1] Serial OK");

    pinMode(PIN_SENSOR1, INPUT);
    pinMode(PIN_SENSOR2, INPUT);

    // Inicia ambos seriales antes del boot wait
    ambienteBegin();
    vozBegin();

    // Boot wait compartido 2s — captura 0x3F de SP1 y SP2
    Serial.println("[Audio] Boot SP1+SP2 (2 s)...");
    for (uint16_t i = 0; i < 200; i++) {
        vozPoll();
        ambientePoll();
        delay(10);
    }

    // Configura SP1
    // (volumen ya configurado en ambientePlay)
    Serial.println("[SP1] Listo ✓");

    // Inicia SP2 con pista ambiente
    ambientePlay(3);  // 0003.mp3 en loop

    partido.resetear();

#ifdef MODO_GOLES
    Serial.printf("[JUEGO] Modo: primero en %d goles gana\n", GOLES_MAX);
#else
    Serial.printf("[JUEGO] Modo: tiempo %lu ms\n", DURACION_MS);
#endif

    Serial.println("[2] Sistema listo");
}

void loop() {
    vozPoll();
    ambientePoll();

    static bool prev1 = HIGH, prev2 = HIGH;
    static unsigned long ultimoGol = 0;
    bool cur1 = digitalRead(PIN_SENSOR1);
    bool cur2 = digitalRead(PIN_SENSOR2);

    if (millis() - ultimoGol >= 2000) {
        if (cur1 == LOW && prev1 == HIGH) {
            ultimoGol = millis();
            registrarGol(0);
        } else if (cur2 == LOW && prev2 == HIGH) {
            ultimoGol = millis();
            registrarGol(1);
        }
    }

#ifndef MODO_GOLES
    // Modo tiempo: verificar si terminó el partido
    if (!partido.termino()) {
        unsigned long restante = DURACION_MS - (millis() - partido.inicio);
        static unsigned long ultimoAviso = 0;
        if (millis() - ultimoAviso >= 10000) {
            ultimoAviso = millis();
            Serial.printf("[JUEGO] Tiempo restante: %lu s\n", restante / 1000);
        }
    } else {
        int8_t w = partido.ganador();
        if (w == 0)      Serial.println("[JUEGO] ¡Tiempo! Ganó equipo 1");
        else if (w == 1) Serial.println("[JUEGO] ¡Tiempo! Ganó equipo 2");
        else             Serial.println("[JUEGO] ¡Tiempo! Empate");
        partido.resetear();
    }
#endif

    prev1 = cur1;
    prev2 = cur2;
}
