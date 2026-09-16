#include "Reposo.h"
#include "WebConfig.h"
#include "Display.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include <Arduino.h>

static bool     _activo          = false;
static uint32_t _ultimaActividad = 0;
static bool     _jugandoPrev     = false;  // activo||pausado del tick anterior

static void entrar() {
    _activo = true;
    // Reposo real: farola y WiFi apagados de hardware, no solo atenuados — el
    // objetivo es que consuma lo mínimo posible si se queda así toda la noche.
    setCpuFrequencyMhz(80);
    displayApagar();
    ambienteEntrarReposo();
    vozEntrarReposo();
    webConfigApagarWifi();
    Serial.println("\n[REPOSO] Entrando en reposo");
}

static void salir() {
    _activo = false;
    setCpuFrequencyMhz(240);
    webConfigReactivarWifi();
    displayEncender(config.brillo);
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
        // Red de seguridad: si por algún camino el partido quedó activo estando
        // en reposo, sale sola (normalmente ya salió por reposoNotificarActividad()
        // al detectar el click del encoder que lo arrancó).
        if (partido.activo || partido.pausado) salir();
        return;
    }

    if (!partido.activo && !partido.pausado
        && millis() - _ultimaActividad >= (uint32_t)config.standbyTimeoutSegs * 1000UL) {
        entrar();
    }
}
