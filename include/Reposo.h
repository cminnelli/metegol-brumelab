#pragma once
#include "Partido.h"

// Modo reposo: tras standbyTimeoutSegs de inactividad (sin partido en curso ni
// pausado), baja el brillo de la farola, silencia SP1/SP2, y baja el clock de
// la ESP32 — hasta que llega actividad real (botón/encoder, o un partido que
// arranca por la web).

void reposoInit();                        // llamar una vez en setup()
void reposoNotificarActividad();          // llamar en cada interacción física (botón, encoder)
void reposoTick(const Partido& partido);  // llamar cada loop()
bool reposoActivo();
