#include "Comentarista.h"
#include "WebConfig.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include <Arduino.h>

// ── Tipos internos ────────────────────────────────────────────────────────────

enum class EstadoPartido : uint8_t {
    INICIO, PRIMEROS_MINUTOS, ULTIMO_TRAMO,
    GOLEADA, CALIENTE, PAREJO, DEFINIDO, ABURRIDO, TRANQUILO
};

static const char* nombreEstado(EstadoPartido e) {
    switch (e) {
        case EstadoPartido::INICIO:           return "inicio";
        case EstadoPartido::PRIMEROS_MINUTOS: return "primeros_min";
        case EstadoPartido::ULTIMO_TRAMO:     return "ultimo_tramo";
        case EstadoPartido::GOLEADA:          return "goleada";
        case EstadoPartido::CALIENTE:         return "caliente";
        case EstadoPartido::PAREJO:           return "parejo";
        case EstadoPartido::DEFINIDO:         return "definido";
        case EstadoPartido::ABURRIDO:         return "aburrido";
        default:                              return "tranquilo";
    }
}

static const RangoAudio& rangoDeEstado(EstadoPartido e) {
    switch (e) {
        case EstadoPartido::INICIO:           return config.comentInicio;
        case EstadoPartido::PRIMEROS_MINUTOS: return config.comentPrimerosMins;
        case EstadoPartido::GOLEADA:          return config.comentGoleada;
        case EstadoPartido::CALIENTE:         return config.comentCaliente;
        case EstadoPartido::PAREJO:           return config.comentParejo;
        case EstadoPartido::DEFINIDO:         return config.comentDefinido;
        case EstadoPartido::ABURRIDO:         return config.comentAburrido;
        default:                              return config.comentTranquilo;
    }
}

static const RangoAudio& rangoUltimoTramoActual(const Partido& p) {
    uint8_t diff  = (uint8_t)abs((int)p.goles[0] - (int)p.goles[1]);
    uint8_t total = p.goles[0] + p.goles[1];
    if (diff >= config.goleadaDiff)   return config.comentUltimoTramoGoleada;
    if (diff == 0 && total > 0)       return config.comentUltimoTramoEmpateGoles;
    if (diff == 0)                    return config.comentUltimoTramoAburrido;
    return config.comentUltimoTramoAjustado;
}

// ── Determinación del estado ──────────────────────────────────────────────────

static uint32_t _inicioFiredAt = 0;

// Tiempo máximo desde el último gol para que el partido siga siendo "caliente"
#define CALIENTE_RECIENTE_MS 45000UL

static EstadoPartido determinarEstado(const Partido& p) {
    uint32_t now     = millis();
    uint32_t elapsed = now - p.inicio;
    uint8_t  diff    = (uint8_t)abs((int)p.goles[0] - (int)p.goles[1]);
    uint8_t  total   = p.goles[0] + p.goles[1];

    // ── 1. Primeros minutos ────────────────────────────────────────────────────
    // Dura primerosMinsSegs a partir del comentario de inicio
    uint32_t refInicio = (_inicioFiredAt > 0) ? _inicioFiredAt : p.inicio;
    if ((now - refInicio) < (uint32_t)config.primerosMinsSegs * 1000UL)
        return EstadoPartido::PRIMEROS_MINUTOS;

    // ── 2. Último tramo — siempre gana sobre el marcador ──────────────────────
    if (config.modoJuego == 1) {
        uint32_t tiempoTotal = (uint32_t)config.duracionMin * 60000UL;
        if (tiempoTotal > elapsed && (tiempoTotal - elapsed) < (uint32_t)config.ultimoTramoSegs * 1000UL)
            return EstadoPartido::ULTIMO_TRAMO;
    } else {
        uint8_t maxGoles = max(p.goles[0], p.goles[1]);
        if (config.golesMax > maxGoles && (config.golesMax - maxGoles) <= 1)
            return EstadoPartido::ULTIMO_TRAMO;
    }

    // ── 3. Goleada — diferencia aplastante ────────────────────────────────────
    if (diff >= config.goleadaDiff)
        return EstadoPartido::GOLEADA;

    // ── 4. Caliente — partido vivo: muchos goles + apretado + acción reciente ─
    uint32_t tiempoDesdeGol = (total > 0) ? (now - p.ultimoGol) : 0xFFFFFFFFUL;
    if (total >= config.calienteGoles
        && diff < config.goleadaDiff
        && tiempoDesdeGol < CALIENTE_RECIENTE_MS)
        return EstadoPartido::CALIENTE;

    // ── 5. Aburrido — sin actividad desde que terminaron los primeros minutos ─
    uint32_t normalStart  = refInicio + (uint32_t)config.primerosMinsSegs * 1000UL;
    uint32_t tiempoSinGol = (total == 0)
        ? (now > normalStart ? now - normalStart : 0)
        : tiempoDesdeGol;
    if (tiempoSinGol > (uint32_t)config.umbralAburridoSegs * 1000UL)
        return EstadoPartido::ABURRIDO;

    // ── 6–8. Basado en el marcador ────────────────────────────────────────────
    if (diff == 0) return EstadoPartido::PAREJO;
    if (diff == 1) return EstadoPartido::TRANQUILO;
    return EstadoPartido::DEFINIDO;   // diff 2..goleadaDiff-1
}

