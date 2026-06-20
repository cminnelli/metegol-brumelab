# Metegol Electrónico

Sistema de audio y marcador electrónico para metegol, basado en ESP32. Detecta goles, reproduce comentarios de voz y música ambiente reactiva, muestra el marcador en un display LED, y permite configuración vía WiFi.

---

## Flujo de uso

```
Encender
  ↓
Display: "1-GOLES" (modo default)
  ↓
Girar encoder → alterna entre "1-GOLES" y "2-TIEMPO"
  ↓
Click → Pitido → "ARRANCAAA!" → Partido en curso
  ↓
  ├─ Gol → SP2 reacción + comentario SP1 → continúa
  ├─ Click → "PAUSA!" → Partido pausado
  │    └─ Click → "VAMOS!" → Reanuda
  ├─ Doble click → "CANCELADO!" → Vuelve a espera (encoder disponible para cambiar modo)
  └─ Hold 2s → Reinicia ESP32
  ↓
Fin de partido → Pitido final → Comentario final → Display ganador
  ↓
Click → Nuevo partido
```

---

## Hardware

| Componente | Cantidad | Notas |
|-----------|----------|-------|
| ESP32 Dev Module (30 pines) | 1 | Microcontrolador principal |
| DFPlayer Mini | 2 | SP1 = voz, SP2 = ambiente |
| microSD | 2 | Una por DFPlayer |
| Módulos MAX7219 (8×8 LED, cadena de 4) | 1 | Display del marcador |
| Sensor óptico de gol | 2 | Uno por arco |
| Encoder KY-040 (con pulsador) | 1 | Control principal |
| Fuente 5V / 2A mínimo | 1 | El sistema consume bastante |

---

## Mapa de pines

### Display MAX7219

| Señal | GPIO ESP32 |
|-------|-----------|
| DIN (MOSI) | 23 |
| CLK (SCK) | 18 |
| CS | 5 |
| VCC | **5V** (obligatorio — no usar 3.3V) |
| GND | GND común |

### SP1 — DFPlayer voz/comentarista

| Señal | GPIO ESP32 |
|-------|-----------|
| RX del DFPlayer | GPIO 26 (TX del ESP32) |
| TX del DFPlayer | GPIO 27 (RX del ESP32) |
| VCC | 5V |
| GND | GND común |

### SP2 — DFPlayer música ambiente

| Señal | GPIO ESP32 |
|-------|-----------|
| RX del DFPlayer | GPIO 16 (TX del ESP32) |
| TX del DFPlayer | GPIO 17 (RX del ESP32) |
| VCC | 5V |
| GND | GND común |

### Sensores de gol

| Equipo | GPIO |
|--------|------|
| Celeste | 34 (input-only, sin pull-up interno — requiere pull-up externo 10kΩ a 3.3V) |
| Blanco | 35 (idem) |

> **Importante:** GPIO 34 y 35 son input-only en el ESP32 y no tienen pull-up interno. Sin resistencia de pull-up externa el pin flota y puede generar falsos goles. Conectar 10kΩ entre cada pin y 3.3V.

### Encoder KY-040

| Señal | GPIO |
|-------|------|
| CLK | 32 |
| DT | 33 |
| SW | 25 |
| VCC | 3.3V |

---

## Advertencias de cableado

- **GND común obligatorio.** Todos los componentes deben compartir el mismo GND. Sin GND común el audio tiene ruido y el display puede comportarse de forma errática.
- **MAX7219 → 5V.** No funciona bien a 3.3V.
- **DFPlayer → 5V.** Puede funcionar a 3.3V pero con ruido de audio. Agregar capacitor 100µF entre VCC y GND de cada DFPlayer.
- **Encoder → 3.3V.** No conectar a 5V.
- **GPIO 5 está ocupado** (CS del display). Los pines libres más seguros para expansión: 4, 13, 19, 21, 22.

---

## Tarjetas SD

### SP1 — Voz y comentarista (pistas 0001–0088)

Los archivos deben estar en la raíz de la SD numerados con 4 dígitos.

| Rango | Contenido | Offset HW |
|-------|-----------|-----------|
| 0001–0006 | Comentarios de inicio | — |
| 0007–0012 | Primeros minutos | — |
| 0013–0018 | Partido parejo | — |
| 0019–0024 | Partido caliente | — |
| 0025–0030 | Goleada | — |
| 0031–0036 | Partido definido | — |
| 0037–0042 | Último tramo | — |
| 0043–0048 | Partido aburrido | — |
| 0049–0054 | Partido tranquilo | — |
| 0055–0058 | Gol normal | **-1** |
| 0059–0062 | Gol efusivo | **-1** |
| 0063–0066 | Gol empate | **-1** |
| 0067–0070 | Gol caliente | **-1** |
| 0071–0073 | Gol agónico | **-1** |
| 0074–0076 | Gol agónico empate | **-1** |
| 0077–0078 | Pitido de inicio | **-1** |
| 0079–0080 | Pitido de fin | **-1** |
| 0081–0082 | Comentario final empate | **-1** |
| 0083–0084 | Comentario final aplastante | **-1** |
| 0085–0086 | Comentario final ajustado | **-1** |
| 0087–0088 | Comentario final normal | **-1** |

> El offset **-1** existe porque el DFPlayer de clon numera desde 0 internamente. Las pistas del rango GOL/PITIDO/FIN se envían como `número - 1` al hardware.

### SP2 — Música ambiente (pistas 0001–0017, SD propia)

SD independiente del SP1. Sin offset de hardware.

