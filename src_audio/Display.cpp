#include "Display.h"
#include "WebConfig.h"
#include <Arduino.h>
#include <string.h>
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

// La fuente de MD_MAX72XX es solo ASCII — un nombre con tilde o "ñ" (2 bytes en
// UTF-8) se ve como un cuadrito o corta el texto. Traduce los acentos españoles
// más comunes a su equivalente ASCII y descarta cualquier otro byte no-ASCII.
static void asciiSanitize(const char* in, char* out, size_t outLen) {
    size_t oi = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && oi + 1 < outLen; p++) {
        if (*p < 0x80) { out[oi++] = (char)*p; continue; }
        if (*p == 0xC3 && *(p + 1)) {   // UTF-8 2 bytes: bloque Latin-1 Supplement
            unsigned char c2 = *(++p);
            char rep = 0;
            switch (c2) {
                case 0xA1: rep = 'a'; break;  // á
                case 0xA9: rep = 'e'; break;  // é
                case 0xAD: rep = 'i'; break;  // í
                case 0xB3: rep = 'o'; break;  // ó
                case 0xBA: rep = 'u'; break;  // ú
                case 0xB1: rep = 'n'; break;  // ñ
                case 0xBC: rep = 'u'; break;  // ü
                case 0x81: rep = 'A'; break;  // Á
                case 0x89: rep = 'E'; break;  // É
                case 0x8D: rep = 'I'; break;  // Í
                case 0x93: rep = 'O'; break;  // Ó
                case 0x9A: rep = 'U'; break;  // Ú
                case 0x91: rep = 'N'; break;  // Ñ
                case 0x9C: rep = 'U'; break;  // Ü
                default:   rep = 0;   break;
            }
            if (rep) out[oi++] = rep;
        }
        // otro byte no-ASCII: se descarta silenciosamente
    }
    out[oi] = '\0';
}

// Todo texto scrolleado pasa por acá — reutiliza un único buffer estático porque
// MD_Parola guarda el puntero, no una copia, y cada llamada dispara un scroll nuevo.
static void scrollSanitizado(const char* texto, textPosition_t align, textEffect_t efecto, uint16_t velocidad) {
    static char buf[64];
    asciiSanitize(texto, buf, sizeof(buf));
    // Espacio final para que el texto no quede pegado contra lo que venga después
    // (el marcador, el próximo scroll, etc.)
    size_t len = strlen(buf);
    if (len + 1 < sizeof(buf)) { buf[len] = ' '; buf[len + 1] = '\0'; }
    disp.displayScroll(buf, align, efecto, velocidad);
}

void displayInit() {
    Serial.println("[DISP] begin..."); Serial.flush();
    disp.begin();
    Serial.printf("[DISP] brillo=%d velocidad=%d\n", config.brillo, config.velocidadScroll); Serial.flush();

    // Animacion de intro: pulso de brillo
    disp.setIntensity(0);
    for (uint8_t i = 0; i <= 15; i++) { disp.setIntensity(i); delay(18); }
    for (uint8_t i = 15; i > config.brillo; i--) { disp.setIntensity(i); delay(18); }
    disp.setIntensity(config.brillo);

    scrollSanitizado(config.textoBoot, PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
    Serial.printf("[DISP] scroll '%s' iniciado\n", config.textoBoot); Serial.flush();
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
    scrollSanitizado(texto, PA_LEFT, PA_SCROLL_LEFT, velocidad);
    _enScroll = true;
}

void displayMarcador(uint8_t local, uint8_t visitante) {
    snprintf(_marcador, sizeof(_marcador), "%d-%d", local, visitante);
    if (!_enScroll) {
        disp.displayText(_marcador, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    }
}

void displayGol() {
    scrollSanitizado(config.textoGol, PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
}

void displayGanador(int8_t w) {
    if (w == 0)      scrollSanitizado(config.textoGanadorCeleste, PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    else if (w == 1) scrollSanitizado(config.textoGanadorBlanco,  PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    else             scrollSanitizado(config.textoEmpate,         PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
}

void displayModo(const char* texto) {
    scrollSanitizado(texto, PA_LEFT, PA_SCROLL_RIGHT, config.velocidadScroll);
    _enScroll = true;
}

void displayTiempo(uint32_t ms) {
    static char buf[10];   // estático: MD_Parola guarda el puntero, no una copia
    uint32_t seg = ms / 1000;
    snprintf(buf, sizeof(buf), "%02lu:%02lu ", seg / 60, seg % 60);   // espacio final
    disp.displayScroll(buf, PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    _enScroll = true;
}

bool displayEnScroll() {
    return _enScroll;
}
