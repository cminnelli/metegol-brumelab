#include "AudioAmbiente.h"
#include "WebConfig.h"
#include <Arduino.h>

#define AMB_TX 16
#define AMB_RX 17

// Failsafe de hardware: gol_reaccion e hinchada tienen que terminar solos, avisados
// por el 0x3D real del DFPlayer — esto NO es un recorte de duración normal, es la
// red de seguridad para el caso límite de que el módulo se cuelgue y nunca avise.
// Por eso el número es bien generoso (muy por encima de cualquier pista real) y no
// es un parámetro de la web: no hay "duración correcta" que configurar, la pista
// define su propia duración.
#define GOL_REACCION_FAILSAFE_MS 15000UL
#define HINCHADA_FAILSAFE_MS     90000UL
// Ambiente/caliente (loop de fondo): confirmado en mesa real que hay DFPlayers que
// se quedan mudos sin avisar NADA — ni siquiera el 0x3D que gol_reaccion/hinchada
// sí reciben. Sin este failsafe, esos dos no tienen ninguna red de contención (el
// 0x3D nunca llega, así que relanzarMismaPista() nunca se dispara). Medido con
// afinfo sobre los wav reales: la pista más larga de ambienteGenerico dura 98s —
// 150s da margen sin arriesgar cortar una pista larga que todavía está sonando bien.
#define AMBIENTE_FAILSAFE_MS     150000UL

// Duración real (ms) de las pistas 1-17 de esta SD — ambienteGenerico, hinchadaMusica,
// momentoCaliente y ambienteGol, en ese orden — medida con `afinfo` sobre los wav.
// Con este DFPlayer el 0x3D prácticamente no llega nunca, así que en vez de esperar
// siempre el failsafe genérico (calibrado para la pista más larga de la categoría),
// el failsafe usa la duración real de la pista que está sonando cuando la conoce, y
// solo cae al genérico si la pista no está en esta tabla (SD distinta, rangos
// reconfigurados fuera de 1-17) — así nunca corta nada antes de tiempo.
static const uint32_t PISTA_DURACION_MS[] = {
    37965, 36504, 84023, 98284,          // 1-4   ambienteGenerico
    93205, 57168, 71866, 57864,          // 5-8   hinchadaMusica
    26192, 24102, 37222,                 // 9-11  momentoCaliente
     5476,  7301,  6989, 13096, 10951, 12423,  // 12-17 ambienteGol
};
// Duración real medida, sin el margen del failsafe — para mostrar en el log
// (transcurrido/total), no para decidir cuándo forzar.
static uint32_t duracionPistaBase(uint8_t pista) {
    if (pista >= 1 && pista <= sizeof(PISTA_DURACION_MS) / sizeof(PISTA_DURACION_MS[0]))
        return PISTA_DURACION_MS[pista - 1];
    return 0;   // pista desconocida
}

// Margen configurable (config.failsafeMargenMs, panel → Configuración técnica →
// Pitidos, finales y ambiente): cuánto se espera sobre la duración real medida
// antes de forzar el relanzamiento. Default 1200ms.
static uint32_t duracionPista(uint8_t pista) {
    uint32_t base = duracionPistaBase(pista);
    return base > 0 ? base + config.failsafeMargenMs : 0;   // 0: el caller cae al failsafe genérico
}

// La hinchada suena como máximo esta cantidad de veces por partido (el gol que la
// dispara + una "retoma" si otro gol la interrumpe mientras sigue sonando). Pasado el
// tope, los goles siguientes solo reaccionan y vuelven al ambiente normal/caliente de
// siempre — no vuelve a traer hinchada por el resto del partido.
#define HINCHADA_MAX_VECES 2

