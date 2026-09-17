#pragma once
#include <stdint.h>

void displayInit();
void displayTick();
void displaySetBrillo(uint8_t brillo);   // aplica el brillo normal en el acto (0-15), sin esperar a reiniciar
void displayTexto(const char* texto, uint16_t velocidad = 40);
void displayMarcador(uint8_t local, uint8_t visitante);
void displayMarcadorConScroll(uint8_t local, uint8_t visitante);  // como displayMarcador, pero con transición: sale lo que había y entra deslizando
void displayGol();
void displayGanador(int8_t ganador);
void displayTiempo(uint32_t ms);
void displayTiempoActualizar(uint32_t ms);  // refresca los segundos en el lugar, sin animación (tick en vivo)
void displayModo(const char* texto);
bool displayEnScroll();
void displayApagar();                 // reposo: apaga el hardware de la matriz (shutdown real, no solo brillo 0)
void displayEncender(uint8_t brillo); // sale de reposo: reactiva la matriz y restaura el brillo normal
