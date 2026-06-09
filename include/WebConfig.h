#pragma once
#include <stdint.h>

struct Config {
    uint8_t  volumenVoz;        // SP1 0-30
    uint8_t  volumenAmbiente;   // SP2 0-30
    uint8_t  modoJuego;         // 0=goles, 1=tiempo
    uint8_t  golesMax;          // 1-10
    uint16_t duracionMin;       // 1-30 minutos
    uint8_t  brillo;            // 0-15
    uint8_t  velocidadScroll;   // 10-100 ms/frame
    uint8_t  pistaAmbiente;     // 1-3
};

extern Config config;

void webConfigInit();   // inicia AP + servidor
void webConfigLoop();   // llamar en loop()
