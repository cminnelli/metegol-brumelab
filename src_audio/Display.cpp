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

// Lo que está quieto en pantalla ahora mismo (marcador o tiempo restante) —
// hace falta saberlo para poder hacerlo "salir" scrolleando antes de que entre
// lo próximo (ver iniciarSalida más abajo).
static char _actualMostrado[8] = "";

// Cuando termina un scroll normal (Gollll!!!, PAUSA!, etc.), displayTick() vuelve
// sola al marcador — eso está bien para esos casos. Pero la alternancia
// marcador↔tiempo de modo tiempo (ver displayMarcadorConScroll/displayTiempo) quiere
// que lo que acaba de entrar se quede quieto tal cual, sin que nadie lo pise con el
// marcador. Esta bandera le avisa a displayTick() que no lo haga esta vez.
static bool _mantenerTrasScroll = false;

// Cola de un solo lugar: si pide un scroll/entrada nueva mientras ya hay una en
// curso, no la pisa a mitad de camino (quedaba solapado/cortado, se ve mal) — la
// guarda acá y displayTick() la arranca sola, apenas termina la que está en
// curso. Si llega un tercer pedido antes de que eso pase, pisa al que estaba en
// cola: solo importa el último.
enum class TipoPendiente : uint8_t { NINGUNO, SCROLL, ENTRADA_MARCADOR, ENTRADA_TIEMPO };
struct Pendiente {
    TipoPendiente  tipo = TipoPendiente::NINGUNO;
    char           texto[64];        // TipoPendiente::SCROLL
    textPosition_t align;
    textEffect_t   efecto;
    uint16_t       velocidad;
    uint8_t        golLocal, golVisitante;  // TipoPendiente::ENTRADA_MARCADOR
    uint32_t       tiempoMs;                // TipoPendiente::ENTRADA_TIEMPO
};
static Pendiente _pendiente;

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

static void iniciarScroll(const char* buf, textPosition_t align, textEffect_t efecto, uint16_t velocidad) {
    // Limpia antes de arrancar: si lo anterior en pantalla quedó quieto por
    // displayMarcadorConScroll()/displayTiempo() (que no vuelven solas al
    // marcador), displayScroll() no lo pisa del todo solo — queda un resto
    // pegado del texto viejo mezclado con el nuevo.
    disp.displayClear();
    disp.displayScroll(buf, align, efecto, velocidad);
    _enScroll = true;
    // Un scroll de paso (gol, texto, ganador, o lo que salga de la cola) siempre
    // vuelve al marcador al terminar. Sin esto, si este scroll arrancó encolado
    // detrás de un displayMarcadorConScroll()/displayTiempo() pendiente, heredaba
    // su "quedate quieto" — y al terminar el scroll (que naturalmente cruza toda
    // la pantalla y queda en blanco) la pantalla se quedaba en blanco en vez de
    // volver a mostrar el marcador.
    _mantenerTrasScroll = false;
}

