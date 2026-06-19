# Metegol Electrónico

Sistema electrónico para metegol con ESP32, dos DFPlayer Mini y display LED MAX7219. Detecta goles, reproduce comentarios de audio, mantiene el marcador en pantalla y permite configuración via WiFi.

---

## Estado actual — Checkpoint

| Módulo | Estado |
|--------|--------|
| Sensores de gol | Funcionando |
| Audio — voz/comentarista (SP1) | Funcionando |
| Audio — música ambiente (SP2) | Funcionando |
| Display LED MAX7219 | Funcionando |
| Comentarista con estados | Funcionando |
| Configuración web (WiFi) | Funcionando |
| Encoder KY-040 (inicio/pausa/reset) | Funcionando |

---

## Hardware necesario

- ESP32 Dev Module (30 pines)
- 2× DFPlayer Mini con tarjeta microSD
- 1× Cadena de 4 módulos MAX7219 (8×8 LED)
- 2× Sensor óptico o magnético de gol (uno por equipo)
- 1× Encoder rotativo KY-040 (con pulsador)
- Fuente 5V / 2A mínimo (el sistema consume bastante)
- Cables, resistencias 1kΩ para señal DFPlayer si hay ruido

---

## Mapa de pines — ESP32

### Display MAX7219

| Pin MAX7219 | GPIO ESP32 | Nota |
|-------------|-----------|------|
| VCC         | 5V        | **NO usar 3.3V** — el MAX7219 necesita entre 4V y 5.5V |
| GND         | GND       | GND común con ESP32 (ver advertencia abajo) |
| DIN         | GPIO 23   | SPI MOSI |
| CLK         | GPIO 18   | SPI SCK |
| CS          | GPIO 5    | SPI CS |

### DFPlayer Mini — SP1 (voz / comentarista)

| Pin DFPlayer | GPIO ESP32 | Nota |
|--------------|-----------|------|
| VCC          | 5V        | |
| GND          | GND       | GND común |
| RX           | GPIO 26   | TX del ESP32 → RX del DFPlayer |
| TX           | GPIO 27   | RX del ESP32 ← TX del DFPlayer |

### DFPlayer Mini — SP2 (música ambiente)

| Pin DFPlayer | GPIO ESP32 | Nota |
|--------------|-----------|------|
| VCC          | 5V        | |
| GND          | GND       | GND común |
| RX           | GPIO 16   | TX del ESP32 → RX del DFPlayer |
| TX           | GPIO 17   | RX del ESP32 ← TX del DFPlayer |

### Sensores de gol

| Sensor      | GPIO ESP32 | Nota |
|-------------|-----------|------|
| Equipo celeste | GPIO 34 | Input-only — no usar como output |
| Equipo blanco  | GPIO 35 | Input-only — no usar como output |

### Encoder KY-040

| Pin Encoder | GPIO ESP32 | Nota |
|-------------|-----------|------|
| CLK         | GPIO 32   | Interrupt FALLING |
| DT          | GPIO 33   | |
| SW          | GPIO 25   | Pull-up interno |
| VCC         | 3.3V      | |
| GND         | GND       | GND común |

---

## Advertencias importantes

### GND común — CRÍTICO
**Todos los GND deben estar conectados entre sí.** El GND del ESP32, del display MAX7219, de ambos DFPlayer y de los sensores deben ir al mismo punto de tierra. Sin GND común el circuito no cierra y nada funciona, aunque el voltaje esté correcto.

### Voltajes
- **MAX7219 VCC → 5V obligatorio.** Con 3.3V el display no enciende. Los pines de señal (DIN, CLK, CS) salen a 3.3V del ESP32 y el MAX7219 los acepta sin problema aunque su VCC sea 5V.
- **DFPlayer VCC → 5V.** Puede funcionar a 3.3V con ruido de audio. Recomendado 5V con capacitor de desacople 100µF entre VCC y GND.
- **Encoder → 3.3V.** No conectar a 5V, los pines del ESP32 no toleran 5V en entrada.

