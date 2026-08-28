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
    uint8_t  intervaloDisplay;  // seg entre marcador↔tiempo en modo tiempo (default 5)

    // Display — textos customizables (farola)
    char textoBoot[20];             // al bootear el ESP32          — "METEGOL!"
    char textoArranca[20];          // al iniciar partido            — "ARRANCAAA!"
    char textoPausa[20];            // al pausar                     — "PAUSA!"
    char textoReanuda[20];          // al reanudar                   — "VAMOS!"
    char textoCancelado[20];        // al cancelar                   — "CANCELADO!"
    char textoGol[20];              // en cada gol                   — "Gollll!!!"
    char textoGanadorCeleste[28];   // fin de partido, ganó celeste  — "Fin! Ganador Celeste!"
    char textoGanadorBlanco[28];    // fin de partido, ganó blanco   — "Fin! Ganador Blanco!"
    char textoEmpate[28];           // fin de partido, empate        — "Fin! Empate!"
    char textoPreparense[20];       // prefijo del anuncio del próximo partido de torneo — "Preparense"

    // Comentarista — intervalo y thresholds
    uint16_t intervaloComentariosMin; // default 10
    uint16_t intervaloComentariosMax; // default 30
    uint16_t intervaloStats;          // default 4
    uint8_t  goleadaDiff;             // diff mínimo para goleada (default 3)
    uint8_t  calienteGoles;           // total goles para "caliente" (default 4)
    uint8_t  hinchadaGol;             // gol del partido en que suena la hinchada (default 2)
    uint16_t inicioSegs;              // seg de INICIO (default 30)
    uint16_t primerosMinsSegs;        // seg de PRIMEROS_MINUTOS (default 120)
    uint16_t ultimoTramoSegs;         // seg restantes para ultimo_tramo en modo tiempo (default 60)
    uint16_t umbralAburridoSegs;      // seg sin goles para ABURRIDO aunque haya goleada (default 180)

    // Comentarista — rangos por estado (pistas 01–54)
    RangoAudio comentInicio;          // {1,  6}
    RangoAudio comentPrimerosMins;    // {7,  12}
    RangoAudio comentParejo;          // {13, 18}
    RangoAudio comentCaliente;        // {19, 24}
    RangoAudio comentGoleada;         // {25, 30}
    RangoAudio comentDefinido;               // {31, 36}
    RangoAudio comentUltimoTramoGeneral;     // {37, 42} — inactivo (reservado)
    RangoAudio comentAburrido;               // {43, 48}

    // Último tramo — subcategorías por estado del partido (pistas 89–111)
    RangoAudio comentUltimoTramoEmpateGoles; // {89, 94}
    RangoAudio comentUltimoTramoGoleada;     // {95, 100}
    RangoAudio comentUltimoTramoAjustado;    // {101, 106}
    RangoAudio comentUltimoTramoAburrido;    // {107, 111}
    RangoAudio comentTranquilo;       // {49, 54}

    // Goles — selección contextual (pistas 55–76)
    RangoAudio golNormal;             // {55, 58}
    RangoAudio golEfusivo;            // {59, 62}
    RangoAudio golEmpate;             // {63, 66}
    RangoAudio golCaliente;           // {67, 70}
    RangoAudio golAgonico;            // {71, 73}
    RangoAudio golAgonicoEmpate;      // {74, 76}

    // Pitidos
    RangoAudio pitidoInicio;          // {77, 78}
    RangoAudio pitidoFinal;           // {79, 80}
    // Comentarios de fin de partido
    RangoAudio finalEmpate;           // {81, 82}
    RangoAudio finalAplastante;       // {83, 84}
    RangoAudio finalAjustada;         // {85, 86}
    RangoAudio finalNormal;           // {87, 88}
    // SP2 — ambiente reactivo (pistas 1-17, SD card propia)
    RangoAudio ambienteGenerico;      // {1, 4}
    RangoAudio hinchadaMusica;        // {5, 8}
    RangoAudio momentoCaliente;       // {9, 11}
    RangoAudio ambienteGol;           // {12, 17}
};

extern Config config;

void webConfigInit(Partido* p);  // inicia AP + servidor
void webConfigLoop();            // llamar en loop()
