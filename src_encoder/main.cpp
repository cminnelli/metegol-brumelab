#include <Arduino.h>

#define PIN_CLK  32
#define PIN_DT   33
#define PIN_SW   25

// ---- Encoder (ISR) ----
volatile int  encDelta   = 0;
volatile bool encChanged = false;

void IRAM_ATTR onClk() {
    // Leemos DT inmediatamente en la ISR para capturar la dirección
    if (digitalRead(PIN_DT) == LOW)
        encDelta++;
    else
        encDelta--;
    encChanged = true;
}

// ---- Botón ----
const uint32_t DEBOUNCE_MS     = 40;
const uint32_t DOUBLE_CLICK_MS = 350;
const uint32_t LONG_PRESS_MS   = 700;

bool     btnState      = HIGH;
bool     lastBtnRaw    = HIGH;
uint32_t debounceAt    = 0;

uint32_t pressedAt     = 0;
bool     longFired     = false;

uint32_t lastReleaseAt = 0;
int      pendingClicks = 0;

void setup() {
    Serial.begin(115200);
    delay(500);   // espera que el monitor suba

    pinMode(PIN_CLK, INPUT_PULLUP);
    pinMode(PIN_DT,  INPUT_PULLUP);
    pinMode(PIN_SW,  INPUT_PULLUP);

    // FALLING: cuando CLK baja es el momento más estable para leer DT
    attachInterrupt(digitalPinToInterrupt(PIN_CLK), onClk, FALLING);

    Serial.println("\n=== Encoder KY-040 Test ===");
    Serial.printf("CLK=%d  DT=%d  SW=%d\n", PIN_CLK, PIN_DT, PIN_SW);
    Serial.println("Gira o presioná el botón...\n");
}

void loop() {
    uint32_t now = millis();

    // ---- Encoder: consumir delta acumulado por ISR ----
    if (encChanged) {
        encChanged = false;
        int delta;
        noInterrupts();
        delta    = encDelta;
        encDelta = 0;
        interrupts();

        // El KY-040 suele generar 2 pulsos por detent; mostramos el total
        if (delta > 0)
            Serial.printf("[ENC] Derecha   +%d\n", delta);
        else
            Serial.printf("[ENC] Izquierda  %d\n", delta);
    }

    // ---- Botón: debounce por tiempo ----
    bool raw = digitalRead(PIN_SW);
    if (raw != lastBtnRaw) debounceAt = now;   // reset al detectar cambio
    lastBtnRaw = raw;

    if ((now - debounceAt) >= DEBOUNCE_MS && raw != btnState) {
        btnState = raw;

        if (btnState == LOW) {
            // Flanco de bajada → presionado
            pressedAt = now;
            longFired = false;
        } else {
            // Flanco de subida → soltado
            if (!longFired) {
                pendingClicks++;
                lastReleaseAt = now;
            }
        }
    }

    // Long press: detectar mientras el botón sigue presionado
    if (btnState == LOW && !longFired && (now - pressedAt) >= LONG_PRESS_MS) {
        Serial.println("[BTN] Mantener (long press)");
        longFired     = true;
        pendingClicks = 0;
    }

    // Resolver click/doble-click al vencer el timeout de espera
    if (pendingClicks > 0 && (now - lastReleaseAt) >= DOUBLE_CLICK_MS) {
        if (pendingClicks == 1)
            Serial.println("[BTN] Click");
        else
            Serial.printf("[BTN] Doble click (%d)\n", pendingClicks);
        pendingClicks = 0;
    }
}
