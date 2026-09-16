#include "Reposo.h"
#include "WebConfig.h"
#include "Display.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include <Arduino.h>

static bool     _activo          = false;
static uint32_t _ultimaActividad = 0;
static uint32_t _ultimoScroll    = 0;
static bool     _jugandoPrev     = false;  // activo||pausado del tick anterior

static void entrar() {
    _activo = true;
    setCpuFrequencyMhz(80);   // 240→80: WiFi sigue andando, consumo baja bastante
    displayEntrarReposo(config.textoReposo, config.reposoBrillo);
    ambienteEntrarReposo();
    vozEntrarReposo();
    _ultimoScroll = millis();
    Serial.println("\n[REPOSO] Entrando en reposo");
}

static void salir() {
    _activo = false;
    setCpuFrequencyMhz(240);
    displaySalirReposo(config.brillo);
    ambienteSalirReposo();
    vozSalirReposo();
    Serial.println("\n[REPOSO] Saliendo de reposo");
}

void reposoInit() {
    _ultimaActividad = millis();
}

bool reposoActivo() {
    return _activo;
}

void reposoNotificarActividad() {
    _ultimaActividad = millis();
    if (_activo) salir();
}

void reposoTick(const Partido& partido) {
    // El partido acaba de dejar de estar en curso (terminó por gol/tiempo,
    // lo pararon o cancelaron por botón o por web) — el conteo de reposo
    // arranca de nuevo desde ahora, no desde que empezó el partido.
    bool jugando = partido.activo || partido.pausado;
    if (_jugandoPrev && !jugando) _ultimaActividad = millis();
    _jugandoPrev = jugando;

    if (_activo) {
        // El partido puede haber arrancado desde la web (/start), sin pasar
        // por reposoNotificarActividad() — sale sola igual.
        if (partido.activo || partido.pausado) {
            salir();
            return;
        }
        if (!displayEnScroll()
            && millis() - _ultimoScroll >= (uint32_t)config.reposoIntervaloSegs * 1000UL) {
            _ultimoScroll = millis();
            displayTexto(config.textoReposo, config.velocidadScroll);
        }
        return;
    }

    if (!partido.activo && !partido.pausado
        && millis() - _ultimaActividad >= (uint32_t)config.standbyTimeoutSegs * 1000UL) {
        entrar();
    }
}
