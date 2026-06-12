#include <Arduino.h>
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Partido.h"
#include "WebConfig.h"
#include "Comentarista.h"
#include "config.h"

#define PIN_SENSOR1 34
#define PIN_SENSOR2 35

Partido partido;


void setup() {
    Serial.begin(115200);
    Serial.println("[1] Serial OK");
    webConfigInit(&partido);  // carga config desde NVS + inicia AP WiFi (activa RF → entropía real)
    randomSeed(esp_random() ^ (uint32_t)micros());

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
    comentaristaLoop(partido);

    static bool prev1 = HIGH, prev2 = HIGH;
    static unsigned long ultimoGol1 = 0;
    static unsigned long ultimoGol2 = 0;
    bool cur1 = digitalRead(PIN_SENSOR1);
    bool cur2 = digitalRead(PIN_SENSOR2);


    // Sensores activos solo durante el partido
    if (partido.activo) {
        if (millis() - ultimoGol1 >= 2000) {
            if (cur1 == LOW && prev1 == HIGH) {
                ultimoGol1 = millis();
                partido.registrarGol(0);
            }
        }
        if (millis() - ultimoGol2 >= 2000) {
            if (cur2 == LOW && prev2 == HIGH) {
                ultimoGol2 = millis();
                partido.registrarGol(1);
            }
        }
    }

    static unsigned long ultimoStats = 0;
    if (millis() - ultimoStats >= (uint32_t)config.intervaloStats * 1000UL) {
        ultimoStats = millis();
        comentaristaStats(partido);
    }

    prev1 = cur1;
    prev2 = cur2;
}
