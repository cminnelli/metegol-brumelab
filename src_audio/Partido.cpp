#include "Partido.h"
#include "WebConfig.h"
#include "Comentarista.h"
#include <stdio.h>
#include <Arduino.h>

void Partido::sumarGol(uint8_t equipo) {
    if (equipo < 2) goles[equipo]++;
}

void Partido::resetear() {
    goles[0] = 0;
    goles[1] = 0;
    inicio    = millis();
}

void Partido::getResultado(char* buf, size_t len) const {
    snprintf(buf, len, "%d - %d", goles[0], goles[1]);
}

bool Partido::termino() const {
#ifdef MODO_GOLES
    return goles[0] >= GOLES_MAX || goles[1] >= GOLES_MAX;
#else
    return (millis() - inicio) >= DURACION_MS;
#endif
}

int8_t Partido::ganador() const {
    if (goles[0] > goles[1]) return 0;
    if (goles[1] > goles[0]) return 1;
    return -1;
}

void Partido::registrarGol(uint8_t equipo) {
    sumarGol(equipo);

    Serial.printf("[JUEGO] Gol equipo %d\n", equipo + 1);
    char buf[16];
    getResultado(buf, sizeof(buf));
    Serial.printf("[JUEGO] Marcador: %s\n", buf);

    comentaristaOnGol(*this);

    bool terminado = (config.modoJuego == 0)
        ? (goles[0] >= config.golesMax || goles[1] >= config.golesMax)
        : ((millis() - inicio) >= (uint32_t)config.duracionMin * 60000UL);

    if (terminado) {
        int8_t w = ganador();
        if (w == 0)      Serial.println("[JUEGO] ¡Ganó equipo 1!");
        else if (w == 1) Serial.println("[JUEGO] ¡Ganó equipo 2!");
        else             Serial.println("[JUEGO] ¡Empate!");
        resetear();
        Serial.println("[JUEGO] Partido reiniciado");
    }
}
