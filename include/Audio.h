#pragma once

enum class Pista { GOL = 1 };

void audioInit();
void reproducir(Pista pista);
void audioStop();
void audioPoll();  // leer respuestas del DFPlayer y loguear errores
