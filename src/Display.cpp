#include "Display.h"
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>

// ESP32 WROOM-32 — SPI hardware
// DIN → GPIO 23 | CLK → GPIO 18 | CS → GPIO 5
#define PIN_CS       5
#define MODULOS      4   // 8x32 = 4 módulos de 8x8

// FC16_HW: módulo rojo genérico (el más común).
// Si los caracteres salen al revés, cambiar a MD_MAX72XX::PAROLA_HW
static MD_Parola pantalla(MD_MAX72XX::FC16_HW, PIN_CS, MODULOS);

static char bufTexto[32];

void displayInit() {
    pantalla.begin();
    pantalla.setIntensity(5);
    pantalla.displayClear();
}

void displayTick() {
    pantalla.displayAnimate();
}

void displayTexto(const char* texto, uint16_t velocidad) {
    strncpy(bufTexto, texto, sizeof(bufTexto) - 1);
    bufTexto[sizeof(bufTexto) - 1] = '\0';
    pantalla.displayScroll(bufTexto, PA_LEFT, PA_SCROLL_LEFT, velocidad);
}

void displayMarcador(uint8_t local, uint8_t visitante) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d-%d", local, visitante);
    pantalla.displayClear();
    pantalla.displayText(buf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    pantalla.displayAnimate();
}

void displayGol() {
    displayTexto(" GOL! GOL! GOL! ", 30);
}