// Arranca la entrada deslizante del marcador y la deja quieta al terminar (ver
// _mantenerTrasScroll) — usada directo la primera vez, y desde la cola cuando
// viene después de hacer salir lo que estaba antes (ver displayMarcadorConScroll).
static void iniciarEntradaMarcador(uint8_t local, uint8_t visitante) {
    snprintf(_marcador, sizeof(_marcador), "%d-%d", local, visitante);
    strlcpy(_actualMostrado, _marcador, sizeof(_actualMostrado));
    _mantenerTrasScroll = true;
    _enScroll = true;
    disp.displayClear();
    disp.displayText(_marcador, PA_CENTER, config.velocidadScroll, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
}

// Igual que arriba pero para el tiempo restante (ver displayTiempo).
static void iniciarEntradaTiempo(uint32_t ms) {
    static char buf[8];   // estático: MD_Parola guarda el puntero, no una copia
    uint32_t seg = ms / 1000;
    snprintf(buf, sizeof(buf), "%02lu:%02lu", seg / 60, seg % 60);
    strlcpy(_actualMostrado, buf, sizeof(_actualMostrado));
    _mantenerTrasScroll = true;
    _enScroll = true;
    disp.displayClear();
    disp.displayText(buf, PA_CENTER, config.velocidadScroll, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
}

// Hace SALIR lo que está quieto en pantalla ahora mismo, deslizándolo hacia la
// izquierda desde donde ya está — a diferencia de displayScroll()/iniciarScroll(),
// que siempre arranca desde afuera de la pantalla (si se usa para "sacar" algo que
// ya está centrado, se ve como si reapareciera de la nada para recién ahí salir).
// effectIn=PA_PRINT redibuja el texto donde ya está (sin animación, instantáneo) y
// con pause=0 pasa derecho al efectOut=PA_SCROLL_LEFT, que sí es el que se ve.
static void iniciarSalida(const char* textoActual) {
    _enScroll = true;
    _mantenerTrasScroll = false;
    disp.displayText(textoActual, PA_CENTER, config.velocidadScroll, 0, PA_PRINT, PA_SCROLL_LEFT);
}

// Todo texto scrolleado pasa por acá — reutiliza un único buffer estático porque
// MD_Parola guarda el puntero, no una copia, y cada llamada dispara un scroll nuevo.
// Si ya hay un scroll en curso, no lo interrumpe — lo encola (ver Pendiente arriba).
static void scrollSanitizado(const char* texto, textPosition_t align, textEffect_t efecto, uint16_t velocidad) {
    static char buf[64];
    asciiSanitize(texto, buf, sizeof(buf));
    // Espacio final para que el texto no quede pegado contra lo que venga después
    // (el marcador, el próximo scroll, etc.)
    size_t len = strlen(buf);
    if (len + 1 < sizeof(buf)) { buf[len] = ' '; buf[len + 1] = '\0'; }

    if (_enScroll) {
        _pendiente.tipo      = TipoPendiente::SCROLL;
        strlcpy(_pendiente.texto, buf, sizeof(_pendiente.texto));
        _pendiente.align     = align;
        _pendiente.efecto    = efecto;
        _pendiente.velocidad = velocidad;
        return;
    }
    iniciarScroll(buf, align, efecto, velocidad);
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
    Serial.printf("[DISP] scroll '%s' iniciado\n", config.textoBoot); Serial.flush();
}

void displayTick() {
    if (!disp.displayAnimate()) return;

    switch (_pendiente.tipo) {
        case TipoPendiente::SCROLL:
            _pendiente.tipo = TipoPendiente::NINGUNO;
            iniciarScroll(_pendiente.texto, _pendiente.align, _pendiente.efecto, _pendiente.velocidad);
            return;
        case TipoPendiente::ENTRADA_MARCADOR:
            _pendiente.tipo = TipoPendiente::NINGUNO;
            iniciarEntradaMarcador(_pendiente.golLocal, _pendiente.golVisitante);
            return;
        case TipoPendiente::ENTRADA_TIEMPO:
            _pendiente.tipo = TipoPendiente::NINGUNO;
            iniciarEntradaTiempo(_pendiente.tiempoMs);
            return;
        default: break;
    }

    if (_enScroll) {
        _enScroll = false;
        if (_mantenerTrasScroll) {
            _mantenerTrasScroll = false;   // se queda tal cual quedó — no lo pisa con el marcador
        } else {
            disp.displayText(_marcador, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
            strlcpy(_actualMostrado, _marcador, sizeof(_actualMostrado));
        }
    }
}

void displayTexto(const char* texto, uint16_t velocidad) {
    scrollSanitizado(texto, PA_LEFT, PA_SCROLL_LEFT, velocidad);
}

void displayMarcador(uint8_t local, uint8_t visitante) {
    snprintf(_marcador, sizeof(_marcador), "%d-%d", local, visitante);
    if (!_enScroll) {
        disp.displayText(_marcador, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
        strlcpy(_actualMostrado, _marcador, sizeof(_actualMostrado));
    }
}

// Igual que displayMarcador(), pero es una transición de verdad: lo que está
// quieto en pantalla (marcador o tiempo) sale scrolleando, y recién cuando eso
// termina entra el marcador deslizando — nada de cortar de golpe. La usa la
// alternancia marcador↔tiempo de modo tiempo (ver displayTiempo). A diferencia
// de displayTexto()/displayGol(), no vuelve sola al marcador al terminar: se
// queda quieta tal cual (ver _mantenerTrasScroll).
void displayMarcadorConScroll(uint8_t local, uint8_t visitante) {
    if (_enScroll) return;
    if (_actualMostrado[0] != '\0') {
        // Hay algo quieto en pantalla (el tiempo, normalmente) — primero lo hace
        // salir, y deja el marcador encolado para que entre justo después.
        _pendiente.tipo         = TipoPendiente::ENTRADA_MARCADOR;
        _pendiente.golLocal     = local;
        _pendiente.golVisitante = visitante;
        iniciarSalida(_actualMostrado);
    } else {
        iniciarEntradaMarcador(local, visitante);
    }
}

void displayGol() {
    scrollSanitizado(config.textoGol, PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
}

void displayGanador(int8_t w) {
    if (w == 0)      scrollSanitizado(config.textoGanadorCeleste, PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    else if (w == 1) scrollSanitizado(config.textoGanadorBlanco,  PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
    else             scrollSanitizado(config.textoEmpate,         PA_CENTER, PA_SCROLL_LEFT, config.velocidadScroll);
}

void displayModo(const char* texto) {
    scrollSanitizado(texto, PA_LEFT, PA_SCROLL_RIGHT, config.velocidadScroll);
}

// Transición de verdad, igual que displayMarcadorConScroll(): lo que está quieto
// sale scrolleando, y recién cuando termina entra el tiempo deslizando.
void displayTiempo(uint32_t ms) {
    if (_enScroll) return;
    if (_actualMostrado[0] != '\0') {
        _pendiente.tipo     = TipoPendiente::ENTRADA_TIEMPO;
        _pendiente.tiempoMs = ms;
        iniciarSalida(_actualMostrado);
    } else {
        iniciarEntradaTiempo(ms);
    }
}

bool displayEnScroll() {
    return _enScroll;
}

void displaySetBrillo(uint8_t brillo) {
    disp.setIntensity(brillo);
}

void displayApagar() {
    disp.displayShutdown(true);
}

void displayEncender(uint8_t brillo) {
    disp.displayShutdown(false);
    disp.setIntensity(brillo);
}
