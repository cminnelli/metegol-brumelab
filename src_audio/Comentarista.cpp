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
        case EstadoPartido::ULTIMO_TRAMO:     return config.comentUltimoTramo;
        case EstadoPartido::GOLEADA:          return config.comentGoleada;
        case EstadoPartido::CALIENTE:         return config.comentCaliente;
        case EstadoPartido::PAREJO:           return config.comentParejo;
        case EstadoPartido::DEFINIDO:         return config.comentDefinido;
        case EstadoPartido::ABURRIDO:         return config.comentAburrido;
        default:                              return config.comentTranquilo;
    }
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

// ── Shuffle sin reposición por rango ─────────────────────────────────────────

struct RangoState { uint8_t desde; uint16_t usados; };
static RangoState _rangoStates[16];
static uint8_t    _nRangoStates = 0;

static uint16_t* getUsados(const RangoAudio& rango) {
    for (uint8_t i = 0; i < _nRangoStates; i++) {
        if (_rangoStates[i].desde == rango.desde) return &_rangoStates[i].usados;
    }
    if (_nRangoStates < 16) {
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
                       const RangoAudio& rango, int8_t hwOffset = 1, bool asBlock = true) {
    if (rango.desde == 0 && rango.hasta == 0) return;
    if (rango.desde > rango.hasta)             return;

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

    uint8_t idx  = avail[random(0, count)];
    uint8_t item = rango.desde + idx;
    if (usados) *usados |= (1u << idx);

    if (asBlock) {
        Serial.printf("\n──── SPK1 - COMENTARIO  ─────────────────────\n");
        Serial.printf("     %-14s [%d-%d]  →  pista %d\n", label, rango.desde, rango.hasta, item);
    } else {
        Serial.printf("     SPK1-VOZ   %-12s [%d-%d]  →  pista %d\n", label, rango.desde, rango.hasta, item);
    }
    vozPlayTrack((uint8_t)(item + hwOffset));
}

static void dispararComentario(const Partido& partido) {
    EstadoPartido     estado = determinarEstado(partido);
    const RangoAudio& rango  = rangoDeEstado(estado);
    reproducir("COMENTARIO", nombreEstado(estado), rango, 1);
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
        reproducir("COMENTARIO", "inicio", config.comentInicio, 1);
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

    // Indicador visual del estado
    const char* icono = "";
    if      (strcmp(est, "caliente")     == 0) icono = " ♨";
    else if (strcmp(est, "goleada")      == 0) icono = " !!";
    else if (strcmp(est, "ultimo_tramo") == 0) icono = " !!";
    else if (strcmp(est, "aburrido")     == 0) icono = " zz";

    Serial.printf("\n──── %02lu:%02lu  [STATS]  ───────────────────────\n",
        (unsigned long)mm, (unsigned long)ss);
    Serial.printf("     %d  ─  %d   |  %s%s  [%s]\n",
        partido.goles[0], partido.goles[1], est, icono, modo);
    Serial.printf("     goles:%d  diff:%d  |  SP2: %s p:%d\n",
        total, diff, ambienteGetEstado(), ambienteGetPista());
}
