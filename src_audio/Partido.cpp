#include "Partido.h"
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