// ── Selección contextual de gol ───────────────────────────────────────────────

// Chequea ULTIMO_TRAMO directamente sin pasar por determinarEstado
// (evita que ABURRIDO u otros estados con mayor prioridad lo pisen)
static bool esUltimoTramo(const Partido& p) {
    if (config.modoJuego == 1) {
        uint32_t elapsed     = millis() - p.inicio;
        uint32_t tiempoTotal = (uint32_t)config.duracionMin * 60000UL;
        if (tiempoTotal > elapsed) {
            uint32_t restante = tiempoTotal - elapsed;
            return restante < (uint32_t)config.ultimoTramoSegs * 1000UL;
        }
        return false;
    } else {
        uint8_t maxGoles = max(p.goles[0], p.goles[1]);
        return (config.golesMax > maxGoles && (config.golesMax - maxGoles) <= 1);
    }
}

struct TipoGol { const RangoAudio& rango; const char* nombre; };

static TipoGol seleccionarTipoGol(const Partido& p) {
    bool    esEmpate  = (p.goles[0] == p.goles[1]);
    bool    esUltimoT = esUltimoTramo(p);
    uint8_t diff      = (uint8_t)abs((int)p.goles[0] - (int)p.goles[1]);
    uint8_t total     = p.goles[0] + p.goles[1];

    if (esUltimoT && esEmpate)                                       return {config.golAgonicoEmpate, "agonico_empate"};
    if (esUltimoT)                                                   return {config.golAgonico,        "agonico"};
    if (esEmpate)                                                    return {config.golEmpate,         "empate"};
    if (total >= config.calienteGoles && diff < config.goleadaDiff) return {config.golCaliente,       "caliente"};
    if (diff >= config.goleadaDiff)                                  return {config.golEfusivo,        "efusivo"};
    return {config.golNormal, "normal"};
}

// ── Frases por pista (para diagnóstico en Serial) ─────────────────────────────

