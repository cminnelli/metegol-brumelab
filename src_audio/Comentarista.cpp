#include "Comentarista.h"
#include "WebConfig.h"
#include "AudioVoz.h"
#include <Arduino.h>

// ── Tipos internos ────────────────────────────────────────────────────────────

enum class EstadoPartido : uint8_t {
    INICIO,
    ULTIMO_TRAMO,
    GOLEADA,
    CALIENTE,
    PAREJO,
    DEFINIDO,
    TRANQUILO
};

static const char* nombreEstado(EstadoPartido e) {
    switch (e) {
        case EstadoPartido::INICIO:       return "inicio";
        case EstadoPartido::ULTIMO_TRAMO: return "ultimo_tramo";
        case EstadoPartido::GOLEADA:      return "goleada";
        case EstadoPartido::CALIENTE:     return "caliente";
        case EstadoPartido::PAREJO:       return "parejo";
        case EstadoPartido::DEFINIDO:     return "definido";
        default:                          return "tranquilo";
    }
}

static const RangoAudio& rangoDeEstado(EstadoPartido e) {
    switch (e) {
        case EstadoPartido::INICIO:       return config.comentInicio;
        case EstadoPartido::ULTIMO_TRAMO: return config.comentUltimoTramo;
        case EstadoPartido::GOLEADA:      return config.comentGoleada;
        case EstadoPartido::CALIENTE:     return config.comentCaliente;
        case EstadoPartido::PAREJO:       return config.comentParejo;
        case EstadoPartido::DEFINIDO:     return config.comentDefinido;
        default:                          return config.comentTranquilo;
    }
}

// ── Determinación del estado ──────────────────────────────────────────────────

static EstadoPartido determinarEstado(const Partido& p) {
    uint32_t elapsed = millis() - p.inicio;
    uint8_t  diff    = (uint8_t)abs((int)p.goles[0] - (int)p.goles[1]);
    uint8_t  total   = p.goles[0] + p.goles[1];

    // 1. Fase inicio
    if (elapsed < (uint32_t)config.inicioSegs * 1000)
        return EstadoPartido::INICIO;

    // 2. Último tramo — lógica distinta según modo de juego
    if (config.modoJuego == 1) {
        // Modo tiempo: evalúa porcentaje restante
        uint32_t tiempoTotal = (uint32_t)config.duracionMin * 60000UL;
        if (tiempoTotal > elapsed) {
            uint32_t restante = tiempoTotal - elapsed;
            if (restante < (tiempoTotal * config.ultimoTramoPorc / 100))
                return EstadoPartido::ULTIMO_TRAMO;
        }
    } else {
        // Modo goles: líder necesita ≤ 1 gol para ganar
        uint8_t maxGoles = max(p.goles[0], p.goles[1]);
        if (config.golesMax > maxGoles && (config.golesMax - maxGoles) <= 1)
            return EstadoPartido::ULTIMO_TRAMO;
    }

    // 3–7. Marcador (fase media del partido)
    if (diff >= config.goleadaDiff)        return EstadoPartido::GOLEADA;
    if (total >= config.calienteGoles)     return EstadoPartido::CALIENTE;
    if (diff == 0)                         return EstadoPartido::PAREJO;
    if (diff >= 1)                         return EstadoPartido::DEFINIDO;
    return EstadoPartido::TRANQUILO;
}

// ── Helpers internos ─────────────────────────────────────────────────────────

static uint32_t _ultimoComentario = 0;

static void reproducir(const char* label, const RangoAudio& rango) {
    if (rango.desde == 0 && rango.hasta == 0) return;
    if (rango.desde > rango.hasta)             return;
    uint8_t item = (uint8_t)random(rango.desde, rango.hasta + 1);
    Serial.printf("[COMENTARIO] Estado: %-12s — ítem %d\n", label, item);
    vozPlayTrack(item);
}

static void dispararComentario(const Partido& partido) {
    EstadoPartido     estado = determinarEstado(partido);
    const RangoAudio& rango  = rangoDeEstado(estado);
    reproducir(nombreEstado(estado), rango);
}

// ── API pública ───────────────────────────────────────────────────────────────

void comentaristaLoop(const Partido& partido) {
    uint32_t intervaloMs = (uint32_t)config.intervaloComentarios * 1000UL;
    if (millis() - _ultimoComentario < intervaloMs) return;
    _ultimoComentario = millis();
    dispararComentario(partido);
}

void comentaristaOnGol(const Partido& partido) {
    reproducir("gol", config.comentGol);
    _ultimoComentario = millis();  // reinicia timer para no duplicar comentario enseguida
}
