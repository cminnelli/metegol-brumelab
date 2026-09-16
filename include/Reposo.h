#pragma once
#include "Partido.h"

// Modo reposo: tras standbyTimeoutSegs de inactividad (sin partido en curso ni
// pausado), apaga la farola y la radio WiFi, silencia SP1/SP2, y baja el clock
// de la ESP32 al mínimo — bajo consumo real, pensado para quedarse así toda la
// noche si nadie lo despierta. Con el WiFi apagado el panel web deja de responder,
// así que la única forma de salir es un click del encoder en la mesa.

void reposoInit();                        // llamar una vez en setup()
void reposoNotificarActividad();          // llamar en cada interacción física (botón, encoder)
void reposoTick(const Partido& partido);  // llamar cada loop()
bool reposoActivo();
