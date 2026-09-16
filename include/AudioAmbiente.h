#pragma once
#include <stdint.h>

// SP2 — sonido ambiente central
// DFPlayer 2: G16 (ESP32 TX → RX módulo) / G17 (ESP32 RX ← TX módulo)

void        ambienteBegin();
void        ambientePoll();
void        ambienteSetVolumen(uint8_t vol);
void        ambienteReiniciar();
void        ambienteActualizar(bool activo, bool esCaliente);
void        ambienteOnGol();
const char* ambienteGetEstado();
uint8_t     ambienteGetPista();
void        ambienteEntrarReposo();  // pausa + intenta bajar el chip a standby
void        ambienteSalirReposo();   // vuelve a modo normal + restaura volumen
