#include <Arduino.h>
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Partido.h"
#include "WebConfig.h"
#include "Comentarista.h"
#include "Display.h"
#include "Reposo.h"
#include "config.h"

#define PIN_SENSOR_CELESTE 34   // equipo celeste
#define PIN_SENSOR_BLANCO  35   // equipo blanco

// ---- Encoder KY-040 ----
#define PIN_CLK  32
#define PIN_DT   33
#define PIN_SW   25

volatile int  encDelta   = 0;
volatile bool encChanged = false;

void IRAM_ATTR onClk() {
    static uint32_t lastPulse = 0;
    uint32_t now = micros();
    if (now - lastPulse < 5000) return;  // 5ms: descarta el 2do pulso del mismo detent (KY-040)
    lastPulse = now;
    if (digitalRead(PIN_DT) == LOW) encDelta++;
    else                            encDelta--;
    encChanged = true;
}

const uint32_t BTN_DEBOUNCE_MS = 40;
const uint32_t BTN_DOUBLE_MS   = 350;
const uint32_t BTN_LONG_MS     = 2000;  // hold 2s → reset ESP32
static bool     btnState      = HIGH;
static bool     btnRaw        = HIGH;
static uint32_t btnDebounceAt = 0;
static uint32_t btnReleaseAt  = 0;
static uint32_t btnPressAt    = 0;
static bool     btnLongFired  = false;
static int      btnPending    = 0;

Partido partido;

// Display en modo tiempo: alterna marcador ↔ tiempo restante, ambos quietos
// (no scrolleando) cada uno su ratito, cada intervaloDisplay segundos.
static uint32_t _altUltimoCambio   = 0;
static bool     _altMuestraTiempo  = false;
static uint32_t _tiempoUltimoTick  = 0;   // último refresco en vivo de los segundos (ver displayTiempoActualizar)

// Sensores de gol — file-scope para poder resetear al iniciar partido
static bool          _prevSensor1  = HIGH;
static bool          _prevSensor2  = HIGH;
static unsigned long _ultimoGol1   = 0;
static unsigned long _ultimoGol2   = 0;
static unsigned long _sensor1LowAt = 0;   // momento en que sensor1 bajó a LOW
static unsigned long _sensor2LowAt = 0;
#define SENSOR_MIN_LOW_MS 20              // duración mínima LOW para validar gol

// Fin de partido (por gol o por tiempo): espera a que termine lo que esté
// sonando en SP1 (el relato del gol, o cualquier comentario regular que
// hubiera en curso) — y la reacción de gol en SP2, si corresponde — antes de
// disparar el pitido final. Sin esto, el pitido se manda igual apenas se
// cumple la condición de fin y puede cortarle la frase a un comentario
// cualquiera que estuviera sonando en ese momento — ver loop().
// _finXPendienteDesde + FIN_PARTIDO_MAX_ESPERA_MS acotan la espera: si algo se
// cuelga (ej. SP1 nunca avisa que terminó), se fuerza igual en vez de quedar
// trabado para siempre. Mientras cualquiera de los dos esté pendiente, el botón
// de "nuevo partido"/cancelar se ignora — evita que arrancar el próximo partido
// cancele en silencio el pitido, el cartel de "Ganador" y el comentario final.
static bool     _finGolPendiente      = false;
static int8_t   _finGolGanador        = -1;
static uint32_t _finGolPendienteDesde = 0;
static bool     _finSinGolPendiente      = false;
static int8_t   _finSinGolGanador        = -1;
static uint32_t _finSinGolPendienteDesde = 0;
#define FIN_PARTIDO_MAX_ESPERA_MS 7000UL

// Fin de partido: invita a jugar de nuevo, una vez que terminó de scrollear
// el "Fin! Ganador..." — ver loop()
static bool    _jugarDeNuevoPendiente = false;

// Mientras el partido queda "terminado" esperando el próximo, la farola rota
// entre el resultado y el texto de "jugar de nuevo" cada intervaloDisplay
// segundos, hasta que arranca el próximo partido — ver loop()
static bool     _finRotando            = false;
static bool     _finRotandoMuestraTexto = false;   // false=marcador, true=texto
static uint32_t _finRotandoUltimoCambio = 0;

