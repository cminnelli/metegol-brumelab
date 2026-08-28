#pragma once
#include <stdint.h>
#include <Arduino.h>

#define TORNEO_MAX_JUG          16
#define TORNEO_MAX_GRUPOS        4
#define TORNEO_MAX_PART_GRUPO    4
#define TORNEO_MAX_PARTIDOS_GRUPO (TORNEO_MAX_GRUPOS * 6)  // C(4,2)=6 partidos por grupo máx.
#define TORNEO_MAX_PARTIDOS_KO   15                        // 8+4+2+1 para un cuadro de 16

struct ParticipanteT {
    char    nombre[16];
    uint8_t grupo;
};

struct PartidoGrupo {
    uint8_t pA, pB;         // índices en torneo.participantes
    uint8_t golA, golB;
    bool    jugado;
};

struct PartidoKO {
    int8_t  pA, pB;         // -1 = bye (sin rival, pA pasa directo)
    uint8_t golA, golB;
    bool    jugado;
    uint8_t ronda;          // 0 = primera ronda del cuadro, crece hacia la final
};

struct Torneo {
    bool     activo;
    uint8_t  fase;          // 0=grupos, 1=knockout, 2=finalizado
    uint8_t  nParticipantes;
    ParticipanteT participantes[TORNEO_MAX_JUG];
    uint8_t  nGrupos;
    PartidoGrupo  partidosGrupo[TORNEO_MAX_PARTIDOS_GRUPO];
    uint8_t  nPartidosGrupo;
    PartidoKO     partidosKO[TORNEO_MAX_PARTIDOS_KO];
    uint8_t  nPartidosKO;
    int8_t   partidoEnJuego;   // índice en partidosGrupo o partidosKO, -1 = ninguno
    bool     enJuegoEsKO;
    int8_t   campeon;          // índice del campeón, -1 hasta que termine
};

extern Torneo torneo;

void   torneoInit();                    // carga desde NVS al boot
bool   torneoCrear(const String nombres[], uint8_t n, uint8_t nGruposSugerido);
bool   torneoJugar(uint8_t idx, bool esKO);
bool   torneoJugarProximo();            // arranca el próximo partido según el orden armado por el sistema
bool   torneoConfirmar(uint8_t golA, uint8_t golB);
void   torneoCancelar();
String torneoEstadoJSON();

// "NombreA y NombreB" del próximo partido a jugar (salteando el que está/estuvo
// en juego) — para el anuncio "Preparense..." en la farola. false si no hay próximo.
bool   torneoProximosNombres(char* buf, size_t len);
