#include "Display.h"
#include "WebConfig.h"
#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>

#define HW_TYPE     MD_MAX72XX::FC16_HW
#define DATA_PIN    23
#define CLK_PIN     18
#define CS_PIN       5
#define MAX_DEVICES  4

static MD_Parola disp(HW_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

static char _marcador[8] = "0-0";
static bool _enScroll = false;

void displayInit() {
    Serial.println("[DISP] begin..."); Serial.flush();
    disp.begin();
    Serial.printf("[DISP] brillo=%d velocidad=%d\n", config.brillo, config.velocidadScroll); Serial.flush();

    // Animacion de intro: pulso de brillo
    disp.setIntensity(0);
    for (uint8_t i = 0; i <= 15; i++) { disp.setIntensity(i); delay(18); }
    for (uint8_t i = 15; i > config.brillo; i--) { disp.setIntensity(i); delay(18); }
    disp.setIntensity(config.brillo);

    disp.displayScroll("Hola Brumelab!", PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
    Serial.println("[DISP] scroll Hola Brumelab! iniciado"); Serial.flush();
}

void displayTick() {
    if (disp.displayAnimate()) {
        if (_enScroll) {
            disp.displayText(_marcador, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
            _enScroll = false;
        }
    }
}

void displayTexto(const char* texto, uint16_t velocidad) {
    disp.displayScroll(texto, PA_LEFT, PA_SCROLL_LEFT, velocidad);
    _enScroll = true;
}

void displayMarcador(uint8_t local, uint8_t visitante) {
    snprintf(_marcador, sizeof(_marcador), "%d-%d", local, visitante);
    if (!_enScroll) {
        disp.displayText(_marcador, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    }
}

void displayGol() {
    disp.displayScroll("Gollll!!!", PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
    Serial.println("[DISP] Gollll!!!");
}

void displayGanador(int8_t w) {
    if (w == 0)      disp.displayScroll("Ganador Celeste!", PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    else if (w == 1) disp.displayScroll("Ganador Blanco!",  PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    else             disp.displayScroll("Empate!",           PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
}
