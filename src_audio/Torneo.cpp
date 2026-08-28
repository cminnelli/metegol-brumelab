#include "Torneo.h"
#include <Preferences.h>
#include <string.h>

Torneo torneo;
static Preferences prefsT;

// ── Persistencia ─────────────────────────────────────────────────────────────

static void guardarTorneo() {
    prefsT.begin("metegol-torneo", false);
    prefsT.putBytes("data", &torneo, sizeof(Torneo));
    prefsT.end();
}

// Borra el torneo de NVS sin tocar el struct en RAM — se usa al coronar campeón
// para que un torneo ya jugado no quede guardado más allá de este encendido.
static void borrarTorneoNVS() {
    prefsT.begin("metegol-torneo", false);
    prefsT.remove("data");
    prefsT.end();
}

void torneoInit() {
    memset(&torneo, 0, sizeof(Torneo));
    torneo.partidoEnJuego = -1;
    torneo.campeon        = -1;
    prefsT.begin("metegol-torneo", true);
    // Si el tamaño guardado no coincide con el struct actual (ej: firmware actualizado),
    // se descarta y arranca vacío en vez de leer memoria con el layout viejo.
    if (prefsT.getBytesLength("data") == sizeof(Torneo)) {
        prefsT.getBytes("data", &torneo, sizeof(Torneo));
    }
    prefsT.end();
}

void torneoCancelar() {
    memset(&torneo, 0, sizeof(Torneo));
    torneo.partidoEnJuego = -1;
    torneo.campeon        = -1;
    guardarTorneo();
}

// ── Tabla de posiciones ──────────────────────────────────────────────────────

struct FilaPos {
    uint8_t idx;
    uint8_t pj, pg, pe, pp;
    int16_t gf, gc, dif;
    uint8_t pts;
};

static void calcularStandings(uint8_t grupo, FilaPos* filas, uint8_t& n) {
    n = 0;
    for (uint8_t i = 0; i < torneo.nParticipantes; i++) {
        if (torneo.participantes[i].grupo != grupo) continue;
        filas[n] = {i, 0, 0, 0, 0, 0, 0, 0, 0};
        n++;
    }
    for (uint8_t m = 0; m < torneo.nPartidosGrupo; m++) {
        PartidoGrupo& pg = torneo.partidosGrupo[m];
        if (!pg.jugado || torneo.participantes[pg.pA].grupo != grupo) continue;
        FilaPos *fa = nullptr, *fb = nullptr;
        for (uint8_t k = 0; k < n; k++) {
            if (filas[k].idx == pg.pA) fa = &filas[k];
            if (filas[k].idx == pg.pB) fb = &filas[k];
        }
        fa->pj++; fb->pj++;
        fa->gf += pg.golA; fa->gc += pg.golB;
        fb->gf += pg.golB; fb->gc += pg.golA;
        if (pg.golA > pg.golB)      { fa->pg++; fa->pts += 3; fb->pp++; }
        else if (pg.golA < pg.golB) { fb->pg++; fb->pts += 3; fa->pp++; }
        else                        { fa->pe++; fb->pe++; fa->pts++; fb->pts++; }
    }
    for (uint8_t k = 0; k < n; k++) filas[k].dif = filas[k].gf - filas[k].gc;
    // orden por pts, dif, goles a favor — insertion sort (n <= 4)
    for (uint8_t i = 1; i < n; i++) {
        FilaPos key = filas[i];
        int8_t j = i - 1;
        while (j >= 0 && (filas[j].pts < key.pts ||
               (filas[j].pts == key.pts && filas[j].dif < key.dif) ||
               (filas[j].pts == key.pts && filas[j].dif == key.dif && filas[j].gf < key.gf))) {
            filas[j + 1] = filas[j];
            j--;
        }
        filas[j + 1] = key;
    }
}

// ── Knockout ─────────────────────────────────────────────────────────────────

static int8_t ganadorKO(const PartidoKO& m) {
    if (m.pB < 0) return m.pA;               // bye
    return (m.golA >= m.golB) ? m.pA : m.pB;
}

