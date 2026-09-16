#pragma once
#include <stdint.h>

void displayInit();
void displayTick();
void displayTexto(const char* texto, uint16_t velocidad = 40);
void displayMarcador(uint8_t local, uint8_t visitante);
void displayGol();
void displayGanador(int8_t ganador);
void displayTiempo(uint32_t ms);
void displayModo(const char* texto);
bool displayEnScroll();
void displayEntrarReposo(const char* texto, uint8_t brillo);  // baja brillo + scrollea mensaje de reposo
void displaySalirReposo(uint8_t brilloNormal);                 // restaura brillo normal
