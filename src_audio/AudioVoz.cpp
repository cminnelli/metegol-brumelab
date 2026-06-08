#include "AudioVoz.h"
#include "config.h"
#include <Arduino.h>

#define VOZ_TX 26
#define VOZ_RX 27

static void cmd(uint8_t c, uint8_t ph, uint8_t pl) {
    uint8_t buf[10];
    buf[0]=0x7E; buf[1]=0xFF; buf[2]=0x06; buf[3]=c;
    buf[4]=0x00; buf[5]=ph;   buf[6]=pl;
    int16_t cs = -(int16_t)(buf[1]+buf[2]+buf[3]+buf[4]+buf[5]+buf[6]);
    buf[7]=(cs>>8)&0xFF; buf[8]=cs&0xFF; buf[9]=0xEF;
    Serial.printf("[SP1 TX] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9]);
    Serial2.write(buf, 10);
    delay(150);
}

void vozBegin() {
    Serial2.begin(9600, SERIAL_8N1, VOZ_RX, VOZ_TX);
}

void vozPlay(Pista pista) {
    Serial.printf("[SP1] ▶ Pista %d\n", (int)pista);
    cmd(0x03, 0x00, (uint8_t)pista);
}

void vozPoll() {
    static uint8_t buf[10], idx = 0;
    while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (b == 0x7E) idx = 0;
        if (idx < 10) buf[idx++] = b;
        if (idx >= 10 && buf[9] == 0xEF) {
            uint8_t tipo = buf[3], val = buf[6];
            switch (tipo) {
                case 0x3D: Serial.printf("[SP1] ✓ Pista %d terminada\n", val); break;
                case 0x3F: Serial.println("[SP1] SD online ✓");                 break;
                case 0x40: if (val != 0x03) Serial.printf("[SP1] ✗ Error 0x%02X\n", val); break;
                default: break;
            }
            idx = 0;
        }
    }
}