static void checkAvanzarRonda(uint8_t ronda) {
    int8_t ganadores[TORNEO_MAX_PART_GRUPO * TORNEO_MAX_GRUPOS];
    uint8_t nGan = 0;
    for (uint8_t i = 0; i < torneo.nPartidosKO; i++) {
        if (torneo.partidosKO[i].ronda != ronda) continue;
        if (!torneo.partidosKO[i].jugado) return;   // ronda incompleta, todavía no se avanza
        ganadores[nGan++] = ganadorKO(torneo.partidosKO[i]);
    }
    if (nGan <= 1) {
        if (nGan == 1) { torneo.campeon = ganadores[0]; torneo.fase = 2; }
        return;
    }
    for (uint8_t k = 0; k + 1 < nGan; k += 2) {
        PartidoKO& m = torneo.partidosKO[torneo.nPartidosKO++];
        m.pA = ganadores[k]; m.pB = ganadores[k + 1];
        m.golA = 0; m.golB = 0; m.jugado = false; m.ronda = ronda + 1;
    }
}

static void generarKnockout() {
    uint8_t calificados[TORNEO_MAX_GRUPOS * 2];
    uint8_t nCalif = 0;
    for (uint8_t g = 0; g < torneo.nGrupos; g++) {
        FilaPos filas[TORNEO_MAX_PART_GRUPO];
        uint8_t n;
        calcularStandings(g, filas, n);
        uint8_t top = n < 2 ? n : 2;
        for (uint8_t k = 0; k < top; k++) calificados[nCalif++] = filas[k].idx;
    }

    // sorteo — decide tanto el orden de emparejamiento como quién recibe bye
    for (int8_t i = nCalif - 1; i > 0; i--) {
        int8_t j = random(0, i + 1);
        uint8_t t = calificados[i]; calificados[i] = calificados[j]; calificados[j] = t;
    }

    uint8_t nSlots = 1;
    while (nSlots < nCalif) nSlots <<= 1;
    uint8_t nByes = nSlots - nCalif;

    torneo.nPartidosKO = 0;
    for (uint8_t i = 0; i < nByes; i++) {
        PartidoKO& m = torneo.partidosKO[torneo.nPartidosKO++];
        m.pA = calificados[i]; m.pB = -1; m.golA = 0; m.golB = 0; m.jugado = true; m.ronda = 0;
    }
    for (uint8_t i = nByes; i + 1 < nCalif; i += 2) {
        PartidoKO& m = torneo.partidosKO[torneo.nPartidosKO++];
        m.pA = calificados[i]; m.pB = calificados[i + 1];
        m.golA = 0; m.golB = 0; m.jugado = false; m.ronda = 0;
    }

    torneo.fase = 1;
    checkAvanzarRonda(0);   // resuelve el caso límite en que la ronda ya cerró solo con byes
}

// ── API pública ──────────────────────────────────────────────────────────────

bool torneoCrear(const String nombres[], uint8_t n, uint8_t nGruposSugerido) {
    if (n < 2 || n > TORNEO_MAX_JUG) return false;

    uint8_t minGrupos = (n + TORNEO_MAX_PART_GRUPO - 1) / TORNEO_MAX_PART_GRUPO;  // ceil(n/4)
    uint8_t maxGrupos = TORNEO_MAX_GRUPOS < (n / 2) ? TORNEO_MAX_GRUPOS : (n / 2);
    if (maxGrupos < minGrupos) maxGrupos = minGrupos;
    uint8_t nGrupos = constrain(nGruposSugerido, minGrupos, maxGrupos);

    memset(&torneo, 0, sizeof(Torneo));
    torneo.nParticipantes = n;
    for (uint8_t i = 0; i < n; i++) strlcpy(torneo.participantes[i].nombre, nombres[i].c_str(), sizeof(torneo.participantes[i].nombre));

    uint8_t orden[TORNEO_MAX_JUG];
    for (uint8_t i = 0; i < n; i++) orden[i] = i;
    for (int8_t i = n - 1; i > 0; i--) {
        int8_t j = random(0, i + 1);
        uint8_t t = orden[i]; orden[i] = orden[j]; orden[j] = t;
    }

    torneo.nGrupos = nGrupos;
    for (uint8_t i = 0; i < n; i++) torneo.participantes[orden[i]].grupo = i % nGrupos;

    torneo.nPartidosGrupo = 0;
    for (uint8_t g = 0; g < nGrupos; g++) {
        uint8_t miembros[TORNEO_MAX_PART_GRUPO], nm = 0;
        for (uint8_t i = 0; i < n; i++) if (torneo.participantes[i].grupo == g) miembros[nm++] = i;
        for (uint8_t a = 0; a < nm; a++) {
            for (uint8_t b = a + 1; b < nm; b++) {
                PartidoGrupo& m = torneo.partidosGrupo[torneo.nPartidosGrupo++];
                m.pA = miembros[a]; m.pB = miembros[b]; m.golA = 0; m.golB = 0; m.jugado = false;
            }
        }
    }

    torneo.fase          = 0;
    torneo.activo        = true;
    torneo.partidoEnJuego = -1;
    torneo.campeon        = -1;
    guardarTorneo();
    return true;
}

