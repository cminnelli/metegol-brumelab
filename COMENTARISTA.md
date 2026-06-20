# Comentarista y Ambiente — Lógica de audio reactivo

El sistema tiene dos canales de audio independientes que trabajan en conjunto:

- **SP1** (voz): comentarista que habla según el estado del partido y reacciona a cada gol.
- **SP2** (ambiente): música de fondo que cambia de intensidad según el estado del partido y reacciona instantáneamente a cada gol.

---

## Flujo de audio durante un partido

```
Click inicio
  ↓
[SP1] Pitido de inicio
[SP2] Empieza música ambiente (genérico o hinchada, aleatoria)
  ↓ (~0.5s — espera que termine el pitido)
[SP1] Comentario de inicio (pistas 01–06)
  ↓ (partido en curso)
  ├─ Cada 12–35s → [SP1] Comentario según estado del partido
  ├─ Cada 2 min → [SP2] Cambia de pista aleatoria (misma categoría o alterna genérico/hinchada)
  ├─ Estado CALIENTE → [SP2] Cambia a música caliente (pistas 09–11)
  └─ Gol →
       [SP2] Reacción de hinchada one-shot (pistas 12–17)
       [SP1] Comentario de gol (pistas 55–76 según contexto)
       [SP2] Vuelve al modo anterior al terminar la reacción
       [SP1] Comentarios regulares se reanuden 2s después de que SP1 y SP2 terminen
  ↓
Click final / tiempo cumplido
  ↓
[SP1] Pitido de fin
[SP2] Se detiene
[SP1] Comentario final (4s después del pitido) → según resultado
```

---

## SP1 — Comentarista

### Timing del comentario de inicio

Cuando el partido arranca, el comentarista espera 500ms y luego verifica si SP1 está libre (pitido terminado). Mientras el pitido suena, reprograma cada 2 segundos. En cuanto termina el pitido, dispara el comentario de inicio. El efecto práctico es que el comentario arranca inmediatamente después del pitido, sin pausa audible.

### Intervalos entre comentarios

Entre comentarios regulares hay un intervalo aleatorio entre `intervaloComentariosMin` y `intervaloComentariosMax` (defaults: 12s–35s). El comentarista siempre espera al menos ese tiempo antes de volver a hablar.

### Guard de bloqueo

Antes de disparar un comentario regular, el comentarista verifica:
1. ¿SP1 está ocupado reproduciendo algo? (`vozIsBusy()`) → si sí, pospone 2s
2. ¿SP2 está en reacción de gol? (`ambienteGetEstado() == "gol_reaccion"`) → si sí, pospone 2s

Esto garantiza que nunca se pisa un comentario de gol con uno regular, y que siempre hay al menos 2s de margen después de la reacción de la hinchada.

### Pausa y reanudación

Al pausar el partido, el comentarista congela su estado. Al reanudar, continúa desde donde estaba — no reproduce el comentario de inicio de vuelta.

---

## Estados del partido (SP1)

La selección del comentario de cada intervalo sigue este orden de prioridad. Se usa el primer estado que aplica:

### 1. PRIMEROS_MINUTOS
**Cuándo:** desde que arranca el partido hasta `primerosMinsSegs` segundos después del comentario de inicio.

Fase de apertura del partido. Comentarios introductorios.
Default: 20 segundos.

---

### 2. ULTIMO_TRAMO
**Cuándo (modo tiempo):** quedan menos de `ultimoTramoSegs` segundos.  
**Cuándo (modo goles):** algún equipo está a exactamente 1 gol de ganar.

Tensión máxima. Tiene prioridad sobre el marcador: aunque haya goleada o partido caliente, si estamos en el último tramo ese estado rige.

Default: últimos 60s (tiempo) / match point (goles).

---

### 3. GOLEADA
**Cuándo:** `diff >= goleadaDiff` (uno aplasta al otro).

Un equipo domina de forma clara. Tiene prioridad sobre ABURRIDO — aunque no haya goles recientes, una goleada sigue siendo noticiable.

Default: diferencia de 3 o más goles.

---

### 4. CALIENTE
**Cuándo:** las tres condiciones simultáneamente:
1. `goles_totales >= calienteGoles` — suficiente actividad acumulada
2. `diff < goleadaDiff` — partido apretado (empate, 1 o 2 goles de diferencia)
3. `time_desde_ultimo_gol < 45s` — hubo acción reciente

Partido vivo, parejo, con goles y acción reciente. Se desactiva si el partido se enfría (sin goles en 45s).

Default: 4+ goles totales.

---

### 5. ABURRIDO
**Cuándo:** tiempo sin goles supera `umbralAburridoSegs`.  
Medido desde: si no hay goles, desde que terminaron los primeros minutos; si hay goles, desde el último gol.

Default: 50 segundos sin actividad.

---

