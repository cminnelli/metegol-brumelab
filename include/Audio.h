#pragma once

// ── Speaker 1 — efectos de gol (ya instalado) ────────────────────────────────
// DFPlayer 1: ESP32 G26 (TX→RX módulo) / G27 (RX←TX módulo)
// Pistas: 0001.mp3 = gol equipo 1 | 0002.mp3 = gol equipo 2
enum class Pista { GOL = 1, GOL2 = 2 };

void audioInit();
void reproducir(Pista pista);
void audioStop();
void audioPoll();

// ── Speaker 2 — sonido ambiente (a instalar) ─────────────────────────────────
// DFPlayer 2: ESP32 G16 (TX→RX módulo) / G17 (RX←TX módulo)
// Pista: 0001.mp3 = música ambiente en loop
void audioAmbienteInit();
void audioAmbienteLoop();   // inicia loop de la pista ambiente
void audioAmbientePoll();
