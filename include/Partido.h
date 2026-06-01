#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

class Partido {
public:
    uint8_t goles[2] = {0, 0};
    uint32_t inicio  = 0;

    void    sumarGol(uint8_t equipo);
    void    resetear();
    void    getResultado(char* buf, size_t len) const;
    bool    termino() const;
    int8_t  ganador() const;   // 0, 1, o -1 si empate
};