static const char* const kFrases[112] = {
    nullptr,                                                                             // 0
    "¡Arranca el partido, señoras y señores!",                                           // 001 INICIO
    "¡Que ruede la pelota, señoras y señores!",                                          // 002
    "¡Ya arrancó el partido, que viva el fútbol!",                                       // 003
    "¡Ya se juega este duelo que promete emociones!",                                    // 004
    "¡Empieza la historia, veremos quién la escribe mejor!",                             // 005
    "¡Empieza la batalla en este gran partido!",                                         // 006
    "Se siguen estudiando en los primeros minutos.",                                     // 007 PRIMEROS_MINUTOS
    "Esto recién empieza, pero ya se vive clima de partido!",                            // 008
    "Nadie regala nada, esto recién arranca!",                                           // 009
    "Todavía nadie muestra las cartas!...",                                              // 010
    "Está claro que nadie quiere cometer el primer error.",                              // 011
    "Mucha cautela en los primeros minutos.",                                            // 012
    "Nadie regala absolutamente nada.",                                                  // 013 PAREJO
    "Esto está para cualquiera!",                                                        // 014
    "Partido chivo, durísimo.",                                                          // 015
    "No se sacan diferencias.",                                                          // 016
    "Un error puede cambiar toda la historia.",                                          // 017
    "Está más cerrado que banco un feriado.",                                            // 018
    "¡Mamita querida como está este partido!",                                           // 019 CALIENTE
    "¡Este partido va a quedar en la historia!",                                         // 020
    "¡No se regalan absolutamente nada!",                                                // 021
    "¡El partido está al rojo vivo!",                                                    // 022
    "¡Esto es una locura hermosa!",                                                      // 023
    "¡Partidazo con todas las letras!",                                                  // 024
    "Los están pasando por arriba.",                                                     // 025 GOLEADA
    "Hay uno que juega y otro que no ve la hora que termine.",                           // 026
    "La diferencia empieza a ser demasiado grande.",                                     // 027
    "La remontada ya parece imposible!",                                                 // 028
    "Está sacando chapa de candidato.",                                                  // 029
    "Pinta para goleada de las que duelen.",                                             // 030
    "Salvo un milagro, empieza a cerrarse la historia.",                                 // 031 DEFINIDO
    "La victoria está cada vez más cerca.",                                              // 032
    "Ya se empieza a bajar la persiana.",                                                // 033
    "Están controlando el partido con autoridad.",                                       // 034
    "Claramente este partido ya tiene dueño.",                                           // 035
    "Hace falta un milagro para cambiar esto!",                                          // 036
    "¡Entramos en zona de definición!",                                                  // 037 ULTIMO_TRAMO (inactivo)
    "¡Se vienen los segundos más calientes!",                                            // 038
    "¡Ahora sí, se juega el todo por el todo!",                                         // 039
    "¡No queda prácticamente nada!",                                                     // 040
    "¡Se viene un cierre para el infarto!",                                              // 041
    "¡Estamos en la recta final del partido!",                                           // 042
    "Mucha lucha, poco fútbol.",                                                         // 043 ABURRIDO
    "Parece que arqueros están de espectadores.",                                        // 044
    "Le está faltando pimienta al partido.",                                             // 045
    "Mucho estudio, pocas emociones.",                                                   // 046
    "Che, me parece que los dos cuidan más de lo que arriesgan.",                        // 047
    "Este partido más trabado que trámite en la municipalidad.",                         // 048
    "Pocas situaciones claras por ahora.",                                               // 049 TRANQUILO
    "Momento de calma en el partido.",                                                   // 050
    "Bajó un cambio el encuentro.",                                                      // 051
    "Los dos intentan acomodarse.",                                                      // 052
    "Circula la pelota, pero sin profundidad.",                                          // 053
    "Partido controlado por ahora.",                                                     // 054
    "¡Gol! ¡Gol! ¡Gol!",                                                               // 055 GOL_NORMAL
    "¡Gol! ¡La mandó a guardar!",                                                        // 056
    "¡Gol! ¡No perdonó!",                                                               // 057
    "¡Gol y a sacar del medio!",                                                         // 058
    "¡GOOOOOOOOOOOL! ¡Qué locura!",                                                     // 059 GOL_EFUSIVO
    "¡GOOOOOOOOOOOL! ¡Tremendo golazo!",                                                 // 060
    "¡GOOOOOOOOOOOL! ¡Se viene abajo el estadio!",                                       // 061
    "¡GOOOOOOOOOOOL! ¡Pero qué golazo, por favor!",                                     // 062
    "¡Golazo! ¡Empate! ¡Empate señores!",                                               // 063 GOL_EMPATE
    "¡Gooooooool! ¡Y lo empata cuando parecía imposible!",                              // 064
    "¡Golazo! ¡Vuelve todo a emparejarse!",                                             // 065
    "¡Gooooooool! ¡Empieza otro partido!",                                              // 066
    "¡GOOOOOOL! ¡No terminaron de festejar uno y llegó el otro!",                       // 067 GOL_CALIENTE
    "¡GOOOOOOL! ¡Que pedazo de golazo por dios!",                                       // 068
    "¡GOOOOOOL! ¡Esto no da respiro!",                                                  // 069
    "¡GOOOOOOL! ¡Partido recontra caliente!",                                           // 070
    "¡GOOOOOOL! ¡SOBRE LA HORA!",                                                       // 071 GOL_AGONICO
    "¡GOOOOOOL! ¡Cuando el partido parecía que se moría!",                              // 072
    "¡GOOOOOOL! ¡Gol agónico, señoras y señores!",                                      // 073
    "¡GOOOOOOL! ¡EMPATE AGÓNICO! ¡NO LO PUEDO CREER!",                                 // 074 GOL_AGONICO_EMPATE
    "¡GOOOOOOL! ¡Lo empató en una de las últimas del partido!",                         // 075
    "¡GOOOOOOL! ¡Rescató un empate imposible!",                                         // 076
    "Silbato inicio",                                                                    // 077 PITIDO_INICIO
    "Silbato inicio",                                                                    // 078
    "Silbato final",                                                                     // 079 PITIDO_FINAL
    "Silbato final",                                                                     // 080
    "Partido tan parejo que cualquier resultado hubiese parecido injusto.",              // 081 FINAL_EMPATE
    "Más equilibrado que balanza de farmacia.",                                          // 082
    "Lo dominó de punta a punta.",                                                       // 083 FINAL_APLASTANTE
    "La de un baile bárbaro.",                                                           // 084
    "Lo ganó porque alguien tenía que ganarlo.",                                         // 085 FINAL_AJUSTADA
    "Se definió por un detalle.",                                                        // 086
    "Sin hacer ruido, hizo el trabajo y se llevó el premio.",                            // 087 FINAL_NORMAL
    "Sin lujos ni milagros. Hizo los deberes y se quedó con los puntos.",                // 088
    "¡Entramos en zona de definición y esto sigue palo a palo!",                        // 089 UT_EMPATE_GOLES
    "¡Se vienen los segundos más calientes de un partido lleno de goles!",              // 090
    "¡Ahora sí, cualquiera de los dos lo puede ganar!",                                 // 091
    "¡No queda prácticamente nada y siguen sin sacarse diferencias!",                   // 092
    "¡Se viene un cierre para el infarto en un partido completamente abierto!",         // 093
    "¡Estamos en la recta final y la mete gana!",                                       // 094
    "¡Entramos en zona de definición y la remontada parece imposible!",                 // 095 UT_GOLEADA
    "¡Se vienen los últimos segundos y buscan liquidarlo por completo!",                // 096
    "¡Ahora sí, estamos cerca del final y hay un claro dominador!",                     // 097
    "¡No queda prácticamente nada y la ventaja sigue siendo muy amplia!",               // 098
    "¡Se viene el cierre de un partido que tiene un claro ganador!",                    // 099
    "¡Se acerca el cierre del partido y todo indica que la historia ya está escrita!",  // 100
    "¡Entramos en zona de definición y esto sigue todo o nada!",                        // 101 UT_AJUSTADO
    "¡Se vienen los segundos más calientes de todo el partido!",                        // 102
    "¡Ahora sí, se juegan todo para empatarlo!",                                        // 103
    "¡No queda prácticamente nada y la diferencia sigue siendo mínima!",                // 104
    "¡Se viene un cierre para el infarto y un gol lo cambia todo!",                     // 105
    "¡Estamos en la recta final y un solo gol puede cambiarlo todo!",                   // 106
    "¡Entramos en zona de definición buscando una emoción que todavía no aparece!",     // 107 UT_ABURRIDO
    "¡Se vienen los últimos segundos y cualquiera que se despierte puede ganar!",       // 108
    "¡Recta final del partido ahora sí, no queda margen para el error!",                // 109
    "¡Se viene el último tramo de un partido más cerrado que negocio en feriado!",      // 110
    "¡Estamos en la recta final y una jugada puede cambiarlo todo!",                    // 111
};