// Cola no bloqueante hacia el DFPlayer: antes cada comando hacía delay(150)
// acá mismo, y los fades encadenaban varios de esos en un for — hasta 1-2s
// bloqueados de un tirón. Ahora cmd() solo encola, y drenarCola() despacha de
// a uno respetando el mismo espaciado (más el gap extra que algunos pasos
// necesitan), sin frenar el loop. Se drena desde ambientePoll(), que ya se
// llama en cada vuelta del loop principal.
struct ComandoDF { uint8_t c, ph, pl; uint16_t gapMs; };
#define AMB_COLA_CAP 8   // como mucho volumen + play + loop encolados a la vez, con margen
static ComandoDF _cola[AMB_COLA_CAP];
static uint8_t   _colaLen       = 0;
static uint32_t  _ultimoEnvioAt = 0;
static uint16_t  _gapPendiente  = 150;   // gap exigido antes del próximo envío

static void cmd(uint8_t c, uint8_t ph, uint8_t pl, uint16_t gapMs = 150) {
    if (_colaLen >= AMB_COLA_CAP) return;   // cola llena: se descarta el comando más nuevo antes que corromper el orden
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
    Serial1.write(buf, 10);

    _ultimoEnvioAt = millis();
    _gapPendiente  = actual.gapMs;
    for (uint8_t i = 1; i < _colaLen; i++) _cola[i - 1] = _cola[i];
    _colaLen--;
}

// ── Estado ────────────────────────────────────────────────────────────────────

enum class AmbModo : uint8_t { PARADO, NORMAL, HINCHADA, CALIENTE, GOL_REACCION };

static AmbModo  _modo          = AmbModo::PARADO;
static AmbModo  _modoAnteGol   = AmbModo::PARADO; // modo antes de GOL_REACCION
static uint8_t  _pistaActual   = 0;     // 0 = sin pista activa
static bool     _enCaliente    = false; // sesión caliente activa
static uint8_t  _calienteCount = 0;     // sesiones caliente por partido (máx 2)
static uint8_t  _hinchadaVeces = 0;     // cuántas veces sonó hinchada en este partido (tope: HINCHADA_MAX_VECES)
static uint8_t  _golesPartido  = 0;    // goles totales del partido (para trigger hinchada)
static uint32_t _trackStartAt  = 0;   // para reportar duración al cambiar de pista
static uint32_t _ultimo0x3DProcesadoAt = 0;   // ver debounce de 0x3D duplicado en ambientePoll()

// ── Helpers ───────────────────────────────────────────────────────────────────

// Cambia de pista de ambiente. Un DFPlayer no puede mezclar dos pistas — no hay
// crossfade real posible con un solo módulo — así que el corte es directo e
// instantáneo. (Se probó tapar el corte con un sonido corto de "transición" antes
// de la pista nueva, pero depende de tener esos archivos extra grabados en la SD
// y sumaba una fuente más de fallo del DFPlayer sin ganancia real — se descartó.)
// Nunca arma el loop de hardware (0x19): el software es el único responsable de
// relanzar cuando corresponde (ver relanzarMismaPista/el failsafe en ambientePoll)
// — pedirle al DFPlayer que loopee por su cuenta ADEMÁS competía con eso, y si el
// módulo a veces sí cumplía el loop, sonaba como un doble corte.
// cabecera=false: el caller ya imprimió el bloque "SPK2 - AMBIENTE" con la razón
// del cambio (failsafe, error, etc.) — evita que un solo evento se vea en el log
// como si fueran dos (una cabecera para el porqué, otra para el resultado).
static void tocarAmbiente(const RangoAudio& r, const char* label, bool cabecera = true) {
    uint8_t pista = r.desde + random(r.hasta - r.desde + 1);
    _pistaActual  = pista;
    _trackStartAt = millis();
    cmd(0x06, 0x00, config.volumenAmbiente);
    cmd(0x03, 0x00, pista);
    if (cabecera) Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
    Serial.printf("     %-14s pista %d\n", label, pista);
}

