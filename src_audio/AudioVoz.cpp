#include "AudioVoz.h"
#include "WebConfig.h"
#include <Arduino.h>

#define VOZ_TX 26
#define VOZ_RX 27

// Cola no bloqueante hacia el DFPlayer: antes cada comando hacía delay(150)
// acá mismo, lo que frenaba sensores/display/web cada vez que sonaba algo.
// Ahora cmd() solo encola, y drenarCola() despacha de a uno respetando el
// mismo espaciado, sin bloquear el loop. Se drena desde vozPoll(), que ya
// se llama en cada vuelta del loop principal.
struct ComandoDF { uint8_t c, ph, pl; uint16_t gapMs; };
#define VOZ_COLA_CAP 8
static ComandoDF _cola[VOZ_COLA_CAP];
static uint8_t   _colaLen      = 0;
static uint32_t  _ultimoEnvioAt = 0;
static uint16_t  _gapPendiente  = 150;   // gap exigido antes del próximo envío

static void cmd(uint8_t c, uint8_t ph, uint8_t pl, uint16_t gapMs = 150) {
    if (_colaLen >= VOZ_COLA_CAP) return;   // cola llena: se descarta el comando más nuevo antes que corromper el orden
    _cola[_colaLen++] = { c, ph, pl, gapMs };
}

static void drenarCola() {
    if (_colaLen == 0) return;
    if (millis() - _ultimoEnvioAt < _gapPendiente) return;

    ComandoDF actual = _cola[0];
    uint8_t buf[10];
    buf[0]=0x7E; buf[1]=0xFF; buf[2]=0x06; buf[3]=actual.c;
    buf[4]=0x00; buf[5]=actual.ph; buf[6]=actual.pl;
    int16_t cs = -(int16_t)(buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]);
    buf[7]=(cs>>8)&0xFF; buf[8]=cs&0xFF; buf[9]=0xEF;
    Serial2.write(buf, 10);

    _ultimoEnvioAt = millis();
    _gapPendiente  = actual.gapMs;
    for (uint8_t i = 1; i < _colaLen; i++) _cola[i - 1] = _cola[i];
    _colaLen--;
}

void vozBegin() {
    Serial2.begin(9600, SERIAL_8N1, VOZ_RX, VOZ_TX);
}

void vozStop() {
    cmd(0x0E, 0x00, 0x00);   // pause SP1
}

void vozPlay(Pista pista) {
    cmd(0x03, 0x00, (uint8_t)pista);
}

static bool     _sp1Busy       = false;
static uint32_t _sp1BusyDesde  = 0;
static uint8_t  _sp1UltimaPista = 0;   // para poder loguear qué pista dio error (ver vozPoll)
#define SP1_BUSY_TIMEOUT_MS 8000UL   // red de seguridad si el aviso de "terminé" nunca llega

void vozPlayTrack(uint8_t n) {
    _sp1Busy        = true;
    _sp1BusyDesde   = millis();
    _sp1UltimaPista = n;
    cmd(0x03, 0x00, n);
}

bool vozIsBusy() { return _sp1Busy; }

// El gol es lo más importante: pisa cualquier comentario en curso, pero con un
// fade breve (baja volumen, cambia de pista, restaura) para que no suene tosco.
void vozPlayTrackPrioritario(uint8_t n) {
    if (_sp1Busy) {
        cmd(0x06, 0x00, 0);              // fade breve: baja volumen antes de pisar
        _sp1Busy        = true;
        _sp1BusyDesde   = millis();
        _sp1UltimaPista = n;
        cmd(0x03, 0x00, n);
        cmd(0x06, 0x00, config.volumenVoz);  // restaura volumen ya sobre la pista del gol
    } else {
        vozPlayTrack(n);
    }
}

void vozSetVolumen(uint8_t vol) {
    cmd(0x06, 0x00, vol);
}

void vozPitidoInicio() {
    uint8_t n = config.pitidoInicio.desde + random(config.pitidoInicio.hasta - config.pitidoInicio.desde + 1);
    Serial.printf("\n──── SPK1 - SILBATO  ────────────────────────\n");
    Serial.printf("     inicio   pista %d\n", n);
    vozPlayTrack(n);
}

void vozPitidoFinal() {
    uint8_t n = config.pitidoFinal.desde + random(config.pitidoFinal.hasta - config.pitidoFinal.desde + 1);
    Serial.printf("\n──── SPK1 - SILBATO  ────────────────────────\n");
    Serial.printf("     final    pista %d\n", n);
    vozPlayTrack(n);
}

// 0x0A/0x0B (standby/normal del estándar DFPlayer Mini) se probaron acá y
// dejaban el chip sin responder a nada después — se sacan. La pausa (0x0E)
// sola ya garantiza el silencio real y es la misma que usa el resto del proyecto.
void vozEntrarReposo() {
    cmd(0x0E, 0x00, 0x00);
}

void vozSalirReposo() {
    cmd(0x06, 0x00, config.volumenVoz);
}

void vozPoll() {
    drenarCola();

    static uint8_t buf[10], idx = 0;
    while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (idx == 0) {
            if (b == 0x7E) buf[idx++] = b;   // solo un 0x7E arranca una trama nueva
            continue;
        }
        // A mitad de trama, un 0x7E es dato (ej. el checksum de la pista 64), no un
        // reset — antes se perdía el aviso siempre que esto pasaba. No se valida el
        // checksum en sí (bytes 7-8): los clones de DFPlayer de esta mesa no lo
        // calculan según la fórmula estándar (ver nota en README), exigirlo acá tira
        // avisos reales y todo termina cayendo al timeout del watchdog.
        if (idx < 10) buf[idx++] = b;
        if (idx >= 10) {
            if (buf[9] == 0xEF) {
                uint8_t tipo = buf[3], val = buf[6];
                switch (tipo) {
                    // val = pista que terminó. vozPlayTrackPrioritario encola un fade antes del
                    // play real, así que _sp1UltimaPista puede quedar seteada varios cientos de ms
                    // antes de que la pista nueva llegue a sonar — sin este chequeo, un 0x3D legítimo
                    // de la pista VIEJA (que va a llegar en ese lapso) libera _sp1Busy de más.
                    case 0x3D: if (val == _sp1UltimaPista) _sp1Busy = false; break;
                    case 0x3F: _sp1Busy = false; Serial.println("\n[ELEC] SP1: reset"); cmd(0x06, 0x00, config.volumenVoz); break;
                    case 0x40: _sp1Busy = false; Serial.printf("\n[ELEC] SP1: error 0x%02X en pista %d\n", val, _sp1UltimaPista); break;
                    default: break;
                }
            }
            idx = 0;   // trama procesada o descartada: listo para resincronizar con el próximo 0x7E
        }
    }

    if (_sp1Busy && millis() - _sp1BusyDesde > SP1_BUSY_TIMEOUT_MS) {
        Serial.println("\n[ELEC] SP1: busy timeout, liberando");
        _sp1Busy = false;
    }
}