// Llamado desde WebConfig antes de arrancar/reanudar un partido por web — evita la
// misma carrera que el botón del encoder (ver _finGolPendiente/_finSinGolPendiente arriba)
bool finDePartidoPendiente() {
    return _finGolPendiente || _finSinGolPendiente;
}

// Llamado desde WebConfig al iniciar/reanudar via web — resetea sensores igual que el encoder
void resetearDeteccionGoles() {
    _prevSensor1     = digitalRead(PIN_SENSOR_CELESTE);
    _prevSensor2     = digitalRead(PIN_SENSOR_BLANCO);
    // El bloqueo de 5s entre goles del mismo arco no tiene sentido al arrancar/
    // reanudar — acá alcanza con ~1s para no confundir el rebote del botón o el
    // ruido del propio arranque con un gol real. Restar 4000 en vez de dejarlo en
    // millis() logra eso sin agregar una ventana de bloqueo aparte (millis() - _ultimoGolX
    // sigue dando el delta correcto aunque esto envuelva cerca del boot, por ser aritmética
    // unsigned).
    _ultimoGol1      = millis() - 4000;
    _ultimoGol2      = millis() - 4000;
    _sensor1LowAt    = 0;
    _sensor2LowAt    = 0;
    _altUltimoCambio = millis();
}

// Llamado desde WebConfig cuando se aprieta "Terminar partido" en el panel —
// cierra el partido ya mismo con el marcador actual. El pitido/comentario final
// quedan pendientes hasta que SP1 esté libre (mismo criterio que el fin por
// gol/tiempo — ver _finSinGolPendiente y el bloque que lo dispara en loop()),
// para no cortarle la frase a un comentario que estuviera sonando en ese momento.
void finalizarPartidoManual() {
    if (!partido.activo && !partido.pausado) return;
    partido.activo    = false;
    partido.pausado   = false;
    partido.terminado = true;
    _finSinGolGanador        = partido.ganador();
    _finSinGolPendiente      = true;
    _finSinGolPendienteDesde = millis();
    Serial.println("\n[JUEGO] Partido finalizado manualmente (web) — esperando cierre (pitido/comentario)");
}


void setup() {
    Serial.begin(115200);
    Serial.println("=== HOLA BRUMELAB! ===");
    {
        esp_reset_reason_t r = esp_reset_reason();
        const char* motivo =
            r == ESP_RST_POWERON  ? "POWER_ON"  :
            r == ESP_RST_SW       ? "SW_RESET"  :
            r == ESP_RST_PANIC    ? "PANIC/EXCEPCION" :
            r == ESP_RST_INT_WDT  ? "WDT_INTERRUPCION" :
            r == ESP_RST_TASK_WDT ? "WDT_TAREA"  :
            r == ESP_RST_WDT      ? "WDT_OTRO"   :
            r == ESP_RST_DEEPSLEEP? "DEEP_SLEEP" :
            r == ESP_RST_BROWNOUT ? "BROWNOUT"   : "DESCONOCIDO";
        Serial.printf("[RESET] Motivo: %s (%d)\n", motivo, (int)r);
    }
    Serial.println("[1] Serial OK");
    webConfigInit(&partido);  // carga config desde NVS + inicia AP WiFi (activa RF → entropía real)
    randomSeed(esp_random() ^ (uint32_t)micros());

    Serial.println("=== CONFIG CARGADA ===");
    Serial.printf("  Volumen (voz)     : %d\n", config.volumenVoz);
    Serial.printf("  Volumen (ambiente): %d\n", config.volumenAmbiente);
    Serial.printf("  Modo de juego     : %s\n", config.modoJuego == 0 ? "goles" : "tiempo");
    if (config.modoJuego == 0)
        Serial.printf("  Goles para ganar  : %d\n", config.golesMax);
    else
        Serial.printf("  Duracion          : %d min\n", config.duracionMin);
    Serial.printf("  Brillo display    : %d\n", config.brillo);
    Serial.printf("  Vel. scroll       : %d ms\n", config.velocidadScroll);
    Serial.printf("  Pista ambiente    : %d\n", config.pistaAmbiente);
    Serial.println("======================");

    pinMode(PIN_SENSOR_CELESTE, INPUT);
    pinMode(PIN_SENSOR_BLANCO,  INPUT);
    pinMode(PIN_CLK, INPUT_PULLUP);
    pinMode(PIN_DT,  INPUT_PULLUP);
    pinMode(PIN_SW,  INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_CLK), onClk, FALLING);

    ambienteBegin();
    vozBegin();

    // Para ambos DFPlayers al arrancar (siguen con poder aunque el ESP32 haya reseteado)
    Serial.println("[BOOT] Silenciando DFPlayers para arrancar limpio (normal, no es un error)...");
    vozStop();
    ambienteReiniciar();

    // Boot wait compartido 2s — captura 0x3F de SP1 y SP2
    for (uint16_t i = 0; i < 200; i++) {
        vozPoll();
        ambientePoll();
        delay(10);
    }

    // Aplica volúmenes desde config guardada
    vozSetVolumen(config.volumenVoz);
    for (uint8_t i = 0; i < 30; i++) { vozPoll(); ambientePoll(); delay(10); }
    ambienteSetVolumen(config.volumenAmbiente);
    for (uint8_t i = 0; i < 30; i++) { vozPoll(); ambientePoll(); delay(10); }
    // Inicia SP2 con pista ambiente
    displayInit();  // scrollea "MAKERGOL!" — se completa en los primeros ciclos de loop()

    partido.resetear();
    reposoInit();
    // Descarta cualquier ruido del encoder acumulado durante el boot
    noInterrupts(); encDelta = 0; encChanged = false; interrupts();
    Serial.println("[2] Sistema listo");
}

