#pragma once
#include "Partido.h"

void        comentaristaLoop(const Partido& partido);
void        comentaristaOnGol(const Partido& partido);
void        comentaristaStats(const Partido& partido);
void        comentaristaReiniciar();                        // resetea estado interno para nuevo partido
const char* comentaristaGetEstado(const Partido& partido);  // nombre del estado actual
