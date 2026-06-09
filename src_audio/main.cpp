#include <Arduino.h>
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Partido.h"
#include "WebConfig.h"
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

    bool terminado = (config.modoJuego == 0)
        ? (partido.goles[0] >= config.golesMax || partido.goles[1] >= config.golesMax)
        : ((millis() - partido.inicio) >= (uint32_t)config.duracionMin * 60000UL);

    if (terminado) {
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

    webConfigInit();  // carga config desde NVS + inicia AP WiFi

    Serial.println("=== CONFIG CARGADA ===");
    Serial.printf("  Volumen (voz)     : %d\n", config.volumenVoz);
    Serial.printf("  Volumen (ambiente): %d\n", config.volumenAmbiente);
    Serial.printf("  Modo de juego     : %s\n", config.modoJuego == 0 ? "goles" : "tiempo");
    if (config.modoJuego == 0)
        Serial.printf("  Goles para ganar  : %d\n", config.golesMax);
    else
        Serial.printf("  Duracion          : %d min\n", config.duracionMin);
    Serial.printf("  Brillo display    : %d\n", config.brillo);
    Serial.printf("  Vel. scroll       : %d ms\n", config.velocidadScroll);
    Serial.printf("  Pista ambiente    : %d\n", config.pistaAmbiente);
    Serial.println("======================");

    pinMode(PIN_SENSOR1, INPUT);
    pinMode(PIN_SENSOR2, INPUT);

    ambienteBegin();
    vozBegin();

    // Boot wait compartido 2s — captura 0x3F de SP1 y SP2
    Serial.println("[Audio] Boot SP1+SP2 (2 s)...");
    for (uint16_t i = 0; i < 200; i++) {
        vozPoll();
        ambientePoll();
        delay(10);
    }

    // Aplica volúmenes desde config guardada
    vozSetVolumen(config.volumenVoz);
    for (uint8_t i = 0; i < 30; i++) { vozPoll(); ambientePoll(); delay(10); }
    ambienteSetVolumen(config.volumenAmbiente);
    for (uint8_t i = 0; i < 30; i++) { vozPoll(); ambientePoll(); delay(10); }
    // Inicia SP2 con pista ambiente
    ambientePlay(config.pistaAmbiente);

    partido.resetear();
    Serial.println("[2] Sistema listo");
}

void loop() {
    webConfigLoop();
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

    if (config.modoJuego == 1) {
        uint32_t durMs = (uint32_t)config.duracionMin * 60000UL;
        static unsigned long ultimoAviso = 0;
        if (millis() - ultimoAviso >= 10000) {
            ultimoAviso = millis();
            uint32_t restante = durMs - (millis() - partido.inicio);
            Serial.printf("[JUEGO] Tiempo restante: %lu s\n", restante / 1000);
        }
    }

    prev1 = cur1;
    prev2 = cur2;
}
