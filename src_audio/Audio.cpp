#include "Audio.h"
#include "config.h"
#include <Arduino.h>

// ── Speaker 1 — efectos de gol ────────────────────────────────────────────────
#define AUDIO_TX  26   // ESP32 TX → DFPlayer 1 RX
#define AUDIO_RX  27   // ESP32 RX ← DFPlayer 1 TX

// ── Speaker 2 — sonido ambiente ───────────────────────────────────────────────
#define AUDIO2_TX 16   // ESP32 TX → DFPlayer 2 RX
#define AUDIO2_RX 17   // ESP32 RX ← DFPlayer 2 TX

static void dfCmd(uint8_t cmd, uint8_t paramH, uint8_t paramL) {
    uint8_t buf[10];
    buf[0] = 0x7E; buf[1] = 0xFF; buf[2] = 0x06; buf[3] = cmd;
    buf[4] = 0x00; buf[5] = paramH; buf[6] = paramL;
    int16_t cs = -(int16_t)(buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]);
    buf[7] = (uint8_t)((cs >> 8) & 0xFF);
    buf[8] = (uint8_t)(cs & 0xFF);
    buf[9] = 0xEF;
    Serial.printf("[SP1 TX] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9]);
    Serial2.write(buf, 10);
    delay(150);
}

static void df2Cmd(uint8_t cmd, uint8_t paramH, uint8_t paramL) {
    uint8_t buf[10];
    buf[0] = 0x7E; buf[1] = 0xFF; buf[2] = 0x06; buf[3] = cmd;
    buf[4] = 0x00; buf[5] = paramH; buf[6] = paramL;
    int16_t cs = -(int16_t)(buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]);
    buf[7] = (uint8_t)((cs >> 8) & 0xFF);
    buf[8] = (uint8_t)(cs & 0xFF);
    buf[9] = 0xEF;
    Serial.printf("[SP2 TX] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9]);
    Serial1.write(buf, 10);
    delay(150);
}

void audioInit() {
    Serial2.begin(9600, SERIAL_8N1, AUDIO_RX, AUDIO_TX);
}

void audioAmbienteBegin() {
    Serial1.begin(9600, SERIAL_8N1, AUDIO2_RX, AUDIO2_TX);
    Serial.println("[SP2] Serial1 listo");
}

void reproducir(Pista pista) {
    Serial.printf("[SP1] ▶ Reproduciendo pista %d\n", (int)pista);
    dfCmd(0x03, 0x00, (uint8_t)pista);
}

void audioStop() {
    dfCmd(0x16, 0x00, 0x00);
}

void audioPoll() {
    static uint8_t buf[10];
    static uint8_t idx = 0;

    while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (b == 0x7E) idx = 0;
        if (idx < 10) buf[idx++] = b;
        if (idx >= 10 && buf[9] == 0xEF) {
            uint8_t tipo = buf[3];
            uint8_t val  = buf[6];
            switch (tipo) {
                case 0x3D: Serial.printf("[SP1] ✓ Pista %d terminada\n", val);            break;
                case 0x3F: Serial.println("[SP1] SD card online ✓");                       break;
                case 0x40: if (val != 0x03) Serial.printf("[SP1] ✗ Error: 0x%02X\n", val); break;
                default:   break;
            }
            idx = 0;
        }
    }
}

void audioAmbientePoll() {
    static uint8_t buf[10];
    static uint8_t idx = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        if (b == 0x7E) idx = 0;
        if (idx < 10) buf[idx++] = b;
        if (idx >= 10 && buf[9] == 0xEF) {
            uint8_t tipo = buf[3];
            uint8_t val  = buf[6];
            switch (tipo) {
                case 0x3D: Serial.printf("[SP2] ✓ Pista %d terminada — reloop\n", val);
                           df2Cmd(0x03, 0x00, 0x03);  // loop manual 0002.mp3
                           break;
                case 0x3F: Serial.println("[SP2] SD card online ✓");                       break;
                case 0x40: Serial.printf("[SP2] ✗ Error: 0x%02X\n", val);                  break;
                default:   Serial.printf("[SP2] RX tipo=0x%02X val=0x%02X\n", tipo, val);  break;
            }
            idx = 0;
        }
    }
}

void audioInitAll() {
    Serial.println("[Audio] Boot SP1+SP2 simultáneo (2 s)...");
    for (uint16_t i = 0; i < 200; i++) {
        audioPoll();
        audioAmbientePoll();
        delay(10);
    }

    Serial.println("[SP1] Configurando volumen...");
    dfCmd(0x06, 0x00, VOLUMEN);
    for (uint8_t i = 0; i < 30; i++) { audioPoll(); audioAmbientePoll(); delay(10); }
    Serial.println("[SP1] Listo ✓");

    Serial.println("[SP2] Configurando volumen...");
    df2Cmd(0x06, 0x00, VOLUMEN);
    for (uint8_t i = 0; i < 30; i++) { audioPoll(); audioAmbientePoll(); delay(10); }

    Serial.println("[SP2] Iniciando 0002.mp3 ambiente...");
    df2Cmd(0x03, 0x00, 0x03);  // play pista 2 = 0002.mp3
    Serial.println("[SP2] Listo ✓");
}
