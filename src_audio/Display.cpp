#include "Display.h"
#include <Arduino.h>

// DIN → GPIO 23 | CLK → GPIO 18 | CS → GPIO 5
#define PIN_DIN  23
#define PIN_CLK  18
#define PIN_CS    5
#define MODULOS   4

static void shiftByte(uint8_t data) {
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(PIN_CLK, LOW);
        digitalWrite(PIN_DIN, (data >> i) & 1);
        delayMicroseconds(2);
        digitalWrite(PIN_CLK, HIGH);
        delayMicroseconds(2);
    }
}

static void rawCmd(uint8_t reg, uint8_t data) {
    digitalWrite(PIN_CS, LOW);
    delayMicroseconds(2);
    for (uint8_t i = 0; i < MODULOS; i++) {
        shiftByte(reg);
        shiftByte(data);
    }
    delayMicroseconds(2);
    digitalWrite(PIN_CS, HIGH);
    delayMicroseconds(100);
}

void displayInit() {
    pinMode(PIN_DIN, OUTPUT);
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_CS,  OUTPUT);
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_CS,  HIGH);
    delay(100);

    rawCmd(0x0C, 0x00);  // Shutdown
    rawCmd(0x0F, 0x00);  // Display Test OFF
    rawCmd(0x09, 0x00);  // Sin decodificación BCD
    rawCmd(0x0A, 0x05);  // Brillo: 5/15
    rawCmd(0x0B, 0x07);  // Scan limit: 8 filas
    for (uint8_t row = 1; row <= 8; row++) rawCmd(row, 0x00);  // limpiar filas
    rawCmd(0x0C, 0x01);  // Normal (encender)

    Serial.println("[DISP] MAX7219 listo");
}

static uint32_t _apagarEn = 0;

void displayTick() {
    if (_apagarEn && millis() >= _apagarEn) {
        _apagarEn = 0;
        rawCmd(0x0F, 0x00);  // Display Test OFF → apagado
    }
}

void displayTexto(const char* texto, uint16_t velocidad) {}
void displayMarcador(uint8_t local, uint8_t visitante) {}

void displayGol() {
    rawCmd(0x0F, 0x01);       // todos encendidos
    _apagarEn = millis() + 2000;  // apagar en 2 segundos sin bloquear
    Serial.println("[DISP] Flash GOL!");
}
