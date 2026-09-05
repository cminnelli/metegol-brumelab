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
static uint8_t  _golesPartido  = 0;    // goles totales del partido (para trigger hinchada)

// Transición suave: fade-out al cambiar pista
static uint8_t  _pendingVol    = 0;
static uint32_t _pendingVolAt  = 0;
static uint32_t _trackStartAt  = 0;   // para reportar duración al cambiar de pista

// ── Helpers ───────────────────────────────────────────────────────────────────

// Fade-out/in reales: suben/bajan el volumen en pasos (no un corte/salto
// instantáneo). Cada paso pasa por cmd(), que ya tiene su propio delay(150).
static void fadeOutAmbiente() {
    for (int16_t v = (int16_t)config.volumenAmbiente - 5; v > 0; v -= 5) {
        cmd(0x06, 0x00, (uint8_t)v);
    }
    cmd(0x06, 0x00, 0);
}

static void fadeInAmbiente(uint8_t target) {
    for (int16_t v = 5; v < (int16_t)target; v += 5) {
        cmd(0x06, 0x00, (uint8_t)v);
    }
    cmd(0x06, 0x00, target);
}

// Transición con fade real de ida y vuelta: baja volumen gradualmente, cambia
// de pista, deja asentar 300ms, y sube el volumen gradual de nuevo — en vez de
// un salto instantáneo a volumen completo (sonaba como si arrancara "bajito").
// loop=false se usa para pistas de un solo disparo (hinchada) que igual quieren el fade
// al entrar — _pistaActual se guarda igual para que las stats muestren la pista real.
static void tocarConTransicion(const RangoAudio& r, const char* label, bool loop) {
    bool     fade = (_pistaActual > 0 && _pendingVol == 0);
    uint32_t durS = 0;
    if (fade) {
        durS = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
        fadeOutAmbiente();
    }
    uint8_t pista = r.desde + random(r.hasta - r.desde + 1);
    _pistaActual  = pista;
    _trackStartAt = millis();
    cmd(0x03, 0x00, pista);
    if (loop) cmd(0x19, 0x00, 0x00);
    if (fade) {
        delay(300);   // deja asentar la pista nueva antes de subir el volumen
        // El ambiente genérico puede estar grabado más flojo que la reacción de
        // gol/hinchada — este boost (parametrizable en Ajustes) lo compensa.
        int16_t target = (int16_t)config.volumenAmbiente;
        if (&r == &config.ambienteGenerico) target += config.ambienteGenericoBoost;
        target = constrain(target, 0, 30);
        fadeInAmbiente((uint8_t)target);
    }
    Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
    if (fade && durS > 0)
        Serial.printf("     %-14s pista %d  |  %lus\n", label, pista, (unsigned long)durS);
    else
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
    Serial.printf("\n──── SP2  REINICIAR  [%s  p:%d]\n", ambienteGetEstado(), _pistaActual);
    if (_pendingVol > 0) {
        cmd(0x06, 0x00, config.volumenAmbiente);  // restaura volumen antes de parar
    }
    cmd(0x0E, 0x00, 0x00);             // pausa SP2
    _modo          = AmbModo::PARADO;
    _modoAnteGol   = AmbModo::PARADO;
    _pistaActual   = 0;
    _pendingVol    = 0;
    _hinchadaFired = false;
    _golesPartido  = 0;
    _enCaliente    = false;
    _calienteCount = 0;
}

