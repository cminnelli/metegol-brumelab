#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>

// Test aislado de la matriz LED — sin WiFi, sin audio, sin sensores.
// Mismos pines que el proyecto real. Arrancar con MAX_DEVICES=1 y
// solo la placa 1 conectada; si funciona, subir de a una placa mas.

#define HW_TYPE     MD_MAX72XX::FC16_HW
#define DATA_PIN    23
#define CLK_PIN     18
#define CS_PIN       5
#define MAX_DEVICES  1   // <-- cambiar a 2, 3, 4 a medida que se suman placas

MD_Parola disp(HW_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== TEST MATRIZ AISLADO ===");
    Serial.printf("MAX_DEVICES=%d  DATA=%d CLK=%d CS=%d\n", MAX_DEVICES, DATA_PIN, CLK_PIN, CS_PIN);

    bool ok = disp.begin();
    Serial.printf("disp.begin() = %s\n", ok ? "OK" : "FALLO");

    disp.setIntensity(10);   // brillo bien arriba para descartar problema de intensidad
    disp.displayClear();
    disp.displayText("TEST", PA_CENTER, 40, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void loop() {
    if (disp.displayAnimate()) {
        disp.displayReset();
    }
}
