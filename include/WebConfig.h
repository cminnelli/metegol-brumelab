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
    uint8_t  golesMax;          // 1-10
    uint16_t duracionMin;       // 1-30 minutos

    // Display
    uint8_t  brillo;            // 0-15
    uint8_t  velocidadScroll;   // 10-100 ms/frame

    // Comentarista — intervalo y thresholds
    uint16_t intervaloComentarios; // segundos entre evaluaciones (default 20)
    uint8_t  goleadaDiff;          // diff mínimo para goleada (default 3)
    uint8_t  calienteGoles;        // total goles para "caliente" (default 4)
    uint16_t inicioSegs;           // segundos de fase inicio (default 60)
    uint8_t  ultimoTramoPorc;      // % tiempo restante para ultimo_tramo (default 10)

    // Comentarista — rangos de audio por estado
    RangoAudio comentGol;         // dispara al marcar un gol (sensor)
    RangoAudio comentInicio;
    RangoAudio comentTranquilo;
    RangoAudio comentParejo;
    RangoAudio comentCaliente;
    RangoAudio comentGoleada;
    RangoAudio comentDefinido;
    RangoAudio comentUltimoTramo;
};

extern Config config;

void webConfigInit(Partido* p);  // inicia AP + servidor
void webConfigLoop();            // llamar en loop()
