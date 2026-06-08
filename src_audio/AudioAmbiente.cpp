#include "AudioAmbiente.h"
#include "config.h"
#include <Arduino.h>

#define AMB_TX 16
#define AMB_RX 17

static uint8_t _pistaActual = 3;  // pista ambiente por defecto

static void cmd(uint8_t c, uint8_t ph, uint8_t pl) {
    uint8_t buf[10];
    buf[0]=0x7E; buf[1]=0xFF; buf[2]=0x06; buf[3]=c;
    buf[4]=0x00; buf[5]=ph;   buf[6]=pl;
    int16_t cs = -(int16_t)(buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]);
    buf[7]=(cs>>8)&0xFF; buf[8]=cs&0xFF; buf[9]=0xEF;
    Serial.printf("[SP2 TX] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9]);
    Serial1.write(buf, 10);
    delay(150);
}

void ambienteBegin() {
    Serial1.begin(9600, SERIAL_8N1, AMB_RX, AMB_TX);
    Serial.println("[SP2] Serial1 listo");
}

void ambientePlay(uint8_t pista) {
    _pistaActual = pista;
    Serial.printf("[SP2] ▶ Pista %d (ambiente)\n", pista);
    cmd(0x06, 0x00, VOLUMEN);
    delay(300);
    cmd(0x03, 0x00, pista);
}

void ambientePoll() {
    static uint8_t buf[10], idx = 0;
    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        if (b == 0x7E) idx = 0;
        if (idx < 10) buf[idx++] = b;
        if (idx >= 10 && buf[9] == 0xEF) {
            uint8_t tipo = buf[3], val = buf[6];
            switch (tipo) {
                case 0x3D: Serial.printf("[SP2] ✓ Pista %d terminada — reloop\n", val);
                           cmd(0x03, 0x00, _pistaActual);  // loop manual
                           break;
                case 0x3F: Serial.println("[SP2] SD online ✓");                 break;
                case 0x40: Serial.printf("[SP2] ✗ Error 0x%02X\n", val);        break;
                default:   Serial.printf("[SP2] RX 0x%02X\n", tipo);            break;
            }
            idx = 0;
        }
    }
}