static const char* fraseDePista(uint8_t pista) {
    if (pista == 0 || pista > 111) return nullptr;
    return kFrases[pista];
}

// ── Shuffle sin reposición por rango ─────────────────────────────────────────

struct RangoState { uint8_t desde; uint16_t usados; };
static RangoState _rangoStates[24];
static uint8_t    _nRangoStates = 0;

static uint16_t* getUsados(const RangoAudio& rango) {
    for (uint8_t i = 0; i < _nRangoStates; i++) {
        if (_rangoStates[i].desde == rango.desde) return &_rangoStates[i].usados;
    }
    if (_nRangoStates < 24) {
        _rangoStates[_nRangoStates] = {rango.desde, 0};
        return &_rangoStates[_nRangoStates++].usados;
    }
    return nullptr;
}

// ── Helpers internos ─────────────────────────────────────────────────────────

static uint32_t _proximoComentario = 0;
static bool     _iniciado          = false;
static bool     _inicioPendiente   = false;
static uint32_t _finPendienteEn    = 0;
static uint8_t  _golesFinales[2]   = {0, 0};

// asBlock=true  → bloque separador (comentarios, final)
// asBlock=false → línea hija sin separador (dentro del bloque GOL)
static void reproducir(const char* prefix, const char* label,
                       const RangoAudio& rango, int8_t hwOffset = 0, bool asBlock = true) {
    if (rango.desde == 0) {
        Serial.printf("\n[WARN] SPK1: rango '%s' desde=0 — configurá desde≥1 en la web\n", label);
        return;
    }
    if (rango.desde > rango.hasta) return;

    uint8_t   size     = rango.hasta - rango.desde + 1;
    uint16_t* usados   = getUsados(rango);
    uint16_t  fullMask = (uint16_t)((1u << size) - 1);

    if (!usados || *usados == fullMask) {
        if (usados) *usados = 0;
    }

    uint8_t avail[16];
    uint8_t count = 0;
    for (uint8_t i = 0; i < size; i++) {
        if (!usados || !(*usados & (1u << i))) avail[count++] = i;
    }
    if (count == 0) return;

    uint8_t idx   = avail[random(0, count)];
    uint8_t item  = rango.desde + idx;
    uint8_t pista = (uint8_t)(item + hwOffset);
    if (usados) *usados |= (1u << idx);

    const char* frase = fraseDePista(item);
    if (asBlock) {
        Serial.printf("\n──── SPK1 - COMENTARIO  ─────────────────────\n");
        Serial.printf("     %-14s [%d-%d]  →  pista %d\n", label, rango.desde, rango.hasta, item);
        if (frase) Serial.printf("     %04d  %s\n", item, frase);
    } else {
        Serial.printf("     SPK1-VOZ   %-12s [%d-%d]  →  pista %d\n", label, rango.desde, rango.hasta, item);
        if (frase) Serial.printf("     %04d  %s\n", item, frase);
    }
    // El gol (asBlock=false) pisa cualquier comentario en curso con un fade breve;
    // el resto de los comentarios esperan su turno (vozIsBusy) antes de sonar.
    if (asBlock) vozPlayTrack(pista);
    else         vozPlayTrackPrioritario(pista);
}