// Relanza la MISMA pista que ya estaba sonando (no elige una nueva) — se usa cuando
// el 0x19 de hardware debería haberla loopeado solo y no lo hizo (o no confiamos en
// que lo haya hecho): sigue siendo la misma canción hasta el próximo gol, el ESP32
// solo se asegura de que efectivamente vuelva a sonar.
static void relanzarMismaPista(const char* label, bool cabecera = true) {
    // Sin cmd(0x06 volumen): no cambió desde que arrancó la pista, y reenviarlo
    // igual genera su propio blip audible en el DFPlayer, separado del corte del
    // play — sonaba como un doble corte en vez de uno solo.
    // Sin cmd(0x19 loop) tampoco: si el DFPlayer a veces SÍ cumple el loop de
    // hardware (no "nunca", solo poco confiable) y encima el software también
    // fuerza el relanzamiento por su cuenta, pueden competir los dos casi al
    // mismo tiempo — el módulo reiniciando solo Y el ESP32 forzándolo también.
    // Ahora el software es el único responsable de principio a fin: nunca se le
    // pide al módulo que loopee solo una pista de ambiente/caliente.
    _trackStartAt = millis();
    cmd(0x03, 0x00, _pistaActual);
    if (cabecera) Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
    Serial.printf("     %-14s pista %d (relanzada)\n", label, _pistaActual);
}

// Decide a dónde vuelve SP2 cuando termina gol_reaccion — mismo camino tanto si el
// DFPlayer avisó de verdad (0x3D, viaFailsafe=false) como si se fuerza por el failsafe
// (viaFailsafe=true, que solo cambia el sufijo del log). La hinchada suena como máximo
// HINCHADA_MAX_VECES veces por partido: el gol que la dispara + una "retoma" si otro gol
// la interrumpe mientras sigue sonando — pasado el tope, cae al caso de abajo (vuelve a
// ambiente normal/caliente) igual que si nunca hubiera habido hinchada.
static void volverDeGolReaccion(bool viaFailsafe) {
    // Una sola cabecera "SPK2 - AMBIENTE" por evento: el porqué (esta línea, o la
    // de "gol_reaccion fin" más abajo) y el resultado (la que imprime tocarAmbiente()
    // al final, con cabecera=false) van bajo el mismo bloque — si no, en el log
    // parece que fueran dos eventos separados.
    Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
    if (viaFailsafe) {
        Serial.printf("     gol_reaccion: el DFPlayer nunca avisó que terminó, failsafe forzando salida\n");
    }
    const char* wdComa = viaFailsafe ? ", wd" : "";   // sufijo dentro de "hinchada (...)"
    const char* wdSolo = viaFailsafe ? " (wd)" : "";  // sufijo para "ambiente"/"caliente" sueltos
    uint32_t durS = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
    char lbl[24];
    const RangoAudio* r;

    if (_modoAnteGol == AmbModo::HINCHADA && _hinchadaVeces < HINCHADA_MAX_VECES) {
        _hinchadaVeces++;
        _modo = AmbModo::HINCHADA;
        snprintf(lbl, sizeof(lbl), "hinchada (retoma%s)", wdComa);
        r = &config.hinchadaMusica;
    } else if (_hinchadaVeces == 0 && _golesPartido >= config.hinchadaGol) {
        _hinchadaVeces = 1;
        _modo = AmbModo::HINCHADA;
        snprintf(lbl, sizeof(lbl), "hinchada (once%s)", wdComa);
        r = &config.hinchadaMusica;
    } else {
        _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
        r = (_modo == AmbModo::CALIENTE) ? &config.momentoCaliente : &config.ambienteGenerico;
        snprintf(lbl, sizeof(lbl), "%s%s", (_modo == AmbModo::CALIENTE) ? "caliente" : "ambiente", wdSolo);
    }

    if (!viaFailsafe) {
        Serial.printf("     gol_reaccion fin  |  %lus  →  %s\n", (unsigned long)durS, lbl);
    }
    tocarAmbiente(*r, lbl, false);
}

// ── API pública ───────────────────────────────────────────────────────────────

void ambienteBegin() {
    Serial1.begin(9600, SERIAL_8N1, AMB_RX, AMB_TX);
}

void ambienteSetVolumen(uint8_t vol) {
    cmd(0x06, 0x00, vol);
}

