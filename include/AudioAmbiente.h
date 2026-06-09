#pragma once
#include <stdint.h>

// SP2 — sonido ambiente central
// DFPlayer 2: G16 (ESP32 TX → RX módulo) / G17 (ESP32 RX ← TX módulo)

void ambienteBegin();
void ambientePlay(uint8_t pista);  // número de pista (1-based)
void ambientePoll();
void ambienteSetVolumen(uint8_t vol);