### Pines ocupados — no reasignar
GPIO 16, 17, 18, 23, 25, 26, 27, 32, 33, 34, 35, 5 están todos en uso. Si necesitás agregar periféricos, los pines disponibles libres más seguros son: **4, 13, 19, 21, 22**.

---

## Comportamiento del display

| Momento | Display |
|---------|---------|
| Boot | Pulso de brillo (animación) → scroll "Hola Brumelab!" → "0-0" |
| Click (inicio de partido) | Scroll "ARRANCAAA!" → marcador actual |
| Gol | Scroll "Gollll!!!" → marcador actualizado |
| Fin de partido (goles o tiempo) | Scroll "Ganador Celeste!" / "Ganador Blanco!" / "Empate!" → score final |
| Doble click (reset) | Muestra "0-0" |
| Pausa → reanuda | Muestra marcador actual |

---

## Control — Encoder KY-040

| Acción | Estado del partido | Resultado |
|--------|-------------------|-----------|
| Click simple | En espera | Inicia partido |
| Click simple | En juego | Pausa |
| Click simple | Pausado | Reanuda |
| Click simple | Terminado | Nuevo partido |
| Doble click | Cualquiera | Reset → En espera |

---

## Configuración WiFi

Al encender el ESP32 se conecta a la red WiFi guardada (o levanta un AP propio llamado **Metegol** en 192.168.4.1).

Panel de configuración: `http://metegol.local/configBrume`

Parámetros configurables:
- Volumen voz (SP1) y ambiente (SP2)
- Modo de juego: por goles o por tiempo
- Goles para ganar / duración en minutos
- Brillo del display (0–15)
- Velocidad de scroll (ms/frame)
- Pista de ambiente (1–3)
- Intervalos y thresholds del comentarista

La configuración se guarda en NVS (memoria no volátil del ESP32) y persiste entre reinicios.

---

## Estructura de archivos

```
src_audio/           ← código activo del proyecto
  main.cpp           ← setup/loop, sensores, encoder, lógica de partido
  Display.cpp        ← display MAX7219 via MD_Parola
  AudioVoz.cpp       ← SP1 — DFPlayer voz/comentarista
  AudioAmbiente.cpp  ← SP2 — DFPlayer música ambiente
  Comentarista.cpp   ← máquina de estados del comentarista
  Partido.cpp        ← modelo del partido (goles, tiempo, ganador)
  WebConfig.cpp      ← servidor web + configuración NVS

include/             ← headers compartidos
  Display.h
  AudioVoz.h
  AudioAmbiente.h
  Comentarista.h
  Partido.h
  WebConfig.h
  config.h           ← constantes de build (GOLES_MAX, DURACION_MS, etc.)
```

---

## Compilar y flashear

```bash
# Compilar
pio run -e test_audio

# Compilar y flashear
pio run -e test_audio --target upload

# Monitor serial
pio device monitor -e test_audio
```

Entornos disponibles en `platformio.ini`:
- `test_audio` — entorno principal (ESP32 físico)
- `wokwi` — simulación Wokwi (audio deshabilitado, log por Serial)
- `test_encoder` — prueba aislada del encoder

---

## Tarjetas microSD — DFPlayer

Los DFPlayer Mini de clones **no aceptan el comando checksum** estándar de la librería DFRobotDFPlayerMini. Por eso el audio se maneja con comandos raw Serial directamente (sin librería externa).

Estructura de carpetas en la SD:
- Los archivos deben estar en la raíz o en carpetas numeradas según el protocolo DFPlayer.
- Pistas 01–54: comentarios del comentarista por estado de juego.
- Pistas 55–76: reacciones de gol (normal, efusivo, empate, caliente, agónico, etc.).

---

## Librerías usadas

```ini
lib_deps =
    majicDesigns/MD_Parola @ ^3.7.3
    majicDesigns/MD_MAX72XX @ ^3.5.1
```

El resto (WiFi, WebServer, Preferences, ESPmDNS) son parte del framework Arduino para ESP32.
