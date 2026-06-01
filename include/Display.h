#pragma once
#include <stdint.h>

void displayInit();
void displayTick();
void displayTexto(const char* texto, uint16_t velocidad = 40);
void displayMarcador(uint8_t local, uint8_t visitante);
void displayGol();
