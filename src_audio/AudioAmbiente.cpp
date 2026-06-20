#include "AudioAmbiente.h"
#include "WebConfig.h"
#include <Arduino.h>

#define AMB_TX 16
#define AMB_RX 17

static void cmd(uint8_t c, uint8_t ph, uint8_t pl) {
    uint8_t buf[10];
    buf[0]=0x7E; buf[1]=0xFF; buf[2]=0x06; buf[3]=c;
    buf[4]=0x00; buf[5]=ph;   buf[6]=pl;
    int16_t cs = -(int16_t)(buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]);
    buf[7]=(cs>>8)&0xFF; buf[8]=cs&0xFF; buf[9]=0xEF;
    Serial1.write(buf, 10);
    delay(150);
}

// ── Estado ────────────────────────────────────────────────────────────────────

enum class AmbModo : uint8_t { PARADO, NORMAL, HINCHADA, CALIENTE, GOL_REACCION };

static AmbModo  _modo          = AmbModo::PARADO;
static AmbModo  _modoAnteGol   = AmbModo::PARADO; // modo antes de GOL_REACCION
static uint8_t  _pistaActual   = 0;     // 0 = sin pista activa
static bool     _enCaliente    = false; // sesión caliente activa
static uint8_t  _calienteCount = 0;     // sesiones caliente por partido (máx 2)
static bool     _hinchadaFired = false; // hinchada ya sonó en este partido

// Transición suave: fade-out al cambiar pista
static uint8_t  _pendingVol    = 0;
static uint32_t _pendingVolAt  = 0;
static uint32_t _trackStartAt  = 0;   // para reportar duración al cambiar de pista

// ── Helpers ───────────────────────────────────────────────────────────────────

// Transición con fade-out: baja volumen, cambia pista, restaura volumen 250ms después
static void tocarConTransicion(const RangoAudio& r, const char* label, bool loop) {
    bool     fade         = (_pistaActual > 0 && _pendingVol == 0);
    uint8_t  pistaAnterior = _pistaActual;
    uint32_t durS          = 0;
    if (fade) {
        durS          = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
        _pendingVol   = config.volumenAmbiente;
        _pendingVolAt = millis() + 700;
        cmd(0x06, 0x00, 0);
    }
    uint8_t pista = r.desde + random(r.hasta - r.desde + 1);
    _pistaActual  = loop ? pista : 0;
    _trackStartAt = millis();
    cmd(0x03, 0x00, pista);
    if (loop) cmd(0x19, 0x00, 0x00);
    Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
    if (fade && durS > 0)
        Serial.printf("     %-14s pista %d  |  %lus\n", label, pista, (unsigned long)durS);
    else
        Serial.printf("     %-14s pista %d\n", label, pista);
}

// One-shot sin fade (reacciones inmediatas: gol_reaccion, hinchada)
static void tocarUnaVez(const RangoAudio& r, const char* label) {
    if (_pendingVol > 0) {
        cmd(0x06, 0x00, config.volumenAmbiente);
        _pendingVol = 0;
    }
    uint8_t pista = r.desde + random(r.hasta - r.desde + 1);
    _pistaActual  = pista;   // para que stats muestre la pista correcta
    _trackStartAt = millis();
    cmd(0x03, 0x00, pista);
    Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
    Serial.printf("     %-14s pista %d\n", label, pista);
}

// ── API pública ───────────────────────────────────────────────────────────────

void ambienteBegin() {
    Serial1.begin(9600, SERIAL_8N1, AMB_RX, AMB_TX);
}

void ambienteSetVolumen(uint8_t vol) {
    cmd(0x06, 0x00, vol);
}

void ambienteReiniciar() {
    cmd(0x0E, 0x00, 0x00);             // pausa SP2 — silencia antes de PARADO
    _modo          = AmbModo::PARADO;
    _modoAnteGol   = AmbModo::PARADO;
    _pistaActual   = 0;
    _pendingVol    = 0;
    _hinchadaFired = false;
    _enCaliente    = false;
    _calienteCount = 0;
}