void ambientePoll() {
    // Restaura volumen tras transición (no bloqueante)
    if (_pendingVol > 0 && millis() >= _pendingVolAt) {
        cmd(0x06, 0x00, _pendingVol);
        _pendingVol = 0;
    }

    // Watchdogs de timeout de gol_reaccion/hinchada — corren siempre (no solo
    // con partido.activo). Si el gol que dispara la hinchada es el que termina
    // el partido, partido.activo ya está en false para cuando esto se evalúa,
    // así que estos chequeos no pueden depender de ese flag: si dependieran,
    // el ambiente quedaba trabado en hinchada para siempre tras el pitido final,
    // porque no hay ningún otro evento (próximo gol, próximo partido) que lo saque.
    if (_modo == AmbModo::GOL_REACCION) {
        if (millis() - _trackStartAt > (uint32_t)config.golReaccionTimeoutSegs * 1000UL) {
            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
            Serial.printf("     gol_reaccion timeout → forzando salida\n");
            if (_modoAnteGol == AmbModo::HINCHADA) {
                _modo = AmbModo::HINCHADA;
                tocarConTransicion(config.hinchadaMusica, "hinchada (retoma, wd)", false);
            } else if (!_hinchadaFired && _golesPartido >= config.hinchadaGol) {
                _hinchadaFired = true;
                _modo = AmbModo::HINCHADA;
                tocarConTransicion(config.hinchadaMusica, "hinchada (once, wd)", false);
            } else {
                _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                const RangoAudio& r = _enCaliente ? config.momentoCaliente : config.ambienteGenerico;
                const char*     lbl = _enCaliente ? "caliente (wd)" : "ambiente (wd)";
                tocarConTransicion(r, lbl, true);
            }
        }
    } else if (_modo == AmbModo::HINCHADA) {
        if (millis() - _trackStartAt > (uint32_t)config.hinchadaTimeoutSegs * 1000UL) {
            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
            Serial.printf("     hinchada timeout → forzando salida\n");
            _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
            const RangoAudio& r = _enCaliente ? config.momentoCaliente : config.ambienteGenerico;
            const char*     lbl = _enCaliente ? "caliente (wd)" : "ambiente (wd)";
            tocarConTransicion(r, lbl, true);
        }
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
                    Serial.printf("\n[ELEC] SP2: reset  [modo:%s  p:%d]\n", ambienteGetEstado(), _pistaActual);
                    cmd(0x06, 0x00, config.volumenAmbiente);  // restaura volumen tras reset
                    if (_modo == AmbModo::GOL_REACCION || _modo == AmbModo::HINCHADA) {
                        _modo        = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                        _pistaActual = 0;
                    }
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
                            tocarConTransicion(config.hinchadaMusica, "hinchada (retoma)", false);
                        } else if (!_hinchadaFired && _golesPartido >= config.hinchadaGol) {
                            _hinchadaFired = true;
                            _modo = AmbModo::HINCHADA;
                            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                            Serial.printf("     gol_reaccion fin  |  %lus  →  hinchada\n", (unsigned long)durS);
                            tocarConTransicion(config.hinchadaMusica, "hinchada (once)", false);
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
                    Serial.printf("\n[ELEC] SP2: error 0x%02X  [modo:%s  p:%d]\n", val, ambienteGetEstado(), _pistaActual);
                    _pistaActual = 0;
                    break;
                default: break;
            }
            idx = 0;
        }
    }
}

void ambienteActualizar(bool activo, bool esCaliente) {
    if (!activo) return;   // deja que SP2 siga; ambienteReiniciar() lo para

    // gol_reaccion/hinchada: se manejan por evento 0x3D (o su watchdog de timeout,
    // en ambientePoll — corre siempre, incluso con el partido ya terminado). Acá
    // no hay que hacer nada más que esperar a que salgan de ese estado.
    if (_modo == AmbModo::GOL_REACCION || _modo == AmbModo::HINCHADA) {
        return;
    }

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
    _golesPartido++;
    // Si ya está sonando una reacción de gol, no la corta a mitad de pista para
    // arrancar otra — la deja terminar entera. El gol igual quedó contado arriba
    // (para el trigger de hinchada); solo se ignora el retrigger de audio.
    if (_modo == AmbModo::GOL_REACCION) return;
    _modoAnteGol = _modo;
    if (_pendingVol > 0) { cmd(0x06, 0x00, config.volumenAmbiente); _pendingVol = 0; }
    // Sin fade acá: cada paso de volumen tiene un delay(150) fijo del protocolo
    // DFPlayer, así que un fade-out real suma ~1s de pasos/silencio audibles antes
    // de que entre la reacción — peor que el corte directo. Instantáneo, ya
    // probado en mesa real que suena bien así.
    _modo = AmbModo::GOL_REACCION;
    uint8_t pista = config.ambienteGol.desde + random(config.ambienteGol.hasta - config.ambienteGol.desde + 1);
    _pistaActual  = pista;
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
