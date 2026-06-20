#pragma once
#include <stdint.h>

// SP1 — efectos de voz/gol
// DFPlayer 1: G26 (ESP32 TX → RX módulo) / G27 (ESP32 RX ← TX módulo)

enum class Pista { GOL1 = 1, GOL2 = 2 };

void vozBegin();
void vozPlay(Pista pista);
void vozPlayTrack(uint8_t n);   // reproduce pista arbitraria (1-255)
void vozPoll();
void vozSetVolumen(uint8_t vol);
void vozPitidoInicio();         // pitido de inicio (pistas 77-78)
void vozPitidoFinal();          // pitido de fin    (pistas 79-80)
bool vozIsBusy();               // true mientras SP1 está reproduciendo un track
