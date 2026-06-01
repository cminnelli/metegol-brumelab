#pragma once

// ── Modalidad ────────────────────────────────────────────
// Descomentá UNA sola opción

#define MODO_GOLES      // primer equipo en llegar a GOLES_MAX gana
// #define MODO_TIEMPO  // gana quien tenga más goles al acabar el tiempo

// ── Parámetros ───────────────────────────────────────────
#define GOLES_MAX       4           // goles para ganar (MODO_GOLES)
#define DURACION_MS     300000UL    // duración del partido en ms (MODO_TIEMPO) — 5 min

// ── Audio ────────────────────────────────────────────
#define VOLUMEN         30          // 0 (mínimo) a 30 (máximo)

// ── Display ──────────────────────────────────────────────
#define BRILLO          5           // 0 (mínimo) a 15 (máximo)
#define VELOCIDAD_SCROLL 40         // ms por frame (menor = más rápido)