### 6. PAREJO
Empate (diff == 0).

### 7. TRANQUILO
Ventaja de 1 gol (diff == 1).

### 8. DEFINIDO
Ventaja de 2 goles o más, sin llegar a goleada.

---

## Selección de comentario de gol (SP1)

Independiente del estado del comentarista. Cuando cae un gol, el tipo de audio se selecciona así:

| Tipo | Condición | Pistas |
|------|-----------|--------|
| `agonico_empate` | Último tramo + el gol empata | 74–76 |
| `agonico` | Último tramo + no empata | 71–73 |
| `empate` | El gol deja el partido empatado | 63–66 |
| `caliente` | 4+ goles totales y partido apretado | 67–70 |
| `efusivo` | El gol amplía la diferencia a goleada | 59–62 |
| `normal` | El resto de los casos | 55–58 |

Todos los comentarios de gol usan shuffle sin repetición — no se repite el mismo audio hasta haber reproducido todos los de su rango.

---

## SP2 — Música ambiente reactiva

SP2 tiene su propia SD con pistas 1–17 y opera en una máquina de estados independiente:

### Estados SP2

| Estado | Cuándo | Pistas | Frecuencia |
|--------|--------|--------|------------|
| `PARADO` | Partido no activo | Silencio | — |
| `NORMAL` | Partido activo, no caliente | Ambiente genérico (1–4), seamless loop | La mayor parte del partido |
| `HINCHADA` | Después del 1er gol_reaccion | Hinchada (5–8), one-shot | **1 sola vez por partido** |
| `CALIENTE` | Partido activo + estado caliente | Momento caliente (9–11), seamless loop | **Máx. 2 veces por partido** |
| `GOL_REACCION` | Al momento de cada gol | Reacción de gol (12–17), one-shot | Cada gol |

### Comportamiento por estado

**NORMAL:** arranca al iniciar el partido. Elige una pista aleatoria de `ambienteGenérico` (1–4) que loopea sin corte (`0x19` DFPlayer) de forma indefinida. No cambia automáticamente de pista — la misma pista suena mientras el partido esté normal.

**HINCHADA:** se dispara una sola vez por partido, al finalizar la primera reacción de gol. Reproduce una pista aleatoria de `hinchadaMusica` (5–8) sin loop. Al terminar, vuelve al modo anterior (NORMAL o CALIENTE).

**CALIENTE:** entra cuando el comentarista detecta estado CALIENTE. Cambia a `momentoCaliente` (9–11) con fade-out de la pista anterior. Loopea seamless. Al enfriarse el partido, vuelve a NORMAL con fade-out. Máximo 2 sesiones caliente por partido.

**GOL_REACCION:** al detectar un gol, interrumpe instantáneamente y reproduce una pista de `ambienteGol` (12–17) sin loop, con volumen asegurado. Al terminar: si es la primera vez → entra en HINCHADA; si ya sonó → restaura el modo anterior. El comentarista espera hasta que termine la reacción + 2s de margen.

### Transiciones suaves

Al cambiar de modo (NORMAL↔CALIENTE), SP2 primero baja el volumen a 0 (`150ms`), inicia la nueva pista, y restaura el volumen `~250ms` después. El efecto es una breve pausa limpia entre tracks, mucho menos abrupta que un corte directo.

Las reacciones de gol y la hinchada son inmediatas (sin fade) porque reemplazan intencionalmente el sonido en curso.

### Watchdog

Si SP2 queda en silencio por error (`0x40` DFPlayer), en el próximo ciclo de `ambienteActualizar` detecta `_pistaActual == 0` y reinicia la reproducción automáticamente.

---

## Parámetros configurables

| Parámetro | Default | Descripción |
|-----------|---------|-------------|
| `intervaloComentariosMin` | 12s | Mínimo entre comentarios regulares |
| `intervaloComentariosMax` | 35s | Máximo entre comentarios regulares |
| `primerosMinsSegs` | 20s | Duración de la fase de apertura |
| `ultimoTramoSegs` | 60s | Umbral de último tramo (modo tiempo) |
| `goleadaDiff` | 3 | Diferencia de goles para declarar goleada |
| `calienteGoles` | 4 | Goles totales mínimos para estado CALIENTE |
| `umbralAburridoSegs` | 50s | Segundos sin goles para estado ABURRIDO |

**Hardcodeados (no configurables):**
- Ventana de "acción reciente" para CALIENTE: **45 segundos**
- Intervalo de cambio de fase en SP2 NORMAL: **120 segundos**
- Margen post-gol antes de comentario regular: **2 segundos**

---

## Rangos de pistas configurables (web)

Todos los rangos `desde/hasta` son configurables desde el panel web. Los defaults corresponden a la distribución de tracks documentada en README.md. Si se reorganizan las SDs, actualizar los rangos desde la web y guardar.
