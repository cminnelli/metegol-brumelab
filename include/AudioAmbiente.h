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
uint32_t    ambienteGetDuracionSegs();       // hace cuánto está sonando la pista actual de SP2
uint32_t    ambienteGetDuracionTotalSegs();  // duración total conocida de esa pista (0 si no está en la tabla)
uint32_t    ambienteGetDuracionMs();         // igual que ambienteGetDuracionSegs(), en ms
uint32_t    ambienteGetDuracionTotalMs();    // igual que ambienteGetDuracionTotalSegs(), en ms
void        ambienteEntrarReposo();  // pausa + intenta bajar el chip a standby
void        ambienteSalirReposo();   // vuelve a modo normal + restaura volumen
