#pragma once

// SP1 — efectos de voz/gol
// DFPlayer 1: G26 (ESP32 TX → RX módulo) / G27 (ESP32 RX ← TX módulo)

enum class Pista { GOL1 = 1, GOL2 = 2 };

void vozBegin();
void vozPlay(Pista pista);
void vozPoll();