// "Reiniciar" acá es solo poner SP2 en silencio y limpiar su estado interno (modo,
// pista, contadores) — no reinicia el ESP32 ni el módulo DFPlayer en sí. Se llama
// tanto al bootear (arranque limpio) como al arrancar/cancelar un partido.
void ambienteReiniciar() {
    Serial.printf("\n──── SP2  DETENIDO  [%s  p:%d]\n", ambienteGetEstado(), _pistaActual);
    cmd(0x0E, 0x00, 0x00);             // pausa SP2
    _modo          = AmbModo::PARADO;
    _modoAnteGol   = AmbModo::PARADO;
    _pistaActual   = 0;
    _hinchadaVeces = 0;
    _golesPartido  = 0;
    _enCaliente    = false;
    _calienteCount = 0;
}

void ambientePoll() {
    drenarCola();

    // Watchdogs de failsafe de gol_reaccion/hinchada — corren siempre (no solo
    // con partido.activo). Si el gol que dispara la hinchada es el que termina
    // el partido, partido.activo ya está en false para cuando esto se evalúa,
    // así que estos chequeos no pueden depender de ese flag: si dependieran,
    // el ambiente quedaba trabado en hinchada para siempre tras el pitido final,
    // porque no hay ningún otro evento (próximo gol, próximo partido) que lo saque.
    if (_modo == AmbModo::GOL_REACCION) {
        uint32_t limite = duracionPista(_pistaActual);
        if (limite == 0) limite = GOL_REACCION_FAILSAFE_MS;
        if (millis() - _trackStartAt > limite) {
            volverDeGolReaccion(true);
        }
    } else if (_modo == AmbModo::HINCHADA) {
        uint32_t limite = duracionPista(_pistaActual);
        if (limite == 0) limite = HINCHADA_FAILSAFE_MS;
        if (millis() - _trackStartAt > limite) {
            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
            Serial.printf("     hinchada: el DFPlayer nunca avisó que terminó, failsafe forzando salida\n");
            _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
            const RangoAudio& r = _enCaliente ? config.momentoCaliente : config.ambienteGenerico;
            const char*     lbl = _enCaliente ? "caliente (wd)" : "ambiente (wd)";
            tocarAmbiente(r, lbl, false);
        }
    } else if (_modo == AmbModo::NORMAL || _modo == AmbModo::CALIENTE) {
        // Confirmado en mesa real: hay DFPlayers que se quedan mudos sin avisar
        // NADA, ni el 0x3D que relanzarMismaPista() necesita para reaccionar — con
        // esos módulos, sin este failsafe, el ambiente no tiene ninguna forma de
        // recuperarse. Igual que arriba: último recurso, no depende de ningún aviso.
        uint32_t limite = duracionPista(_pistaActual);
        if (limite == 0) limite = AMBIENTE_FAILSAFE_MS;
        if (millis() - _trackStartAt > limite) {
            Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
            Serial.printf("     %s: el DFPlayer nunca avisó nada, failsafe forzando relanzar\n", ambienteGetEstado());
            relanzarMismaPista(_modo == AmbModo::CALIENTE ? "caliente" : "ambiente", false);
        }
    }

    static uint8_t buf[10], idx = 0;
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
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
            if (buf[9] != 0xEF) { idx = 0; continue; }
            uint8_t tipo = buf[3], val = buf[6];
            switch (tipo) {
                case 0x3F:
                    Serial.printf("\n[ELEC] SP2: reset  [modo:%s  p:%d]\n", ambienteGetEstado(), _pistaActual);
                    cmd(0x06, 0x00, config.volumenAmbiente);  // restaura volumen tras reset
                    if (_modo == AmbModo::GOL_REACCION || _modo == AmbModo::HINCHADA) {
                        _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                    }
                    // Cualquier modo: tras un reset no hay pista activa de verdad, la haya
                    // pedido quien la haya pedido — así el watchdog de ambienteActualizar()
                    // (pistaActual==0) la retoma solo en el próximo tick, sin esperar a un gol.
                    _pistaActual = 0;
                    break;
                case 0x3D: {
                    uint32_t desdeInicio = millis() - _trackStartAt;
                    // Log crudo de TODO 0x3D que llega, se use o se descarte — sin esto, un
                    // aviso descartado desaparece del log sin dejar rastro y no hay forma de
                    // saber si el doble corte viene de acá o de otro lado.
                    Serial.printf("\n[0x3D] val=%d  pistaActual=%d  modo=%s  desde_inicio=%lums\n",
                        val, _pistaActual, ambienteGetEstado(), (unsigned long)desdeInicio);
                    if (_modo == AmbModo::PARADO) { Serial.printf("       descartado: modo PARADO\n"); break; }
                    // Algunos clones de DFPlayer mandan el 0x3D DUPLICADO — dos avisos para el
                    // mismo final de pista, real, no atrasado. El chequeo de "desde_inicio" de
                    // abajo no lo frena porque los dos llegan en el momento correcto. Este es
                    // un resguardo aparte: si ya procesamos un 0x3D hace menos de 1s (sea cual
                    // sea la pista), el segundo es casi seguro un duplicado del mismo aviso.
                    if (millis() - _ultimo0x3DProcesadoAt < 1000UL) {
                        Serial.printf("       descartado: 0x3D duplicado (< 1s del último procesado)\n");
                        break;
                    }
                    // val = número de pista que terminó. Como cmd() ahora encola (no bloquea), al
                    // encolar el comando de una pista nueva _pistaActual se actualiza al instante
                    // pero el comando "play" real puede tardar hasta ~150ms en salir por la cola —
                    // en esa ventana puede llegar el 0x3D legítimo de la pista VIEJA, que sin este
                    // chequeo se interpretaba como si la pista nueva ya hubiera terminado.
                    if (val != _pistaActual) { Serial.printf("       descartado: val != pistaActual\n"); break; }
                    // Confirmado en mesa real: cuando se relanza la MISMA pista (ver
                    // relanzarMismaPista, failsafe de ambiente/caliente), este chequeo de
                    // arriba no alcanza — el número no cambió, así que un 0x3D viejo y
                    // atrasado de la reproducción ANTERIOR de esa misma pista pasa el
                    // chequeo igual y dispara un segundo relanzamiento casi inmediato
                    // (sonaba como un doble corte). Ninguna pista real dura tan poco, así
                    // que un aviso a menos de 1.5s de haber (re)arrancado tiene que ser un
                    // resto viejo — se descarta.
                    if (desdeInicio < 1500UL) { Serial.printf("       descartado: <1.5s desde inicio, probable aviso viejo\n"); break; }
                    if (_modo == AmbModo::GOL_REACCION) {
                        volverDeGolReaccion(false);
                    } else if (_modo == AmbModo::HINCHADA) {
                        uint32_t durS = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
                        _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                        const RangoAudio& r = (_modo == AmbModo::CALIENTE) ? config.momentoCaliente : config.ambienteGenerico;
                        const char*       lbl = (_modo == AmbModo::CALIENTE) ? "caliente" : "ambiente";
                        Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                        Serial.printf("     hinchada fin  |  %lus  →  %s\n", (unsigned long)durS, lbl);
                        tocarAmbiente(r, lbl, false);
                    } else {
                        // NORMAL/CALIENTE: no confiar en que el 0x19 la haya loopeado bien sola
                        // (hay DFPlayers/clones que no lo respetan) — se usa este aviso real de
                        // "pista terminada" para relanzar la MISMA pista por software. Sigue siendo
                        // una sola canción en loop hasta el próximo gol; solo cambia quién se asegura
                        // de que vuelva a sonar. El log de duración sirve para diagnosticar en la
                        // mesa real si el 0x3D llega en cada vuelta del loop de hardware o solo
                        // cuando se corta de verdad.
                        uint32_t durS = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
                        const char* lbl = (_modo == AmbModo::CALIENTE) ? "caliente" : "ambiente";
                        Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                        Serial.printf("     loop anterior duró %lus\n", (unsigned long)durS);
                        relanzarMismaPista(lbl, false);
                    }
                    break;
                }
                case 0x40:
                    Serial.printf("\n[ELEC] SP2: error 0x%02X  [modo:%s  p:%d]\n", val, ambienteGetEstado(), _pistaActual);
                    _pistaActual = 0;
                    // La pista pedida falló (ej. archivo faltante en la SD) — no tiene
                    // sentido esperar el failsafe de varios segundos para algo que ya
                    // sabemos que no va a avisar 0x3D. Se recupera al toque, igual que
                    // si hubiera terminado bien.
                    if (_modo == AmbModo::GOL_REACCION) {
                        volverDeGolReaccion(true);
                    } else if (_modo == AmbModo::HINCHADA) {
                        uint32_t durS = _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
                        _modo = _enCaliente ? AmbModo::CALIENTE : AmbModo::NORMAL;
                        const RangoAudio& r = (_modo == AmbModo::CALIENTE) ? config.momentoCaliente : config.ambienteGenerico;
                        const char*       lbl = (_modo == AmbModo::CALIENTE) ? "caliente" : "ambiente";
                        Serial.printf("\n──── SPK2 - AMBIENTE  ───────────────────────\n");
                        Serial.printf("     hinchada fin  |  %lus  →  %s (err)\n", (unsigned long)durS, lbl);
                        tocarAmbiente(r, lbl, false);
                    }
                    break;
                default: break;
            }
            idx = 0;
        }
    }
}