| Rango | Categoría | Cuándo suena |
|-------|-----------|-------------|
| 0001–0004 | Ambiente genérico | Durante el partido, fase normal |
| 0005–0008 | Hinchada / música | Alterna con genérico cada 2 minutos |
| 0009–0011 | Momento caliente | Cuando el partido está "caliente" (SP2 modo caliente) |
| 0012–0017 | Reacción de gol | One-shot al momento del gol, luego vuelve al modo anterior |

---

## Display — comportamiento

| Momento | Mensaje |
|---------|---------|
| Boot | Pulso de brillo → scroll "Hola Brumelab!" |
| Pre-game | Scroll "1-GOLES" o "2-TIEMPO" (encoder) |
| Inicio de partido | Scroll "ARRANCAAA!" → marcador "0-0" |
| Gol | Scroll "Gollll!!!" → marcador actualizado |
| Pausa | Scroll "PAUSA!" |
| Reanuda | Scroll "VAMOS!" → marcador |
| Cancelado | Scroll "CANCELADO!" → marcador "0-0" |
| Modo tiempo (en juego) | Marcador estático; scrollea tiempo restante MM:SS cada N segundos |
| Fin de partido | Scroll "Ganador Celeste!" / "Ganador Blanco!" / "Empate!" |

---

## Control — encoder KY-040

### Rotación (pre-game o partido terminado)

Alterna el modo de juego entre **1-GOLES** y **2-TIEMPO**.

### Pulsador

| Estado | Click simple | Doble click | Hold 2 segundos |
|--------|-------------|-------------|-----------------|
| En espera | Inicia partido | Cancela (vuelve a espera) | Reinicia ESP32 |
| Jugando | Pausa → display "PAUSA!" | Cancela → display "CANCELADO!" | Reinicia ESP32 |
| Pausado | Reanuda → display "VAMOS!" | Cancela → display "CANCELADO!" | Reinicia ESP32 |
| Terminado | Inicia nuevo partido | Cancela → display "CANCELADO!" | Reinicia ESP32 |

> Para reiniciar un partido en curso: doble click (cancela) + click simple (inicia nuevo).  
> El hold de 2 segundos hace `ESP.restart()` — útil si algo se cuelga.

---

## Modos de juego

### Modo Goles (default)
El partido termina cuando un equipo llega al número de goles configurado (`golesMax`, default 4). El display muestra solo el marcador `X-Y`.

### Modo Tiempo
El partido dura un tiempo fijo (`duracionMin`, default 5 minutos). El display alterna entre el marcador y el tiempo restante `MM:SS` cada `intervaloDisplay` segundos (default 5s).

---

## Configuración WiFi

Al encender, el ESP32 levanta un access point propio llamado **Metegol** con IP `192.168.4.1`.

Panel web: `http://192.168.4.1/configBrume` (o `http://metegol.local/configBrume` si hay mDNS)

Parámetros configurables:

| Parámetro | Default | Descripción |
|-----------|---------|-------------|
| Volumen voz (SP1) | 30 | 0–30 |
| Volumen ambiente (SP2) | 20 | 0–30 |
| Goles para ganar | 4 | Modo goles |
| Duración | 5 min | Modo tiempo |
| Brillo display | 5 | 0–15 |
| Velocidad scroll | 40 ms | ms por frame |
| Intervalo display | 5 s | Cada cuánto scrollea el tiempo (modo tiempo) |
| Rangos de pistas SP1 | — | Configurable por estado del comentarista |
| Rangos de pistas SP2 | — | Configurable por categoría de ambiente |
| Intervalos del comentarista | — | Min/max entre comentarios, thresholds de estado |

La configuración se guarda en NVS (memoria no volátil) y persiste entre reinicios, excepto el modo de juego que siempre arranca en **Goles**.

---

## Compilar y flashear

```bash
# Compilar y flashear al ESP32 físico
pio run -e test_audio --target upload

# Solo compilar
pio run -e test_audio

# Monitor serial (115200 baud)
pio device monitor -e test_audio
```

Entornos disponibles:

| Entorno | Uso |
|---------|-----|
| `test_audio` | ESP32 físico — entorno principal |
| `wokwi` | Simulación (audio deshabilitado, log por Serial) |
| `test_encoder` | Prueba aislada del encoder |

---

## Estructura de archivos

```
src_audio/           ← código activo
  main.cpp           ← loop principal, sensores, encoder, lógica de partido
  AudioVoz.cpp       ← SP1: DFPlayer voz/comentarista (Serial2, GPIO 26/27)
  AudioAmbiente.cpp  ← SP2: DFPlayer ambiente reactivo (Serial1, GPIO 16/17)
  Comentarista.cpp   ← máquina de estados del comentarista
  Display.cpp        ← display MAX7219 via MD_Parola (SPI software GPIO 23/18/5)
  Partido.cpp        ← modelo del partido (goles, tiempo, ganador)
  WebConfig.cpp      ← servidor web + NVS

include/
  AudioVoz.h
  AudioAmbiente.h
  Comentarista.h
  Display.h
  Partido.h
  WebConfig.h
  config.h           ← constantes de build

COMENTARISTA.md      ← lógica detallada del comentarista y SP2
README.md            ← este archivo
```

---

## Librerías

```ini
lib_deps =
    majicDesigns/MD_Parola @ ^3.7.3
    majicDesigns/MD_MAX72XX @ ^3.5.1
```

WiFi, WebServer, Preferences, ESPmDNS son parte del framework Arduino para ESP32.

> **Nota DFPlayer clones:** Los módulos DFPlayer de clon no aceptan el checksum estándar de la librería oficial. El audio se maneja con comandos raw Serial (protocolo 0x7E directo), sin librería externa.