bool torneoJugar(uint8_t idx, bool esKO) {
    if (!torneo.activo) return false;
    if (esKO) {
        if (idx >= torneo.nPartidosKO) return false;
        PartidoKO& m = torneo.partidosKO[idx];
        if (m.jugado || m.pB < 0) return false;
    } else {
        if (idx >= torneo.nPartidosGrupo) return false;
        if (torneo.partidosGrupo[idx].jugado) return false;
    }
    torneo.partidoEnJuego = idx;
    torneo.enJuegoEsKO    = esKO;
    guardarTorneo();
    return true;
}

// ── Próximo partido — el orden lo decide el sistema, no el usuario ───────────
// saltar=0 → el próximo a jugar; saltar=1 → el siguiente después de ese.
// Salta el partido que está (o acaba de estar) en juego, aunque todavía no
// esté marcado "jugado" (se marca recién al confirmar el resultado).
static bool buscarProximoIdx(uint8_t saltar, uint8_t& idxOut, bool& esKOOut) {
    uint8_t contados = 0;
    if (torneo.fase == 0) {
        for (uint8_t i = 0; i < torneo.nPartidosGrupo; i++) {
            if (torneo.partidosGrupo[i].jugado) continue;
            if (!torneo.enJuegoEsKO && (int8_t)i == torneo.partidoEnJuego) continue;
            if (contados == saltar) { idxOut = i; esKOOut = false; return true; }
            contados++;
        }
    } else if (torneo.fase == 1) {
        for (uint8_t i = 0; i < torneo.nPartidosKO; i++) {
            PartidoKO& m = torneo.partidosKO[i];
            if (m.jugado || m.pB < 0) continue;
            if (torneo.enJuegoEsKO && (int8_t)i == torneo.partidoEnJuego) continue;
            if (contados == saltar) { idxOut = i; esKOOut = true; return true; }
            contados++;
        }
    }
    return false;
}

bool torneoJugarProximo() {
    uint8_t idx; bool esKO;
    if (!buscarProximoIdx(0, idx, esKO)) return false;
    return torneoJugar(idx, esKO);
}

bool torneoProximosNombres(char* buf, size_t len) {
    uint8_t idx; bool esKO;
    if (!buscarProximoIdx(0, idx, esKO)) return false;
    int8_t pA = esKO ? torneo.partidosKO[idx].pA : torneo.partidosGrupo[idx].pA;
    int8_t pB = esKO ? torneo.partidosKO[idx].pB : torneo.partidosGrupo[idx].pB;
    if (pA < 0 || pB < 0) return false;
    snprintf(buf, len, "%s y %s", torneo.participantes[pA].nombre, torneo.participantes[pB].nombre);
    return true;
}

bool torneoConfirmar(uint8_t golA, uint8_t golB) {
    if (!torneo.activo || torneo.partidoEnJuego < 0) return false;

    if (torneo.enJuegoEsKO) {
        if (golA == golB) return false;   // en knockout no hay empates — hay que desempatar en la mesa
        PartidoKO& m = torneo.partidosKO[torneo.partidoEnJuego];
        m.golA = golA; m.golB = golB; m.jugado = true;
        uint8_t ronda = m.ronda;
        torneo.partidoEnJuego = -1;
        checkAvanzarRonda(ronda);
    } else {
        PartidoGrupo& m = torneo.partidosGrupo[torneo.partidoEnJuego];
        m.golA = golA; m.golB = golB; m.jugado = true;
        torneo.partidoEnJuego = -1;
        bool todosJugados = true;
        for (uint8_t i = 0; i < torneo.nPartidosGrupo; i++) {
            if (!torneo.partidosGrupo[i].jugado) { todosJugados = false; break; }
        }
        if (todosJugados) generarKnockout();
    }
    if (torneo.fase == 2) {
        // Campeón coronado: queda en RAM para esta sesión, pero no se persiste —
        // un reinicio arranca con el torneo vacío en vez de arrastrar uno viejo.
        borrarTorneoNVS();
    } else {
        guardarTorneo();
    }
    return true;
}