void loop() {
    webConfigLoop();
    displayTick();
    vozPoll();
    ambientePoll();
    reposoTick(partido);

    // SP2 reactivo al estado del partido
    {
        const char* estadoStr = comentaristaGetEstado(partido);
        bool esCaliente = (strcmp(estadoStr, "caliente") == 0);
        ambienteActualizar(partido.activo, esCaliente);
    }

    // ---- Sensores primero: si hay gol, registrarGol empuja _proximoComentario
    //     hacia el futuro antes de que comentaristaLoop pueda disparar un track ----
    bool cur1 = digitalRead(PIN_SENSOR_CELESTE);
    bool cur2 = digitalRead(PIN_SENSOR_BLANCO);

    if (partido.activo) {
        // Filtro anti-ruido: un flanco de bajada arma un candidato, y el gol recién
        // se confirma si el sensor sigue en LOW pasados SENSOR_MIN_LOW_MS — un rebote
        // o ruido que vuelve a HIGH antes de eso se descarta sin contar.
        if (millis() - _ultimoGol1 >= 3000) {
            if (cur1 == LOW) {
                if (_prevSensor1 == HIGH) {
                    _sensor1LowAt = millis();
                } else if (_sensor1LowAt != 0 && millis() - _sensor1LowAt >= SENSOR_MIN_LOW_MS) {
                    _sensor1LowAt = 0;
                    _ultimoGol1 = millis();
                    partido.registrarGol(0);
                    Serial.printf("\n──── GOL!  %d ─ %d  ──────────────────────────\n",
                        partido.goles[0], partido.goles[1]);
                    Serial.printf("     Celeste anota!\n");
                    ambienteOnGol();
                    displayMarcador(partido.goles[0], partido.goles[1]);
                    comentaristaOnGol(partido);
                    displayGol();
                    // Reinicia la alternancia marcador↔tiempo desde ahora — sin esto, si
                    // el gol coincide con el momento en que ya le tocaba disparar, el
                    // marcador se redibuja por el gol Y CASI ENSEGUIDA la alternancia
                    // dispara de nuevo un scroll completo de salida+entrada: se ve como
                    // si scrolleara dos veces seguidas (intermitente, según el timing).
                    _altUltimoCambio = millis();
                    if (partido.terminado) {
                        int8_t w = partido.ganador();
                        Serial.printf("     → FINAL  |  %s\n",
                            w == 0 ? "Ganó Celeste!" : w == 1 ? "Ganó Blanco!" : "Empate!");
                        _finGolPendiente      = true;
                        _finGolGanador        = w;
                        _finGolPendienteDesde = millis();
                    }
                }
            } else {
                _sensor1LowAt = 0;
            }
        }
        if (millis() - _ultimoGol2 >= 3000) {
            if (cur2 == LOW) {
                if (_prevSensor2 == HIGH) {
                    _sensor2LowAt = millis();
                } else if (_sensor2LowAt != 0 && millis() - _sensor2LowAt >= SENSOR_MIN_LOW_MS) {
                    _sensor2LowAt = 0;
                    _ultimoGol2 = millis();
                    partido.registrarGol(1);
                    Serial.printf("\n──── GOL!  %d ─ %d  ──────────────────────────\n",
                        partido.goles[0], partido.goles[1]);
                    Serial.printf("     Blanco anota!\n");
                    ambienteOnGol();
                    displayMarcador(partido.goles[0], partido.goles[1]);
                    comentaristaOnGol(partido);
                    displayGol();
                    _altUltimoCambio = millis();  // ídem arriba — evita el doble scroll del marcador
                    if (partido.terminado) {
                        int8_t w = partido.ganador();
                        Serial.printf("     → FINAL  |  %s\n",
                            w == 0 ? "Ganó Celeste!" : w == 1 ? "Ganó Blanco!" : "Empate!");
                        _finGolPendiente      = true;
                        _finGolGanador        = w;
                        _finGolPendienteDesde = millis();
                    }
                }
            } else {
                _sensor2LowAt = 0;
            }
        }
    }
    _prevSensor1 = cur1;
    _prevSensor2 = cur2;

    // ---- Fin de partido por gol: espera a que termine el relato del gol (SP1) y
    //     la reacción de gol (SP2) antes de disparar el pitido final — con techo:
    //     si algo se cuelga, se fuerza igual pasados FIN_PARTIDO_MAX_ESPERA_MS ----
    if (_finGolPendiente &&
        ((!vozIsBusy() && strcmp(ambienteGetEstado(), "gol_reaccion") != 0)
         || (millis() - _finGolPendienteDesde > FIN_PARTIDO_MAX_ESPERA_MS))) {
        _finGolPendiente = false;
        // No se llama ambienteReiniciar() acá: el ambiente que ya está sonando
        // (genérico o caliente) sigue de fondo durante el pitido y el comentario
        // final, en vez de cortar a silencio. Se resetea recién al arrancar el
        // próximo partido.
        vozPitidoFinal();
        comentaristaFinalPartido(partido);
        displayGanador(_finGolGanador);
        _jugarDeNuevoPendiente = true;
    }

    // ---- Fin de partido sin gol (por tiempo, o "Terminar partido" desde la web):
    //     mismo criterio que el fin por gol de arriba — espera a que termine
    //     cualquier comentario en curso en SP1 antes de disparar el pitido final,
    //     para no cortarle la frase a la mitad ----
    if (_finSinGolPendiente &&
        (!vozIsBusy() || (millis() - _finSinGolPendienteDesde > FIN_PARTIDO_MAX_ESPERA_MS))) {
        _finSinGolPendiente = false;
        vozPitidoFinal();
        comentaristaFinalPartido(partido);
        displayGanador(_finSinGolGanador);
        _jugarDeNuevoPendiente = true;
        Serial.println("\n[JUEGO] Cierre de partido disparado (pitido/comentario final)");
    }

    // ---- Invita a jugar de nuevo una vez que terminó de scrollear
    //     el "Fin! Ganador...", y arranca la rotación resultado ↔ texto ----
    if (_jugarDeNuevoPendiente && !displayEnScroll()) {
        _jugarDeNuevoPendiente = false;
        displayTexto(config.textoJugarDeNuevo, config.velocidadScroll);
        _finRotando             = true;
        _finRotandoMuestraTexto = false;   // el próximo cambio muestra el resultado
        _finRotandoUltimoCambio = millis();
    }

    // ---- Partido terminado: rota entre el resultado y "jugar de
    //     nuevo" cada intervaloDisplay segundos, hasta que arranque el próximo ----
    if (_finRotando && !reposoActivo()) {
        if (!partido.terminado) {
            _finRotando = false;   // arrancó de nuevo o se canceló — corta la rotación
        } else if (!displayEnScroll()
                   && millis() - _finRotandoUltimoCambio >= (uint32_t)config.intervaloDisplay * 1000UL) {
            _finRotandoUltimoCambio = millis();
            if (_finRotandoMuestraTexto) displayTexto(config.textoJugarDeNuevo, config.velocidadScroll);
            else                         displayMarcador(partido.goles[0], partido.goles[1]);
            _finRotandoMuestraTexto = !_finRotandoMuestraTexto;
        }
    }

    // ---- Comentarista: corre después de sensores, respeta _proximoComentario ----
    comentaristaLoop(partido);

    // ---- Display: alterna marcador ↔ tiempo restante en modo tiempo, cada uno
    //     quieto (no scrolleando) su ratito de intervaloDisplay segundos ----
    if (partido.activo && config.modoJuego == 1) {
        uint32_t ahora = millis();
        if (ahora - _altUltimoCambio >= (uint32_t)config.intervaloDisplay * 1000UL
            && !displayEnScroll()) {   // no pisar un "Gollll!!!"/"Fin!..." recién disparado
            _altUltimoCambio  = ahora;
            _altMuestraTiempo = !_altMuestraTiempo;
            if (_altMuestraTiempo) {
                uint32_t total    = (uint32_t)config.duracionMin * 60000UL;
                uint32_t elapsed  = ahora - partido.inicio;
                uint32_t restante = (elapsed < total) ? (total - elapsed) : 0;
                displayTiempo(restante);
                _tiempoUltimoTick = ahora;
            } else {
                displayMarcadorConScroll(partido.goles[0], partido.goles[1]);
            }
        } else if (_altMuestraTiempo && ahora - _tiempoUltimoTick >= 1000UL && !displayEnScroll()) {
            // Tickea los segundos en vivo mientras se está mostrando el tiempo,
            // en vez de quedar clavado hasta el próximo cambio de alternancia.
            _tiempoUltimoTick = ahora;
            uint32_t total    = (uint32_t)config.duracionMin * 60000UL;
            uint32_t elapsed  = ahora - partido.inicio;
            uint32_t restante = (elapsed < total) ? (total - elapsed) : 0;
            displayTiempoActualizar(restante);
        }
    }

    // ---- Encoder: rotación — pre-game: alterna modo de juego ----
    if (encChanged) {
        encChanged = false;
        noInterrupts(); int d = encDelta; encDelta = 0; interrupts();
        if (d != 0 && !partido.activo && !partido.pausado) {
            reposoNotificarActividad();
            config.modoJuego = (config.modoJuego == 0) ? 1 : 0;
            displayModo(config.modoJuego == 0 ? "1-GOLES" : "2-TIEMPO");
            Serial.printf("\n[ENCODER] Modo: %s\n", config.modoJuego == 0 ? "goles" : "tiempo");
        }
    }

    // ---- Encoder: botón ----
    {
        uint32_t now = millis();
        bool raw = digitalRead(PIN_SW);
        if (raw != btnRaw) btnDebounceAt = now;
        btnRaw = raw;
        if ((now - btnDebounceAt) >= BTN_DEBOUNCE_MS && raw != btnState) {
            btnState = raw;
            if (btnState == LOW) {   // presionado — inhibir goles durante toda la interacción
                reposoNotificarActividad();
                _ultimoGol1  = now; _ultimoGol2 = now;
                btnPressAt   = now;
                btnLongFired = false;
                noInterrupts(); encDelta = 0; encChanged = false; interrupts();  // descarta ruido mecánico del botón
            }
            if (btnState == HIGH) {   // soltado
                btnPending++;
                btnReleaseAt = now;
            }
        }
        // Long press: 2 segundos mantenido → reset ESP32
        if (btnState == LOW && !btnLongFired && (now - btnPressAt) >= BTN_LONG_MS) {
            btnLongFired = true;
            btnPending   = 0;
            Serial.println("\n[ENCODER] Long press — reiniciando ESP32...");
            delay(200);
            ESP.restart();
        }

        if (btnPending > 0 && (now - btnReleaseAt) >= BTN_DOUBLE_MS) {
            if (_finGolPendiente || _finSinGolPendiente) {
                // Todavía falta sonar el pitido/ganador/comentario final del partido que
                // acaba de terminar (por gol o por tiempo) — se ignora el click para no
                // cancelarlo en silencio (queda acotado por FIN_PARTIDO_MAX_ESPERA_MS,
                // nunca traba del todo).
                Serial.println("\n[ENCODER] Click ignorado — esperando cierre del partido anterior");
                btnPending = 0;
                noInterrupts(); encDelta = 0; encChanged = false; interrupts();
                return;
            }
            if (btnPending == 1) {
                // Click simple
                if (!partido.activo && !partido.terminado && !partido.pausado) {
                    // Standby → iniciar
                    vozPitidoInicio();
                    comentaristaReiniciar();
                    ambienteReiniciar();
                    partido.resetear();
                    partido.activo = true;
                    _altUltimoCambio  = millis();
                    _altMuestraTiempo = false;
                    _ultimoGol1 = millis(); _ultimoGol2 = millis();
                    _prevSensor1 = digitalRead(PIN_SENSOR_CELESTE);
                    _prevSensor2 = digitalRead(PIN_SENSOR_BLANCO);
                    displayTexto(config.textoArranca, config.velocidadScroll);
                    displayMarcador(0, 0);
                    Serial.println("\n[ENCODER] Partido iniciado");
                } else if (partido.activo) {
                    // Jugando → pausar
                    partido.activo  = false;
                    partido.pausado = true;
                    displayTexto(config.textoPausa, config.velocidadScroll);
                    Serial.println("\n[ENCODER] Partido pausado");
                } else if (partido.pausado) {
                    // Pausado → reanudar
                    partido.activo  = true;
                    partido.pausado = false;
                    _ultimoGol1 = millis(); _ultimoGol2 = millis();
                    _prevSensor1 = digitalRead(PIN_SENSOR_CELESTE);
                    _prevSensor2 = digitalRead(PIN_SENSOR_BLANCO);
                    displayTexto(config.textoReanuda, config.velocidadScroll);
                    displayMarcador(partido.goles[0], partido.goles[1]);
                    Serial.println("\n[ENCODER] Partido reanudado");
                } else if (partido.terminado) {
                    // Terminado → nuevo partido
                    _finGolPendiente    = false;
                    _finSinGolPendiente = false;
                    vozPitidoInicio();
                    comentaristaReiniciar();
                    ambienteReiniciar();
                    partido.resetear();
                    partido.activo = true;
                    _altUltimoCambio  = millis();
                    _altMuestraTiempo = false;
                    _ultimoGol1 = millis(); _ultimoGol2 = millis();
                    _prevSensor1 = digitalRead(PIN_SENSOR_CELESTE);
                    _prevSensor2 = digitalRead(PIN_SENSOR_BLANCO);
                    displayTexto(config.textoArranca, config.velocidadScroll);
                    displayMarcador(0, 0);
                    Serial.println("\n[ENCODER] Nuevo partido (desde terminado)");
                }
            } else {
                // Doble click: cancela el partido y vuelve a espera
                _finGolPendiente    = false;
                _finSinGolPendiente = false;
                ambienteReiniciar();
                comentaristaReiniciar();
                partido.activo    = false;
                partido.pausado   = false;
                partido.terminado = false;
                displayTexto(config.textoCancelado, config.velocidadScroll);
                displayMarcador(0, 0);
                Serial.println("\n[ENCODER] Partido cancelado");
            }
            btnPending = 0;
            noInterrupts(); encDelta = 0; encChanged = false; interrupts();  // descarta ruido post-click
        }
    }

    // Fin de partido por tiempo (modo tiempo, sin necesidad de que haya gol) — el
    // marcador/estado cambia ya mismo, apenas se cumple el tiempo; el pitido y el
    // comentario final quedan pendientes hasta que SP1 esté libre (ver más arriba).
    if (partido.activo && config.modoJuego == 1) {
        if ((millis() - partido.inicio) >= (uint32_t)config.duracionMin * 60000UL) {
            partido.activo    = false;
            partido.terminado = true;
            // Sin ambienteReiniciar() acá: el ambiente sigue de fondo durante el
            // pitido y el comentario final (se resetea al arrancar el próximo partido).
            _finSinGolGanador        = partido.ganador();
            _finSinGolPendiente      = true;
            _finSinGolPendienteDesde = millis();
            if (_finSinGolGanador == 0)      Serial.println("\n[JUEGO] ¡Ganó equipo 1! (tiempo)");
            else if (_finSinGolGanador == 1) Serial.println("\n[JUEGO] ¡Ganó equipo 2! (tiempo)");
            else                              Serial.println("\n[JUEGO] ¡Empate! (tiempo)");
            Serial.println("\n[JUEGO] Partido terminado por tiempo — esperando cierre (pitido/comentario)");
        }
    }

    static unsigned long ultimoStats = 0;
    if (millis() - ultimoStats >= (uint32_t)config.intervaloStats * 1000UL) {
        ultimoStats = millis();
        comentaristaStats(partido);
    }
}