void ambienteActualizar(bool activo, bool esCaliente) {
    if (!activo) return;   // deja que SP2 siga; ambienteReiniciar() lo para

    // gol_reaccion/hinchada: se manejan por evento 0x3D (o su watchdog de
    // timeout, en ambientePoll — corre siempre, incluso con el partido ya terminado). Acá
    // no hay que hacer nada más que esperar a que salgan de ese estado.
    if (_modo == AmbModo::GOL_REACCION || _modo == AmbModo::HINCHADA) {
        return;
    }

    // Entrar en CALIENTE (máx 2 veces por partido, solo si el partido está caliente)
    if (esCaliente && !_enCaliente && _calienteCount < 2) {
        _calienteCount++;
        _enCaliente = true;
        _modo       = AmbModo::CALIENTE;
        tocarAmbiente(config.momentoCaliente, "caliente");
        return;
    }

    // Salir de CALIENTE cuando el partido se enfría
    if (!esCaliente && _enCaliente) {
        _enCaliente = false;
        if (_modo == AmbModo::CALIENTE) {
            _modo = AmbModo::NORMAL;
            tocarAmbiente(config.ambienteGenerico, "ambiente");
        }
        return;
    }

    // Arrancar NORMAL al iniciar el partido
    if (_modo == AmbModo::PARADO) {
        _modo = AmbModo::NORMAL;
        tocarAmbiente(config.ambienteGenerico, "ambiente");
        return;
    }

    // Watchdog: reinicia si no hay pista activa (ej. error SP2 o vuelta de gol_reaccion)
    if (_pistaActual == 0) {
        const RangoAudio& r = (_modo == AmbModo::CALIENTE) ? config.momentoCaliente : config.ambienteGenerico;
        const char* lbl     = (_modo == AmbModo::CALIENTE) ? "caliente (wd)" : "ambiente (wd)";
        tocarAmbiente(r, lbl);
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

uint32_t ambienteGetDuracionSegs() {
    return _trackStartAt > 0 ? (millis() - _trackStartAt) / 1000 : 0;
}

uint32_t ambienteGetDuracionTotalSegs() {
    return duracionPistaBase(_pistaActual) / 1000;   // 0 si la pista no está en la tabla
}

// Versiones en ms de las dos de arriba — para comparar con precisión de menos de
// un segundo (ver ANTELACION_GOL_MS en Comentarista.cpp).
uint32_t ambienteGetDuracionMs() {
    return _trackStartAt > 0 ? millis() - _trackStartAt : 0;
}

uint32_t ambienteGetDuracionTotalMs() {
    return duracionPistaBase(_pistaActual);   // 0 si la pista no está en la tabla
}

// 0x0A/0x0B (standby/normal del estándar DFPlayer Mini) se probaron acá y
// dejaban el chip sin responder a nada después — se sacan. ambienteReiniciar()
// (pausa + reset de estado) ya garantiza el silencio real.
void ambienteEntrarReposo() {
    ambienteReiniciar();
}

void ambienteSalirReposo() {
    cmd(0x06, 0x00, config.volumenAmbiente);
}
