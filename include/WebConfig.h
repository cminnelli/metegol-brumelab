#pragma once
#include <stdint.h>
#include "Partido.h"

struct RangoAudio {
    uint8_t desde;
    uint8_t hasta;
};

struct Config {
    // Audio
    uint8_t  volumenVoz;        // SP1 0-30
    uint8_t  volumenAmbiente;   // SP2 0-30
    uint8_t  pistaAmbiente;     // 1-3

    // Juego
    uint8_t  modoJuego;         // 0=goles, 1=tiempo
    uint8_t  golesMax;          // 4-10
    uint16_t duracionMin;       // 3-8 minutos

    // Display
    uint8_t  brillo;            // 0-15
    uint8_t  velocidadScroll;   // 10-100 ms/frame

    // Comentarista — intervalo y thresholds
    uint16_t intervaloComentariosMin; // default 10
    uint16_t intervaloComentariosMax; // default 30
    uint16_t intervaloStats;          // default 4
    uint8_t  goleadaDiff;             // diff mínimo para goleada (default 3)
    uint8_t  calienteGoles;           // total goles para "caliente" (default 4)
    uint16_t inicioSegs;              // seg de INICIO (default 30)
    uint16_t primerosMinsSegs;        // seg de PRIMEROS_MINUTOS (default 120)
    uint8_t  ultimoTramoPorc;         // % tiempo restante para ultimo_tramo (default 10)

    // Comentarista — rangos por estado (pistas 01–54)
    RangoAudio comentInicio;          // {1,  6}
    RangoAudio comentPrimerosMins;    // {7,  12}
    RangoAudio comentParejo;          // {13, 18}
    RangoAudio comentCaliente;        // {19, 24}
    RangoAudio comentGoleada;         // {25, 30}
    RangoAudio comentDefinido;        // {31, 36}
    RangoAudio comentUltimoTramo;     // {37, 42}
    RangoAudio comentAburrido;        // {43, 48}
    RangoAudio comentTranquilo;       // {49, 54}

    // Goles — selección contextual (pistas 55–76)
    RangoAudio golNormal;             // {55, 58}
    RangoAudio golEfusivo;            // {59, 62}
    RangoAudio golEmpate;             // {63, 66}
    RangoAudio golCaliente;           // {67, 70}
    RangoAudio golAgonico;            // {71, 73}
    RangoAudio golAgonicoEmpate;      // {74, 76}
};

extern Config config;

void webConfigInit(Partido* p);  // inicia AP + servidor
void webConfigLoop();            // llamar en loop()