static void dispararComentario(const Partido& partido) {
    EstadoPartido      estado = determinarEstado(partido);
    const RangoAudio*  rango;
    const char*        label = nombreEstado(estado);

    if (estado == EstadoPartido::ULTIMO_TRAMO) {
        rango = &rangoUltimoTramoActual(partido);
        uint8_t diff  = (uint8_t)abs((int)partido.goles[0] - (int)partido.goles[1]);
        uint8_t total = partido.goles[0] + partido.goles[1];
        label = (diff >= config.goleadaDiff)  ? "ut:goleada"
              : (diff == 0 && total > 0)      ? "ut:empate_goles"
              : (diff == 0)                   ? "ut:aburrido"
                                              : "ut:ajustado";
    } else {
        rango = &rangoDeEstado(estado);
    }
    reproducir("COMENTARIO", label, *rango, 0);
}

static void programarProximo() {
    uint32_t minMs = (uint32_t)config.intervaloComentariosMin * 1000UL;
    uint32_t maxMs = (uint32_t)config.intervaloComentariosMax * 1000UL;
    if (maxMs < minMs) maxMs = minMs;
    _proximoComentario = millis() + (uint32_t)random(minMs, maxMs + 1);
}

// ── API pública ───────────────────────────────────────────────────────────────

void comentaristaLoop(const Partido& partido) {
    // Comentario de final pendiente (se dispara 4s después del pitido final)
    if (_finPendienteEn > 0 && millis() >= _finPendienteEn) {
        _finPendienteEn = 0;
        uint8_t diff = (uint8_t)abs((int)_golesFinales[0] - (int)_golesFinales[1]);
        bool    empate = (_golesFinales[0] == _golesFinales[1]);
        const RangoAudio* r;
        const char*       label;
        if (empate) {
            r = &config.finalEmpate;      label = "final_empate";
        } else if (diff >= config.goleadaDiff) {
            r = &config.finalAplastante;  label = "final_aplastante";
        } else if (diff == 1) {
            r = &config.finalAjustada;    label = "final_ajustada";
        } else {
            r = &config.finalNormal;      label = "final_normal";
        }
        reproducir("COMENTARIO", label, *r, 0);
    }

    if (!partido.activo) {
        if (!partido.pausado) {
            // Fin real (terminado o en espera) — resetea para próxima partida
            _iniciado        = false;
            _inicioPendiente = false;
        }
        return;
    }
    if (!_iniciado) {
        _iniciado          = true;
        _inicioPendiente   = true;
        _proximoComentario = millis() + 500UL;   // dispara rápido — vozIsBusy espera que termine el pitido
        return;
    }
    if (millis() < _proximoComentario) return;

    if (_inicioPendiente) {
        if (vozIsBusy()) { _proximoComentario = millis() + 2000UL; return; }
        _inicioPendiente = false;
        _inicioFiredAt   = millis();
        reproducir("COMENTARIO", "inicio", config.comentInicio, 0);
        programarProximo();
        return;
    }
    // Espera a que terminen: SP2 reacción gol Y SP1 comentario de gol (+ 2s margen)
    if (strcmp(ambienteGetEstado(), "gol_reaccion") == 0 || vozIsBusy()) {
        _proximoComentario = millis() + 2000UL;
        return;
    }
    dispararComentario(partido);
    programarProximo();
}