// ── JSON ─────────────────────────────────────────────────────────────────────

static String jesc(const char* s) {
    String out;
    for (const char* p = s; *p; p++) {
        if (*p == '"' || *p == '\\') out += '\\';
        out += *p;
    }
    return out;
}

static const char* nombreDe(int8_t idx) {
    return idx < 0 ? "" : torneo.participantes[idx].nombre;
}

static String parJSON(uint8_t saltar) {
    uint8_t idx; bool esKO;
    if (!buscarProximoIdx(saltar, idx, esKO)) return "null";
    int8_t pA = esKO ? torneo.partidosKO[idx].pA : torneo.partidosGrupo[idx].pA;
    int8_t pB = esKO ? torneo.partidosKO[idx].pB : torneo.partidosGrupo[idx].pB;
    return "{\"pA\":\"" + jesc(nombreDe(pA)) + "\",\"pB\":\"" + jesc(nombreDe(pB)) + "\"}";
}

String torneoEstadoJSON() {
    String j = "{";
    j += "\"activo\":" + String(torneo.activo ? "true" : "false");
    j += ",\"fase\":" + String(torneo.fase);
    j += ",\"campeon\":";
    j += torneo.campeon >= 0 ? ("\"" + jesc(nombreDe(torneo.campeon)) + "\"") : "null";
    j += ",\"partidoEnJuego\":" + String(torneo.partidoEnJuego);
    j += ",\"enJuegoEsKO\":" + String(torneo.enJuegoEsKO ? "true" : "false");
    j += ",\"proximo\":" + parJSON(0);
    j += ",\"despues\":" + parJSON(1);

    j += ",\"grupos\":[";
    for (uint8_t g = 0; g < torneo.nGrupos; g++) {
        if (g) j += ",";
        FilaPos filas[TORNEO_MAX_PART_GRUPO];
        uint8_t n;
        calcularStandings(g, filas, n);
        j += "{\"tabla\":[";
        for (uint8_t k = 0; k < n; k++) {
            if (k) j += ",";
            FilaPos& f = filas[k];
            j += "{\"nombre\":\"" + jesc(torneo.participantes[f.idx].nombre) + "\""
               + ",\"pj\":" + String(f.pj) + ",\"pg\":" + String(f.pg) + ",\"pe\":" + String(f.pe) + ",\"pp\":" + String(f.pp)
               + ",\"gf\":" + String(f.gf) + ",\"gc\":" + String(f.gc) + ",\"dif\":" + String(f.dif) + ",\"pts\":" + String(f.pts) + "}";
        }
        j += "]}";
    }
    j += "]";

    j += ",\"partidosGrupo\":[";
    for (uint8_t i = 0; i < torneo.nPartidosGrupo; i++) {
        if (i) j += ",";
        PartidoGrupo& m = torneo.partidosGrupo[i];
        j += "{\"idx\":" + String(i) + ",\"grupo\":" + String(torneo.participantes[m.pA].grupo)
           + ",\"pA\":\"" + jesc(nombreDe(m.pA)) + "\",\"pB\":\"" + jesc(nombreDe(m.pB)) + "\""
           + ",\"golA\":" + String(m.golA) + ",\"golB\":" + String(m.golB) + ",\"jugado\":" + String(m.jugado ? "true" : "false") + "}";
    }
    j += "]";

    j += ",\"partidosKO\":[";
    for (uint8_t i = 0; i < torneo.nPartidosKO; i++) {
        if (i) j += ",";
        PartidoKO& m = torneo.partidosKO[i];
        j += "{\"idx\":" + String(i) + ",\"ronda\":" + String(m.ronda)
           + ",\"pA\":\"" + jesc(nombreDe(m.pA)) + "\",\"pB\":" + (m.pB < 0 ? "null" : ("\"" + jesc(nombreDe(m.pB)) + "\""))
           + ",\"golA\":" + String(m.golA) + ",\"golB\":" + String(m.golB) + ",\"jugado\":" + String(m.jugado ? "true" : "false") + "}";
    }
    j += "]}";
    return j;
}
