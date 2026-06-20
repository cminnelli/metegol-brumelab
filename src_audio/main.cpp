#include <Arduino.h>
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Partido.h"
#include "WebConfig.h"
#include "Comentarista.h"
#include "Display.h"
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

// Display tiempo scrolleante en modo tiempo
static uint32_t _altUltimoCambio = 0;

// Sensores de gol — file-scope para poder resetear al iniciar partido
static bool          _prevSensor1  = HIGH;
static bool          _prevSensor2  = HIGH;
static unsigned long _ultimoGol1   = 0;
static unsigned long _ultimoGol2   = 0;
static unsigned long _sensor1LowAt = 0;   // momento en que sensor1 bajó a LOW
static unsigned long _sensor2LowAt = 0;
#define SENSOR_MIN_LOW_MS 20              // duración mínima LOW para validar gol


void setup() {
    Serial.begin(115200);
    Serial.println("=== HOLA BRUMELAB! ===");
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
    displayInit();
    displayModo(config.modoJuego == 0 ? "1-GOLES" : "2-TIEMPO");

    partido.resetear();
    // Descarta cualquier ruido del encoder acumulado durante el boot
    noInterrupts(); encDelta = 0; encChanged = false; interrupts();
    Serial.println("[2] Sistema listo");
}

void loop() {
    webConfigLoop();
    displayTick();
    vozPoll();
    ambientePoll();

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
        if (millis() - _ultimoGol1 >= 5000) {
            if (cur1 == LOW && _prevSensor1 == HIGH) {
                _ultimoGol1 = millis();
                partido.registrarGol(0);
                Serial.printf("\n──── GOL!  %d ─ %d  ──────────────────────────\n",
                    partido.goles[0], partido.goles[1]);
                Serial.printf("     Celeste anota!\n");
                ambienteOnGol();
                displayMarcador(partido.goles[0], partido.goles[1]);
                if (partido.terminado) {
                    int8_t w = partido.ganador();
                    Serial.printf("     → FINAL  |  %s\n",
                        w == 0 ? "Ganó Celeste!" : w == 1 ? "Ganó Blanco!" : "Empate!");
                    vozPitidoFinal();
                    comentaristaFinalPartido(partido);
                    displayGanador(w);
                } else {
                    comentaristaOnGol(partido);
                    displayGol();
                }
            }
        }
        if (millis() - _ultimoGol2 >= 5000) {
            if (cur2 == LOW && _prevSensor2 == HIGH) {
                _ultimoGol2 = millis();
                partido.registrarGol(1);
                Serial.printf("\n──── GOL!  %d ─ %d  ──────────────────────────\n",
                    partido.goles[0], partido.goles[1]);
                Serial.printf("     Blanco anota!\n");
                ambienteOnGol();
                displayMarcador(partido.goles[0], partido.goles[1]);
                if (partido.terminado) {
                    int8_t w = partido.ganador();
                    Serial.printf("     → FINAL  |  %s\n",
                        w == 0 ? "Ganó Celeste!" : w == 1 ? "Ganó Blanco!" : "Empate!");
                    vozPitidoFinal();
                    comentaristaFinalPartido(partido);
                    displayGanador(w);
                } else {
                    comentaristaOnGol(partido);
                    displayGol();
                }
            }
        }
    }
    _prevSensor1 = cur1;
    _prevSensor2 = cur2;

    // ---- Comentarista: corre después de sensores, respeta _proximoComentario ----
    comentaristaLoop(partido);

    // ---- Display: scrollea el tiempo restante cada intervaloDisplay segundos ----
    if (partido.activo && config.modoJuego == 1) {
        uint32_t ahora = millis();
        if (ahora - _altUltimoCambio >= (uint32_t)config.intervaloDisplay * 1000UL) {
            _altUltimoCambio = ahora;
            uint32_t total    = (uint32_t)config.duracionMin * 60000UL;
            uint32_t elapsed  = ahora - partido.inicio;
            uint32_t restante = (elapsed < total) ? (total - elapsed) : 0;
            displayTiempo(restante);
            // Al terminar el scroll, displayTick vuelve al marcador automáticamente
        }
    }

    // ---- Encoder: rotación — pre-game: alterna modo de juego ----
    if (encChanged) {
        encChanged = false;
        noInterrupts(); int d = encDelta; encDelta = 0; interrupts();
        if (d != 0 && !partido.activo && !partido.pausado) {
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
            if (btnPending == 1) {
                // Click simple
                if (!partido.activo && !partido.terminado && !partido.pausado) {
                    // Standby → iniciar
                    vozPitidoInicio();
                    comentaristaReiniciar();
                    ambienteReiniciar();
                    partido.resetear();
                    partido.activo = true;
                    _altUltimoCambio = millis();
                    _ultimoGol1 = millis(); _ultimoGol2 = millis();
                    _prevSensor1 = digitalRead(PIN_SENSOR_CELESTE);
                    _prevSensor2 = digitalRead(PIN_SENSOR_BLANCO);
                    displayTexto("ARRANCAAA!", config.velocidadScroll);
                    displayMarcador(0, 0);
                    Serial.println("\n[ENCODER] Partido iniciado");
                } else if (partido.activo) {
                    // Jugando → pausar
                    partido.activo  = false;
                    partido.pausado = true;
                    displayTexto("PAUSA!", config.velocidadScroll);
                    Serial.println("\n[ENCODER] Partido pausado");
                } else if (partido.pausado) {
                    // Pausado → reanudar
                    partido.activo  = true;
                    partido.pausado = false;
                    _ultimoGol1 = millis(); _ultimoGol2 = millis();
                    _prevSensor1 = digitalRead(PIN_SENSOR_CELESTE);
                    _prevSensor2 = digitalRead(PIN_SENSOR_BLANCO);
                    displayTexto("VAMOS!", config.velocidadScroll);
                    displayMarcador(partido.goles[0], partido.goles[1]);
                    Serial.println("\n[ENCODER] Partido reanudado");
                } else if (partido.terminado) {
                    // Terminado → nuevo partido
                    vozPitidoInicio();
                    comentaristaReiniciar();
                    ambienteReiniciar();
                    partido.resetear();
                    partido.activo = true;
                    _altUltimoCambio = millis();
                    _ultimoGol1 = millis(); _ultimoGol2 = millis();
                    _prevSensor1 = digitalRead(PIN_SENSOR_CELESTE);
                    _prevSensor2 = digitalRead(PIN_SENSOR_BLANCO);
                    displayTexto("ARRANCAAA!", config.velocidadScroll);
                    displayMarcador(0, 0);
                    Serial.println("\n[ENCODER] Nuevo partido (desde terminado)");
                }
            } else {
                // Doble click: cancela el partido y vuelve a espera
                ambienteReiniciar();
                comentaristaReiniciar();
                partido.activo    = false;
                partido.pausado   = false;
                partido.terminado = false;
                displayTexto("CANCELADO!", config.velocidadScroll);
                displayMarcador(0, 0);
                Serial.println("\n[ENCODER] Partido cancelado");
            }
            btnPending = 0;
            noInterrupts(); encDelta = 0; encChanged = false; interrupts();  // descarta ruido post-click
        }
    }

    // Fin de partido por tiempo (modo tiempo, sin necesidad de que haya gol)
    if (partido.activo && config.modoJuego == 1) {
        if ((millis() - partido.inicio) >= (uint32_t)config.duracionMin * 60000UL) {
            partido.activo    = false;
            partido.terminado = true;
            int8_t w = partido.ganador();
            vozPitidoFinal();
            comentaristaFinalPartido(partido);
            displayGanador(w);
            if (w == 0)      Serial.println("\n[JUEGO] ¡Ganó equipo 1! (tiempo)");
            else if (w == 1) Serial.println("\n[JUEGO] ¡Ganó equipo 2! (tiempo)");
            else             Serial.println("\n[JUEGO] ¡Empate! (tiempo)");
            Serial.println("\n[JUEGO] Partido finalizado por tiempo");
        }
    }

    static unsigned long ultimoStats = 0;
    if (millis() - ultimoStats >= (uint32_t)config.intervaloStats * 1000UL) {
        ultimoStats = millis();
        comentaristaStats(partido);
    }
}
