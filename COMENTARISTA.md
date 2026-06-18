# Comentarista — Máquina de estados

## Flujo del partido

```
t=0     Partido inicia (activo = true)
t=4s    Dispara comentario de INICIO  ← hardcodeado 4s
t=4s    _inicioFiredAt = millis()     ← referencia para todo lo que sigue

t=4s … t=24s   Estado: PRIMEROS_MINUTOS  (primerosMinsSegs = 20s, config 10–30s)

t=24s en adelante   → evaluación de estado por prioridad (ver abajo)
```

---

## Prioridad de estados (de mayor a menor)

Cada vez que el comentarista va a hablar, evalúa el estado en este orden y usa el primero que aplica.

### 1. PRIMEROS_MINUTOS
**Condición:** `(now - _inicioFiredAt) < primerosMinsSegs * 1000`

Fase de apertura. Dura `primerosMinsSegs` segundos contados desde que se disparó el comentario de inicio (no desde que arrancó el partido). Default: 20s. Configurable: 10–30s.

---

### 2. ULTIMO_TRAMO
**Modo tiempo:** `tiempo_restante < ultimoTramoSegs`  
**Modo goles:** `max(goles) == golesMax - 1` (falta exactamente 1 gol para ganar)

Tensión máxima. Tiene prioridad sobre el marcador: aunque haya goleada o partido caliente, si estamos en el último tramo ese es el estado que rige. Default: últimos 60s (tiempo) o "match point" (goles).

---

### 3. GOLEADA
**Condición:** `diff >= goleadaDiff`  
**Parámetro:** `goleadaDiff = 3` (default) — diferencia de 3 o más goles

Un equipo está aplastando. No importa si hace mucho que no hay goles, una goleada siempre es digna de comentario. Tiene prioridad sobre ABURRIDO.

---

### 4. CALIENTE 🔥
**Condición (las 3 deben cumplirse simultáneamente):**
1. `total_goles >= calienteGoles` — hay suficiente actividad acumulada (default: 4 goles)
2. `diff < goleadaDiff` — el partido está **apretado**: la diferencia es menor que el umbral de goleada (menos de 3 goles). Esto incluye empate (0), diferencia de 1 o de 2.
3. `(now - ultimoGol) < 45s` — hubo un gol **recientemente** (últimos 45s). Si el partido tuvo muchos goles pero nadie anotó en el último minuto, ya no es "caliente".

**Por qué las 3 condiciones:**  
Un partido se "calienta" cuando hay mucha acción reciente Y el resultado sigue siendo incierto. Si hay goles pero uno ya ganó por mucho → GOLEADA. Si hubo goles pero el partido se enfrió → ABURRIDO o PAREJO. Solo cuando los tres factores coinciden el estado es verdaderamente "caliente".

**Estado dinámico:** se activa en rachas de goles, se desactiva si el partido se enfría. Al apagarse cae a ABURRIDO, PAREJO, TRANQUILO o DEFINIDO según el marcador.

---

### 5. ABURRIDO 😴
**Condición:** `tiempo_sin_gol > umbralAburridoSegs`  
**Parámetro:** `umbralAburridoSegs = 50s` (default)

Referencia de tiempo: si no hay goles todavía, se mide desde que terminaron los primeros minutos (no desde el inicio del partido, para no solaparse con PRIMEROS_MINUTOS). Si ya hubo goles, se mide desde el último gol.

Tiene prioridad sobre los estados de marcador (PAREJO, TRANQUILO, DEFINIDO) — si el partido está quieto, la situación del marcador es irrelevante para el comentarista.

---

### 6. PAREJO
**Condición:** `diff == 0` (empate)

---

### 7. TRANQUILO
**Condición:** `diff == 1` (ventaja mínima de 1 gol)

---

### 8. DEFINIDO
**Condición:** `diff >= 2` y `diff < goleadaDiff`

Ventaja clara pero sin llegar a goleada (diff entre 2 y goleadaDiff-1).

---

## Selección de audio para goles

Independiente del estado del comentarista. Cuando cae un gol, se elige el tipo de audio contextual:

| Tipo | Condición |
|------|-----------|
| `agonico_empate` | último tramo + empate antes del gol |
| `agonico` | último tramo + no empate |
| `empate` | el gol empata el partido |
| `caliente` | ≥ calienteGoles totales + partido apretado |
| `efusivo` | diff ≥ goleadaDiff (gol de goleada) |
| `normal` | el resto |

---

## Parámetros configurables (con defaults)

| Parámetro | Default | Rango | Descripción |
|-----------|---------|-------|-------------|
| `primerosMinsSegs` | 20s | 10–30s | Duración de la fase de apertura |
| `ultimoTramoSegs` | 60s | 10–120s | Umbral para último tramo (modo tiempo) |
| `goleadaDiff` | 3 | 2–6 | Diferencia de goles para declarar goleada |
| `calienteGoles` | 4 | 2–10 | Mínimo de goles totales para activar caliente |
| `umbralAburridoSegs` | 50s | 30–600s | Segundos sin goles para entrar en aburrido |
| `intervaloComentariosMin` | 12s | 5–120s | Mínimo entre comentarios |
| `intervaloComentariosMax` | 35s | 5–120s | Máximo entre comentarios |

**Hardcodeado (no configurable):**
- Delay antes del comentario de inicio: **4 segundos**
- Ventana de "acción reciente" para CALIENTE: **45 segundos**
