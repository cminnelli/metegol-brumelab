#include <Arduino.h>
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Partido.h"
#include "WebConfig.h"
#include "Comentarista.h"
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
static bool     btnState      = HIGH;
static bool     btnRaw        = HIGH;
static uint32_t btnDebounceAt = 0;
static uint32_t btnReleaseAt  = 0;
static int      btnPending    = 0;

Partido partido;


void setup() {
    Serial.begin(115200);
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
    ambientePlay(config.pistaAmbiente);

    partido.resetear();
    Serial.println("[2] Sistema listo");
}

void loop() {
    webConfigLoop();
    vozPoll();
    ambientePoll();

    // ---- Sensores primero: si hay gol, registrarGol empuja _proximoComentario
    //     hacia el futuro antes de que comentaristaLoop pueda disparar un track ----
    static bool prev1 = HIGH, prev2 = HIGH;
    static unsigned long ultimoGol1 = 0;
    static unsigned long ultimoGol2 = 0;
    bool cur1 = digitalRead(PIN_SENSOR_CELESTE);
    bool cur2 = digitalRead(PIN_SENSOR_BLANCO);

    if (partido.activo) {
        if (millis() - ultimoGol1 >= 2000) {
            if (cur1 == LOW && prev1 == HIGH) {
                ultimoGol1 = millis();
                partido.registrarGol(0);
            }
        }
        if (millis() - ultimoGol2 >= 2000) {
            if (cur2 == LOW && prev2 == HIGH) {
                ultimoGol2 = millis();
                partido.registrarGol(1);
            }
        }
    }
    prev1 = cur1;
    prev2 = cur2;

    // ---- Comentarista: corre después de sensores, respeta _proximoComentario ----
    comentaristaLoop(partido);

    // ---- Encoder: rotación (descartada por ahora) ----
    if (encChanged) {
        encChanged = false;
        noInterrupts(); encDelta = 0; interrupts();
    }

    // ---- Encoder: botón ----
    {
        uint32_t now = millis();
        bool raw = digitalRead(PIN_SW);
        if (raw != btnRaw) btnDebounceAt = now;
        btnRaw = raw;
        if ((now - btnDebounceAt) >= BTN_DEBOUNCE_MS && raw != btnState) {
            btnState = raw;
            if (btnState == HIGH) {   // soltado
                btnPending++;
                btnReleaseAt = now;
            }
        }
        if (btnPending > 0 && (now - btnReleaseAt) >= BTN_DOUBLE_MS) {
            if (btnPending == 1) {
                // Click: toggle inicio / pausa / reanuda
                if (!partido.activo && !partido.terminado && !partido.pausado) {
                    comentaristaReiniciar();
                    partido.resetear();
                    partido.activo = true;
                    ambientePlay(config.pistaAmbiente);
                    Serial.println("[ENC] Partido iniciado");
                } else if (partido.activo) {
                    partido.activo  = false;
                    partido.pausado = true;
                    Serial.println("[ENC] Partido pausado");
                } else if (partido.pausado) {
                    partido.activo  = true;
                    partido.pausado = false;
                    Serial.println("[ENC] Partido reanudado");
                } else if (partido.terminado) {
                    comentaristaReiniciar();
                    partido.resetear();
                    partido.activo    = true;
                    partido.terminado = false;
                    ambientePlay(config.pistaAmbiente);
                    Serial.println("[ENC] Nuevo partido iniciado");
                }
            } else {
                // Doble click: reset → en espera
                partido.resetear();
                partido.activo    = false;
                partido.terminado = false;
                Serial.println("[ENC] Reset → En espera");
            }
            btnPending = 0;
        }
    }

    // Fin de partido por tiempo (modo tiempo, sin necesidad de que haya gol)
    if (partido.activo && config.modoJuego == 1) {
        if ((millis() - partido.inicio) >= (uint32_t)config.duracionMin * 60000UL) {
            partido.activo    = false;
            partido.terminado = true;
            int8_t w = partido.ganador();
            if (w == 0)      Serial.println("[JUEGO] ¡Ganó equipo 1! (tiempo)");
            else if (w == 1) Serial.println("[JUEGO] ¡Ganó equipo 2! (tiempo)");
            else             Serial.println("[JUEGO] ¡Empate! (tiempo)");
            Serial.println("[JUEGO] Partido finalizado por tiempo");
        }
    }

    static unsigned long ultimoStats = 0;
    if (millis() - ultimoStats >= (uint32_t)config.intervaloStats * 1000UL) {
        ultimoStats = millis();
        comentaristaStats(partido);
    }
}