void ambientePoll() {
    // Restaura volumen tras transición (no bloqueante)
    if (_pendingVol > 0 && millis() >= _pendingVolAt) {
        cmd(0x06, 0x00, _pendingVol);
        _pendingVol = 0;
    }

    static uint8_t buf[10], idx = 0;
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        if (b == 0x7E) idx = 0;
        if (idx < 10) buf[idx++] = b;
        if (idx >= 10 && buf[9] == 0xEF) {
            uint8_t tipo = buf[3], val = buf[6];
            switch (tipo) {
                case 0x3F:
                    Serial.println("\n[SPK2:AMB] SD online ✓");
                    break;
                case 0x3D: {
                    if (_modo == AmbModo::PARADO) break;
                    uint32_t durS = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
                    if (_modo == AmbModo::GOL_REACCION) {
                        if (_modoAnteGol == AmbModo::HINCHADA) {
                            // venía de hinchada → retomar
                            _modo = AmbModo::HINCHADA;
                            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                            Serial.printf("     gol_reaccion fin  |  %lus  →  hinchada (retoma)\n", (unsigned long)durS);
                            tocarUnaVez(config.hinchadaMusica, "hinchada (retoma)");
                        } else if (!_hinchadaFired) {
                            _hinchadaFired = true;
                            _modo = AmbModo::HINCHADA;
                            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                            Serial.printf("     gol_reaccion fin  |  %lus  →  hinchada\n", (unsigned long)durS);
                            tocarUnaVez(config.hinchadaMusica, "hinchada (once)");
                        } else {
                            _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                            const RangoAudio& r = (_modo == AmbModo::CALIENTE) ? config.momentoCaliente : config.ambienteGenerico;
                            const char*       lbl = (_modo == AmbModo::CALIENTE) ? "caliente" : "ambiente";
                            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                            Serial.printf("     gol_reaccion fin  |  %lus  →  %s\n", (unsigned long)durS, lbl);
                            tocarConTransicion(r, lbl, true);
                        }
                    } else if (_modo == AmbModo::HINCHADA) {
                        _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                        const RangoAudio& r = (_modo == AmbModo::CALIENTE) ? config.momentoCaliente : config.ambienteGenerico;
                        const char*       lbl = (_modo == AmbModo::CALIENTE) ? "caliente" : "ambiente";
                        Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                        Serial.printf("     hinchada fin  |  %lus  →  %s\n", (unsigned long)durS, lbl);
                        tocarConTransicion(r, lbl, true);
                    }
                    // NORMAL/CALIENTE: 0x19 loopea automáticamente — ignorar
                    break;
                }
                case 0x40:
                    Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                    Serial.printf("     ✗ Error 0x%02X  pista:%d\n", val, _pistaActual);
                    _pistaActual = 0;   // watchdog lo reiniciará
                    break;
                default: break;
            }
            idx = 0;
        }
    }
}

void ambienteActualizar(bool activo, bool esCaliente) {
    if (!activo) return;   // deja que SP2 siga; ambienteReiniciar() lo para
    if (_modo == AmbModo::GOL_REACCION || _modo == AmbModo::HINCHADA) return;

    // Entrar en CALIENTE (máx 2 veces por partido, solo si el partido está caliente)
    if (esCaliente && !_enCaliente && _calienteCount < 2) {
        _calienteCount++;
        _enCaliente = true;
        _modo       = AmbModo::CALIENTE;
        tocarConTransicion(config.momentoCaliente, "caliente", true);
        return;
    }

    // Salir de CALIENTE cuando el partido se enfría
    if (!esCaliente && _enCaliente) {
        _enCaliente = false;
        if (_modo == AmbModo::CALIENTE) {
            _modo = AmbModo::NORMAL;
            tocarConTransicion(config.ambienteGenerico, "ambiente", true);
        }
        return;
    }

    // Arrancar NORMAL al iniciar el partido
    if (_modo == AmbModo::PARADO) {
        _modo = AmbModo::NORMAL;
        tocarConTransicion(config.ambienteGenerico, "ambiente", true);
        return;
    }

    // Watchdog: reinicia si no hay pista activa (ej. error SP2 o vuelta de gol_reaccion)
    if (_pistaActual == 0) {
        const RangoAudio& r = (_modo == AmbModo::CALIENTE) ? config.momentoCaliente : config.ambienteGenerico;
        const char* lbl     = (_modo == AmbModo::CALIENTE) ? "caliente (wd)" : "ambiente (wd)";
        tocarConTransicion(r, lbl, true);
    }
}

void ambienteOnGol() {
    if (_modo == AmbModo::PARADO) return;
    _modoAnteGol = _modo;
    _modo = AmbModo::GOL_REACCION;
    _pistaActual = 0;
    if (_pendingVol > 0) { cmd(0x06, 0x00, config.volumenAmbiente); _pendingVol = 0; }
    uint8_t pista = config.ambienteGol.desde + random(config.ambienteGol.hasta - config.ambienteGol.desde + 1);
    _trackStartAt = millis();
    cmd(0x03, 0x00, pista);
    Serial.printf("     SPK2-AMB   gol_reaccion   pista %d\n", pista);
}

const char* ambienteGetEstado() {
    switch (_modo) {
        case AmbModo::GOL_REACCION: return "gol_reaccion";
        case AmbModo::CALIENTE:     return "caliente";
        case AmbModo::HINCHADA:     return "hinchada";
        case AmbModo::NORMAL:       return "ambiente";
        default:                    return "parado";
    }
}

uint8_t ambienteGetPista() {
    return _pistaActual;
}