void comentaristaReiniciar() {
    _iniciado        = false;
    _inicioPendiente = false;
    _inicioFiredAt   = 0;
    _finPendienteEn  = 0;
    _nRangoStates    = 0;   // limpia historial de shuffle entre partidos
}

void comentaristaFinalPartido(const Partido& partido) {
    _golesFinales[0] = partido.goles[0];
    _golesFinales[1] = partido.goles[1];
    _finPendienteEn  = millis() + 4000UL;
}

void comentaristaOnGol(const Partido& partido) {
    TipoGol tg = seleccionarTipoGol(partido);
    reproducir("COMENTARIO", tg.nombre, tg.rango, 0, false);  // línea hija del bloque GOL
    _proximoComentario = millis() + (uint32_t)config.intervaloComentariosMin * 1000UL;
}

const char* comentaristaGetEstado(const Partido& partido) {
    if (!partido.activo) {
        if (partido.pausado)   return "pausado";
        if (partido.terminado) return "terminado";
        return "en_espera";
    }
    return nombreEstado(determinarEstado(partido));
}

void comentaristaStats(const Partido& partido) {
    if (!partido.activo) return;
    uint32_t elapsed  = millis() - partido.inicio;
    uint32_t mm       = elapsed / 60000;
    uint32_t ss       = (elapsed % 60000) / 1000;
    EstadoPartido e   = determinarEstado(partido);
    uint8_t diff      = (uint8_t)abs((int)partido.goles[0] - (int)partido.goles[1]);
    uint8_t total     = partido.goles[0] + partido.goles[1];
    const char* modo  = config.modoJuego == 0 ? "GOLES" : "TIEMPO";
    const char* est   = nombreEstado(e);

    const char* sub = "";
    if (e == EstadoPartido::ULTIMO_TRAMO) {
        sub = (diff >= config.goleadaDiff)   ? ":goleada"
            : (diff == 0 && total > 0)       ? ":empate_goles"
            : (diff == 0)                    ? ":aburrido"
                                             : ":ajustado";
    }

    Serial.printf("\n──── %02lu:%02lu  [STATS: %s%s]  [%s]\n",
        (unsigned long)mm, (unsigned long)ss, est, sub, modo);
    Serial.printf("     %d  ─  %d   |  goles:%d  diff:%d  |  SP2: %s p:%d\n",
        partido.goles[0], partido.goles[1], total, diff, ambienteGetEstado(), ambienteGetPista());
}
