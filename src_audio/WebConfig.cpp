#include "WebConfig.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Comentarista.h"
#include "Torneo.h"
#include "Display.h"
#include <Arduino.h>
#include <string.h>

extern void resetearDeteccionGoles();  // definida en main.cpp

static Partido* _partido = nullptr;
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#define WIFI_SSID "Metegol"
#define WIFI_PASS "metegol123"

Config config;
static WebServer server(80);
static DNSServer dns;
static Preferences prefs;
static bool _pendingVolUpdate = false;

#define MAX_NETS 3
static char     _staSSID[MAX_NETS][33] = {};
static char     _staPass[MAX_NETS][65] = {};
static uint8_t  _nNets       = 0;
static uint8_t  _tryNetIdx   = 0;
static bool     _staAnunciado = false;
static uint32_t _staStartMs  = 0;
static bool     _staGaveUp   = false;

static void anunciarSTA() {
    if (_staAnunciado) return;
    _staAnunciado = true;
    Serial.println();
    Serial.println("───────────────── Metegol online ─────────────────");
    Serial.printf ("  Red     : %s\n", WiFi.SSID().c_str());
    Serial.printf ("  IP      : http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("  DNS     : http://metegol.local/");
    Serial.println("──────────────────────────────────────────────────");
}

static void guardarWiFiCreds() {
    prefs.begin("metegol-wifi", false);
    prefs.putUChar("nNets", _nNets);
    for (uint8_t i = 0; i < MAX_NETS; i++) {
        char ks[4], kp[4];
        snprintf(ks, sizeof(ks), "s%d", i);
        snprintf(kp, sizeof(kp), "p%d", i);
        if (i < _nNets) { prefs.putString(ks, _staSSID[i]); prefs.putString(kp, _staPass[i]); }
        else            { prefs.remove(ks); prefs.remove(kp); }
    }
    prefs.end();
}

static void cargarWiFiCreds() {
    prefs.begin("metegol-wifi", false);
    uint8_t nv = prefs.getUChar("nNets", 0xFF);
    if (nv == 0xFF) {
        // Migrar formato viejo (clave "ssid"/"pass") al nuevo multi-red
        String os = prefs.getString("ssid", "");
        String op = prefs.getString("pass", "");
        if (os.length() > 0) {
            strlcpy(_staSSID[0], os.c_str(), sizeof(_staSSID[0]));
            strlcpy(_staPass[0], op.c_str(), sizeof(_staPass[0]));
            _nNets = 1;
        }
        prefs.remove("ssid"); prefs.remove("pass");
        prefs.end();
        guardarWiFiCreds();
        return;
    }
    _nNets = min(nv, (uint8_t)MAX_NETS);
    for (uint8_t i = 0; i < _nNets; i++) {
        char ks[4], kp[4];
        snprintf(ks, sizeof(ks), "s%d", i);
        snprintf(kp, sizeof(kp), "p%d", i);
        strlcpy(_staSSID[i], prefs.getString(ks, "").c_str(), sizeof(_staSSID[i]));
        strlcpy(_staPass[i], prefs.getString(kp, "").c_str(), sizeof(_staPass[i]));
    }
    prefs.end();
}

static void addOrUpdateNet(const char* ssid, const char* pass) {
    for (uint8_t i = 0; i < _nNets; i++) {
        if (strcmp(_staSSID[i], ssid) == 0) {
            strlcpy(_staPass[i], pass, sizeof(_staPass[i]));
            guardarWiFiCreds();
            return;
        }
    }
    if (_nNets < MAX_NETS) {
        strlcpy(_staSSID[_nNets], ssid, sizeof(_staSSID[_nNets]));
        strlcpy(_staPass[_nNets], pass, sizeof(_staPass[_nNets]));
        _nNets++;
    } else {
        // Lista llena — rotar: descarta la más vieja, agrega al final
        for (uint8_t i = 0; i < MAX_NETS - 1; i++) {
            strlcpy(_staSSID[i], _staSSID[i+1], sizeof(_staSSID[i]));
            strlcpy(_staPass[i], _staPass[i+1], sizeof(_staPass[i]));
        }
        strlcpy(_staSSID[MAX_NETS-1], ssid, sizeof(_staSSID[MAX_NETS-1]));
        strlcpy(_staPass[MAX_NETS-1], pass, sizeof(_staPass[MAX_NETS-1]));
    }
    guardarWiFiCreds();
}

// ── Persistencia ─────────────────────────────────────────────────────────────

// Versión del esquema de NVS para SP2.
// Incrementar cada vez que cambien los key names de SP2 — fuerza defaults frescos.
#define CONFIG_SP2_VERSION 5

static void cargarConfig() {
    prefs.begin("metegol", false);   // false = lectura/escritura (necesario para guardar versión)
    config.volumenVoz      = 30;
    config.volumenAmbiente = 30;
    config.modoJuego       = 0;  // siempre arranca en goles
    config.golesMax        = prefs.getUChar("golesMax",    4);
    config.duracionMin     = prefs.getUShort("durMin",     5);
    config.brillo          = prefs.getUChar("brillo",      5);
    config.velocidadScroll = prefs.getUChar("velScroll",  40);
    config.intervaloDisplay = prefs.getUChar("iDisp",      5);
    config.pistaAmbiente   = prefs.getUChar("pistaAmb",    3);

    // Display — textos customizables
    strlcpy(config.textoBoot,           prefs.getString("txtBoot", "METEGOL!").c_str(),              sizeof(config.textoBoot));
    strlcpy(config.textoArranca,        prefs.getString("txtArr",  "ARRANCAAA!").c_str(),             sizeof(config.textoArranca));
    strlcpy(config.textoPausa,          prefs.getString("txtPau",  "PAUSA!").c_str(),                 sizeof(config.textoPausa));
    strlcpy(config.textoReanuda,        prefs.getString("txtRea",  "VAMOS!").c_str(),                 sizeof(config.textoReanuda));
    strlcpy(config.textoCancelado,      prefs.getString("txtCan",  "CANCELADO!").c_str(),              sizeof(config.textoCancelado));
    strlcpy(config.textoGol,            prefs.getString("txtGol",  "Gollll!!!").c_str(),               sizeof(config.textoGol));
    strlcpy(config.textoGanadorCeleste, prefs.getString("txtGC",   "Fin! Ganador Celeste!").c_str(),   sizeof(config.textoGanadorCeleste));
    strlcpy(config.textoGanadorBlanco,  prefs.getString("txtGB",   "Fin! Ganador Blanco!").c_str(),    sizeof(config.textoGanadorBlanco));
    strlcpy(config.textoEmpate,         prefs.getString("txtEmp",  "Fin! Empate!").c_str(),            sizeof(config.textoEmpate));
    strlcpy(config.textoPreparense,     prefs.getString("txtPrep", "Preparense").c_str(),              sizeof(config.textoPreparense));

    // Comentarista — thresholds
    config.intervaloComentariosMin = prefs.getUShort("intervComMin",  12);
    config.intervaloComentariosMax = prefs.getUShort("intervComMax",  35);
    config.intervaloStats          = prefs.getUShort("intervStats",    5);
    config.goleadaDiff             = prefs.getUChar("goleadaDiff",    3);  // 3+ goles de diff = goleada
    config.calienteGoles           = prefs.getUChar("calienteGol",    4);  // 4+ goles totales = puede ser caliente
    config.hinchadaGol             = prefs.getUChar("hincGol",         2);  // suena hinchada tras el gol N
    config.inicioSegs              = prefs.getUShort("inicioSegs",   30);
    config.primerosMinsSegs        = prefs.getUShort("primMinsSegs",  20);  // 20s de apertura
    config.ultimoTramoSegs         = prefs.getUShort("ultiTramoSeg",  60);  // últimos 60s = tensión
    config.umbralAburridoSegs      = prefs.getUShort("umbralAbur",   50);  // 50s sin goles = aburrido
    // Comentarista — rangos por estado
    config.comentInicio.desde       = prefs.getUChar("cInD",   1);
    config.comentInicio.hasta       = prefs.getUChar("cInH",   6);
    config.comentPrimerosMins.desde = prefs.getUChar("cPrD",   7);
    config.comentPrimerosMins.hasta = prefs.getUChar("cPrH",  12);
    config.comentParejo.desde       = prefs.getUChar("cPaD",  13);
    config.comentParejo.hasta       = prefs.getUChar("cPaH",  18);
    config.comentCaliente.desde     = prefs.getUChar("cCaD",  19);
    config.comentCaliente.hasta     = prefs.getUChar("cCaH",  24);
    config.comentGoleada.desde      = prefs.getUChar("cGoD",  25);
    config.comentGoleada.hasta      = prefs.getUChar("cGoH",  30);
    config.comentDefinido.desde     = prefs.getUChar("cDeD",  31);
    config.comentDefinido.hasta     = prefs.getUChar("cDeH",  36);
    config.comentUltimoTramoGeneral.desde      = prefs.getUChar("cUtGenD",  37);
    config.comentUltimoTramoGeneral.hasta      = prefs.getUChar("cUtGenH",  42);
    config.comentUltimoTramoEmpateGoles.desde  = prefs.getUChar("cUtEmpD",  89);
    config.comentUltimoTramoEmpateGoles.hasta  = prefs.getUChar("cUtEmpH",  94);
    config.comentUltimoTramoGoleada.desde      = prefs.getUChar("cUtGolD",  95);
    config.comentUltimoTramoGoleada.hasta      = prefs.getUChar("cUtGolH", 100);
    config.comentUltimoTramoAjustado.desde     = prefs.getUChar("cUtAjD",  101);
    config.comentUltimoTramoAjustado.hasta     = prefs.getUChar("cUtAjH",  106);
    config.comentUltimoTramoAburrido.desde     = prefs.getUChar("cUtAbD",  107);
    config.comentUltimoTramoAburrido.hasta     = prefs.getUChar("cUtAbH",  111);
    config.comentAburrido.desde     = prefs.getUChar("cAbD",  43);
    config.comentAburrido.hasta     = prefs.getUChar("cAbH",  48);
    config.comentTranquilo.desde    = prefs.getUChar("cTrD",  49);
    config.comentTranquilo.hasta    = prefs.getUChar("cTrH",  54);
    // Goles — rangos contextuales
    config.golNormal.desde          = prefs.getUChar("gNorD", 55);
    config.golNormal.hasta          = prefs.getUChar("gNorH", 58);
    config.golEfusivo.desde         = prefs.getUChar("gEfD",  59);
    config.golEfusivo.hasta         = prefs.getUChar("gEfH",  62);
    config.golEmpate.desde          = prefs.getUChar("gEmD",  63);
    config.golEmpate.hasta          = prefs.getUChar("gEmH",  66);
    config.golCaliente.desde        = prefs.getUChar("gCaD",  67);
    config.golCaliente.hasta        = prefs.getUChar("gCaH",  70);
    config.golAgonico.desde         = prefs.getUChar("gAgD",  71);
    config.golAgonico.hasta         = prefs.getUChar("gAgH",  73);
    config.golAgonicoEmpate.desde   = prefs.getUChar("gAeD",  74);
    config.golAgonicoEmpate.hasta   = prefs.getUChar("gAeH",  76);
    // Pitidos
    config.pitidoInicio.desde       = prefs.getUChar("pItD",  77);
    config.pitidoInicio.hasta       = prefs.getUChar("pItH",  78);
    config.pitidoFinal.desde        = prefs.getUChar("pFiD",  79);
    config.pitidoFinal.hasta        = prefs.getUChar("pFiH",  80);
    // Finales
    config.finalEmpate.desde        = prefs.getUChar("fEmD",  81);
    config.finalEmpate.hasta        = prefs.getUChar("fEmH",  82);
    config.finalAplastante.desde    = prefs.getUChar("fApD",  83);
    config.finalAplastante.hasta    = prefs.getUChar("fApH",  84);
    config.finalAjustada.desde      = prefs.getUChar("fAjD",  85);
    config.finalAjustada.hasta      = prefs.getUChar("fAjH",  86);
    config.finalNormal.desde        = prefs.getUChar("fNoD",  87);
    config.finalNormal.hasta        = prefs.getUChar("fNoH",  88);
    // SP2 — ambiente reactivo
    // Si la versión NVS es anterior a CONFIG_SP2_VERSION, los key names cambiaron:
    // se ignoran los valores guardados y se usan defaults, evitando basura de sesiones viejas.
    uint8_t sp2Ver = prefs.getUChar("sp2Ver", 0);
    if (sp2Ver < CONFIG_SP2_VERSION) {
        config.ambienteGenerico = {1, 4};
        config.hinchadaMusica   = {5, 8};
        config.momentoCaliente  = {9, 11};
        config.ambienteGol      = {12, 17};
        // Guarda la versión y los defaults para que el próximo boot los cargue normalmente
        prefs.putUChar("sp2Ver", CONFIG_SP2_VERSION);
        prefs.putUChar("aGe4D",  1);  prefs.putUChar("aGe4H",  4);
        prefs.putUChar("hMu4D",  5);  prefs.putUChar("hMu4H",  8);
        prefs.putUChar("mCa4D",  9);  prefs.putUChar("mCa4H", 11);
        prefs.putUChar("aGoD",  12);  prefs.putUChar("aGoH",  17);
        Serial.printf("\n[Config] SP2 reseteado a defaults (sp2Ver %d → %d)\n", sp2Ver, CONFIG_SP2_VERSION);
    } else {
        config.ambienteGenerico.desde = prefs.getUChar("aGe4D",  1);
        config.ambienteGenerico.hasta = prefs.getUChar("aGe4H",  4);
        config.hinchadaMusica.desde   = prefs.getUChar("hMu4D",  5);
        config.hinchadaMusica.hasta   = prefs.getUChar("hMu4H",  8);
        config.momentoCaliente.desde  = prefs.getUChar("mCa4D",  9);
        config.momentoCaliente.hasta  = prefs.getUChar("mCa4H", 11);
        config.ambienteGol.desde      = prefs.getUChar("aGoD",  12);
        config.ambienteGol.hasta      = prefs.getUChar("aGoH",  17);
    }
    prefs.end();
}

static void guardarConfig() {
    prefs.begin("metegol", false);
    prefs.putUChar("volVoz",    config.volumenVoz);
    prefs.putUChar("volAmb",    config.volumenAmbiente);
    // modoJuego no se persiste — siempre arranca en goles
    prefs.putUChar("golesMax",  config.golesMax);
    prefs.putUShort("durMin",   config.duracionMin);
    prefs.putUChar("brillo",    config.brillo);
    prefs.putUChar("velScroll", config.velocidadScroll);
    prefs.putUChar("iDisp",     config.intervaloDisplay);
    prefs.putUChar("pistaAmb",  config.pistaAmbiente);

    // Display — textos customizables
    prefs.putString("txtBoot", config.textoBoot);
    prefs.putString("txtArr",  config.textoArranca);
    prefs.putString("txtPau",  config.textoPausa);
    prefs.putString("txtRea",  config.textoReanuda);
    prefs.putString("txtCan",  config.textoCancelado);
    prefs.putString("txtGol",  config.textoGol);
    prefs.putString("txtGC",   config.textoGanadorCeleste);
    prefs.putString("txtGB",   config.textoGanadorBlanco);
    prefs.putString("txtEmp",  config.textoEmpate);
    prefs.putString("txtPrep", config.textoPreparense);

    // Comentarista — thresholds
    prefs.putUShort("intervComMin",  config.intervaloComentariosMin);
    prefs.putUShort("intervComMax",  config.intervaloComentariosMax);
    prefs.putUShort("intervStats",   config.intervaloStats);
    prefs.putUChar("goleadaDiff",    config.goleadaDiff);
    prefs.putUChar("calienteGol",    config.calienteGoles);
    prefs.putUChar("hincGol",        config.hinchadaGol);
    prefs.putUShort("inicioSegs",    config.inicioSegs);
    prefs.putUShort("primMinsSegs",  config.primerosMinsSegs);
    prefs.putUShort("ultiTramoSeg",  config.ultimoTramoSegs);
    prefs.putUShort("umbralAbur",    config.umbralAburridoSegs);
    // Comentarista — rangos estado
    prefs.putUChar("cInD",  config.comentInicio.desde);
    prefs.putUChar("cInH",  config.comentInicio.hasta);
    prefs.putUChar("cPrD",  config.comentPrimerosMins.desde);
    prefs.putUChar("cPrH",  config.comentPrimerosMins.hasta);
    prefs.putUChar("cPaD",  config.comentParejo.desde);
    prefs.putUChar("cPaH",  config.comentParejo.hasta);
    prefs.putUChar("cCaD",  config.comentCaliente.desde);
    prefs.putUChar("cCaH",  config.comentCaliente.hasta);
    prefs.putUChar("cGoD",  config.comentGoleada.desde);
    prefs.putUChar("cGoH",  config.comentGoleada.hasta);
    prefs.putUChar("cDeD",  config.comentDefinido.desde);
    prefs.putUChar("cDeH",  config.comentDefinido.hasta);
    prefs.putUChar("cUtGenD", config.comentUltimoTramoGeneral.desde);
    prefs.putUChar("cUtGenH", config.comentUltimoTramoGeneral.hasta);
    prefs.putUChar("cUtEmpD", config.comentUltimoTramoEmpateGoles.desde);
    prefs.putUChar("cUtEmpH", config.comentUltimoTramoEmpateGoles.hasta);
    prefs.putUChar("cUtGolD", config.comentUltimoTramoGoleada.desde);
    prefs.putUChar("cUtGolH", config.comentUltimoTramoGoleada.hasta);
    prefs.putUChar("cUtAjD",  config.comentUltimoTramoAjustado.desde);
    prefs.putUChar("cUtAjH",  config.comentUltimoTramoAjustado.hasta);
    prefs.putUChar("cUtAbD",  config.comentUltimoTramoAburrido.desde);
    prefs.putUChar("cUtAbH",  config.comentUltimoTramoAburrido.hasta);
    prefs.putUChar("cAbD",  config.comentAburrido.desde);
    prefs.putUChar("cAbH",  config.comentAburrido.hasta);
    prefs.putUChar("cTrD",  config.comentTranquilo.desde);
    prefs.putUChar("cTrH",  config.comentTranquilo.hasta);
    // Goles — rangos contextuales
    prefs.putUChar("gNorD", config.golNormal.desde);
    prefs.putUChar("gNorH", config.golNormal.hasta);
    prefs.putUChar("gEfD",  config.golEfusivo.desde);
    prefs.putUChar("gEfH",  config.golEfusivo.hasta);
    prefs.putUChar("gEmD",  config.golEmpate.desde);
    prefs.putUChar("gEmH",  config.golEmpate.hasta);
    prefs.putUChar("gCaD",  config.golCaliente.desde);
    prefs.putUChar("gCaH",  config.golCaliente.hasta);
    prefs.putUChar("gAgD",  config.golAgonico.desde);
    prefs.putUChar("gAgH",  config.golAgonico.hasta);
    prefs.putUChar("gAeD",  config.golAgonicoEmpate.desde);
    prefs.putUChar("gAeH",  config.golAgonicoEmpate.hasta);
    // Pitidos
    prefs.putUChar("pItD",  config.pitidoInicio.desde);
    prefs.putUChar("pItH",  config.pitidoInicio.hasta);
    prefs.putUChar("pFiD",  config.pitidoFinal.desde);
    prefs.putUChar("pFiH",  config.pitidoFinal.hasta);
    // Finales
    prefs.putUChar("fEmD",  config.finalEmpate.desde);
    prefs.putUChar("fEmH",  config.finalEmpate.hasta);
    prefs.putUChar("fApD",  config.finalAplastante.desde);
    prefs.putUChar("fApH",  config.finalAplastante.hasta);
    prefs.putUChar("fAjD",  config.finalAjustada.desde);
    prefs.putUChar("fAjH",  config.finalAjustada.hasta);
    prefs.putUChar("fNoD",  config.finalNormal.desde);
    prefs.putUChar("fNoH",  config.finalNormal.hasta);
    // SP2 — ambiente reactivo
    prefs.putUChar("aGe4D", config.ambienteGenerico.desde);
    prefs.putUChar("aGe4H", config.ambienteGenerico.hasta);
    prefs.putUChar("hMu4D", config.hinchadaMusica.desde);
    prefs.putUChar("hMu4H", config.hinchadaMusica.hasta);
    prefs.putUChar("mCa4D", config.momentoCaliente.desde);
    prefs.putUChar("mCa4H", config.momentoCaliente.hasta);
    prefs.putUChar("aGoD",  config.ambienteGol.desde);
    prefs.putUChar("aGoH",  config.ambienteGol.hasta);
    prefs.end();
}

// ── HTML ─────────────────────────────────────────────────────────────────────

static const char HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Metegol</title>
<style>
  :root {
    --bg: #0a0a0f;
    --card: #13131e;
    --border: #22223a;
    --accent: #00e5ff;
    --pink: #ff4081;
    --purple: #7c4dff;
    --green: #69f0ae;
    --orange: #ff9100;
    --celeste: #00b8d4;
    --blanco: #eceff1;
    --text: #e0e0e0;
    --muted: #5a5a7a;
    --radius: 16px;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', system-ui, sans-serif; min-height: 100vh; padding: 16px; }
  header { text-align: center; padding: 28px 0 22px; }
  header h1 { font-size: 1.9rem; font-weight: 800; letter-spacing: 4px; text-transform: uppercase; background: linear-gradient(135deg, var(--accent), var(--pink)); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
  header p { color: var(--muted); font-size: .75rem; margin-top: 5px; letter-spacing: 2px; text-transform: uppercase; }
  .wrap { max-width: 900px; margin: 0 auto; }
  .partido-panel { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); overflow: hidden; margin-bottom: 18px; }
  .status-bar { background: rgba(255,255,255,.03); border-bottom: 1px solid var(--border); padding: 9px 20px; text-align: center; font-size: .68rem; letter-spacing: 2.5px; text-transform: uppercase; color: var(--muted); }
  .scoreboard { display: flex; align-items: center; padding: 22px 16px 12px; gap: 0; }
  .team { flex: 1; text-align: center; }
  .team-name { display: block; font-size: .62rem; font-weight: 700; letter-spacing: 3px; text-transform: uppercase; margin-bottom: 6px; }
  .team.c .team-name { color: var(--celeste); }
  .team.b .team-name { color: #aaa; }
  .team-score { display: block; font-size: 4.5rem; font-weight: 800; line-height: 1; }
  .team.c .team-score { color: var(--celeste); text-shadow: 0 0 40px rgba(0,184,212,.35); }
  .team.b .team-score { color: var(--blanco); text-shadow: 0 0 40px rgba(236,239,241,.2); }
  .score-sep { font-size: 1.8rem; color: var(--border); padding: 0 16px; font-weight: 300; line-height: 1; align-self: center; }
  .partido-meta { display: none; justify-content: center; gap: 28px; padding: 2px 20px 14px; flex-wrap: wrap; }
  .meta-item { text-align: center; }
  .meta-lbl { display: block; font-size: .58rem; letter-spacing: 2px; text-transform: uppercase; color: var(--muted); margin-bottom: 3px; }
  .meta-val { font-size: 1rem; font-weight: 600; color: var(--text); }
  .estado-badge { display: inline-block; padding: 2px 12px; border-radius: 20px; font-size: .78rem; font-weight: 600; background: rgba(0,229,255,.1); color: var(--accent); }
  .ganador-banner { display: none; text-align: center; padding: 10px 20px 0; font-size: 1.1rem; font-weight: 700; color: var(--accent); }
  .btn-row { display: flex; gap: 10px; justify-content: center; padding: 14px 20px 20px; flex-wrap: wrap; }
  .btn-start { padding: 11px 36px; background: linear-gradient(90deg,var(--accent),#0097a7); border: none; border-radius: 24px; color: #000; font-weight: 700; cursor: pointer; font-size: .88rem; letter-spacing: .5px; }
  .btn-stop  { padding: 11px 36px; background: transparent; border: 1.5px solid #f44336; border-radius: 24px; color: #f44336; font-weight: 700; cursor: pointer; font-size: .88rem; display: none; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 14px; }
  .card { background: var(--card); border: 1px solid var(--border); border-left: 3px solid var(--accent); border-radius: var(--radius); padding: 20px 20px 18px; }
  .card.cg { border-left-color: var(--purple); }
  .card.cc { border-left-color: var(--orange); }
  .card.cr { border-left-color: var(--pink); }
  .card.cw { border-left-color: var(--green); }
  .card h2 { font-size: .66rem; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; color: var(--muted); margin-bottom: 16px; display: flex; align-items: center; gap: 7px; }
  .field { margin-bottom: 14px; }
  .field:last-child { margin-bottom: 0; }
  label { display: flex; justify-content: space-between; align-items: center; font-size: .82rem; color: var(--muted); margin-bottom: 7px; }
  label b { color: var(--text); font-weight: 700; font-size: .88rem; min-width: 28px; text-align: right; }
  input[type=range] { -webkit-appearance: none; width: 100%; height: 5px; background: linear-gradient(to right,var(--accent) var(--p,0%),var(--border) var(--p,0%)); border-radius: 3px; outline: none; cursor: pointer; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%; background: #fff; box-shadow: 0 0 0 2px var(--accent),0 2px 6px rgba(0,0,0,.5); cursor: pointer; }
  .toggle-row { display: flex; gap: 8px; }
  .toggle-btn { flex: 1; padding: 9px; border: 1.5px solid var(--border); border-radius: 8px; background: transparent; color: var(--muted); font-size: .82rem; cursor: pointer; transition: all .15s; }
  .toggle-btn.active { border-color: var(--purple); color: var(--purple); background: rgba(124,77,255,.1); }
  .ni { width: 54px; background: rgba(255,255,255,.05); border: 1px solid var(--border); border-radius: 8px; color: var(--text); font-size: .84rem; padding: 5px 7px; text-align: center; outline: none; transition: border-color .15s; }
  .ni:focus { border-color: var(--accent); }
  .ti { width: 100%; background: rgba(255,255,255,.05); border: 1px solid var(--border); border-radius: 8px; color: var(--text); font-size: .84rem; padding: 8px 10px; outline: none; transition: border-color .15s; }
  .ti:focus { border-color: var(--accent); }
  .rt { width: 100%; border-collapse: collapse; font-size: .81rem; }
  .rt th { color: var(--muted); font-weight: 500; padding: 3px 5px 8px; font-size: .7rem; letter-spacing: 1px; text-transform: uppercase; }
  .rt th:first-child { text-align: left; }
  .rt td { padding: 3px 4px; vertical-align: middle; }
  .rs { padding: 8px 0 3px; color: var(--muted); font-size: .63rem; letter-spacing: 2px; text-transform: uppercase; }
  .rl { display: inline-flex; align-items: center; gap: 6px; color: var(--text); }
  .rd { width: 6px; height: 6px; border-radius: 50%; background: var(--accent); flex-shrink: 0; }
  .rd.g { background: var(--pink); }
  .save-bar { margin-top: 20px; padding: 12px 0 4px; text-align: center; }
  .btn-save { padding: 14px 64px; background: linear-gradient(90deg,var(--accent),#0097a7); border: none; border-radius: 32px; color: #000; font-weight: 800; font-size: .95rem; letter-spacing: 2px; cursor: pointer; box-shadow: 0 0 0 1px rgba(0,229,255,.3), 0 8px 32px rgba(0,229,255,.35); transition: opacity .15s,transform .12s,box-shadow .15s; }
  .btn-save:hover { box-shadow: 0 0 0 2px rgba(0,229,255,.6), 0 10px 40px rgba(0,229,255,.5); }
  .btn-save:active { opacity: .85; transform: scale(.97); }
  .sec-lbl { font-size: .61rem; font-weight: 700; letter-spacing: 2.5px; text-transform: uppercase; margin-bottom: 11px; padding-bottom: 8px; border-bottom: 1px solid var(--border); }
  .rng-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; align-items: start; }
  @media(max-width:480px){ .rng-grid { grid-template-columns: 1fr; } }
  #toast { position: fixed; bottom: 26px; left: 50%; transform: translateX(-50%) translateY(80px); background: var(--green); color: #000; padding: 10px 28px; border-radius: 30px; font-weight: 700; font-size: .86rem; transition: transform .26s; pointer-events: none; white-space: nowrap; z-index: 9999; }
  #toast.show { transform: translateX(-50%) translateY(0); }
  #fab-save { position: fixed; bottom: 22px; right: 22px; padding: 14px 22px; background: linear-gradient(135deg,var(--accent),#0097a7); border: none; border-radius: 50px; color: #000; font-weight: 800; font-size: .82rem; letter-spacing: 1.5px; cursor: pointer; box-shadow: 0 4px 24px rgba(0,229,255,.45); z-index: 9998; transition: transform .12s, box-shadow .15s; }
  #fab-save:hover { transform: scale(1.05); box-shadow: 0 6px 32px rgba(0,229,255,.65); }
  #fab-save:active { transform: scale(.96); }

  .tabs { display: flex; gap: 6px; background: var(--card); border: 1px solid var(--border); border-radius: 14px; padding: 5px; margin-bottom: 18px; }
  .tab-btn { flex: 1; padding: 10px 6px; border: none; border-radius: 10px; background: transparent; color: var(--muted); font-weight: 700; font-size: .78rem; letter-spacing: 1px; text-transform: uppercase; cursor: pointer; transition: all .15s; }
  .tab-btn.active { background: linear-gradient(135deg, var(--accent), var(--pink)); color: #000; }
  .tab-panel { animation: fadeIn .18s ease; }
  @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

  .torneo-setup { max-width: 460px; margin: 0 auto; }
  .torneo-setup .names { display: flex; flex-direction: column; gap: 8px; margin: 14px 0; }
  .torneo-setup input[type=text] { width: 100%; padding: 10px 12px; background: rgba(255,255,255,.05); border: 1px solid var(--border); border-radius: 8px; color: var(--text); font-size: .88rem; outline: none; }
  .torneo-setup input[type=text]:focus { border-color: var(--accent); }
  .btn-primary { width: 100%; padding: 13px; background: linear-gradient(90deg,var(--accent),#0097a7); border: none; border-radius: 24px; color: #000; font-weight: 800; font-size: .88rem; letter-spacing: 1px; cursor: pointer; }
  .btn-secondary { padding: 8px 20px; background: transparent; border: 1.5px solid var(--border); border-radius: 20px; color: var(--muted); font-weight: 700; font-size: .76rem; cursor: pointer; }
  .btn-ghost-danger { padding: 8px 20px; background: transparent; border: 1.5px solid rgba(244,67,54,.4); border-radius: 20px; color: #f44336; font-weight: 700; font-size: .76rem; cursor: pointer; }
  .grupo-titulo { font-size: .68rem; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; color: var(--purple); margin: 18px 0 8px; }
  .grupo-titulo:first-child { margin-top: 0; }
  .partidos-lista { display: flex; flex-direction: column; gap: 8px; margin-bottom: 4px; }
  .partido-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 10px 14px; background: rgba(255,255,255,.03); border: 1px solid var(--border); border-radius: 10px; font-size: .84rem; }
  .partido-row .vs { color: var(--muted); font-size: .74rem; text-align: center; flex: 1; }
  .partido-row .res { font-weight: 700; color: var(--accent); }
  .btn-jugar { padding: 6px 16px; background: rgba(0,229,255,.12); border: 1px solid var(--accent); border-radius: 16px; color: var(--accent); font-weight: 700; font-size: .74rem; cursor: pointer; white-space: nowrap; }
  .en-juego-banner { background: rgba(255,145,0,.1); border: 1px solid rgba(255,145,0,.35); border-radius: 12px; padding: 12px 16px; margin-bottom: 16px; text-align: center; }
  .en-juego-banner b { color: var(--orange); }
  .ronda-titulo { font-size: .68rem; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; color: var(--accent); margin: 18px 0 8px; }
  .ronda-titulo:first-child { margin-top: 0; }
  .campeon-banner { text-align: center; padding: 28px 16px; }
  .campeon-banner .trofeo { font-size: 2.6rem; }
  .campeon-banner .nombre { font-size: 1.4rem; font-weight: 800; color: var(--accent); margin-top: 6px; }
  .empty-hint { text-align: center; color: var(--muted); font-size: .84rem; padding: 30px 10px; }
</style>
</head>
<body>
<div class="wrap">
<header>
  <h1>⚽ Metegol</h1>
  <p>Sistema de comentarios</p>
</header>

<nav class="tabs">
  <button type="button" class="tab-btn active" id="tabbtn-partido" onclick="showTab('partido')">Partido</button>
  <button type="button" class="tab-btn" id="tabbtn-torneo" onclick="showTab('torneo')">Torneo</button>
  <button type="button" class="tab-btn" id="tabbtn-ajustes" onclick="showTab('ajustes')">Ajustes</button>
</nav>

<section id="tab-partido" class="tab-panel">
<div class="partido-panel">
  <div class="status-bar" id="partido-label">En espera</div>
  <div class="scoreboard">
    <div class="team c">
      <span class="team-name">Celeste</span>
      <span class="team-score" id="sc-c">0</span>
    </div>
    <span class="score-sep">—</span>
    <div class="team b">
      <span class="team-score" id="sc-b">0</span>
      <span class="team-name">Blanco</span>
    </div>
  </div>
  <div class="partido-meta" id="pmeta">
    <div class="meta-item">
      <span class="meta-lbl">Tiempo</span>
      <span class="meta-val" id="tiempo-juego">00:00</span>
    </div>
    <div class="meta-item">
      <span class="meta-lbl">Estado</span>
      <span id="estado-badge" class="estado-badge">—</span>
    </div>
    <div class="meta-item" id="timer-wrap" style="display:none">
      <span class="meta-lbl">Restante</span>
      <span class="meta-val" id="timer">--:--</span>
    </div>
  </div>
  <div class="ganador-banner" id="ganador-wrap"></div>
  <div class="btn-row">
    <button id="btn-start" class="btn-start" onclick="iniciarPartido()">Iniciar partido</button>
    <button id="btn-stop"  class="btn-stop"  onclick="pararPartido()">Parar partido</button>
  </div>
</div>
</section>

<section id="tab-torneo" class="tab-panel" style="display:none">

<div class="card" id="torneo-setup-card">
  <h2>🏆 Nuevo torneo</h2>
  <div class="torneo-setup">
    <div class="field">
      <label>Cantidad de jugadores <b id="tn">4</b></label>
      <input type="range" id="tCant" min="2" max="16" value="4" oninput="sl(this,'tn');torneoCantidadCambio(this.value)">
    </div>
    <div class="field">
      <label>Grupos <b id="tg">1</b></label>
      <input type="range" id="tGrupos" min="1" max="4" value="1" oninput="sl(this,'tg')">
    </div>
    <div class="names" id="torneo-nombres"></div>
    <button type="button" class="btn-primary" onclick="crearTorneo()">ARMAR TORNEO</button>
  </div>
</div>

<div id="torneo-activo" style="display:none">
  <div class="card cg" id="torneo-en-juego" style="display:none">
    <div class="en-juego-banner">
      <span id="en-juego-txt"></span><br>
      <button type="button" class="btn-jugar" style="margin-top:8px" onclick="showTab('partido')">Ir a la mesa 🎮</button>
      <button type="button" class="btn-jugar" id="btn-confirmar-torneo" style="margin-top:8px;display:none" onclick="confirmarTorneo()">Confirmar resultado ✓</button>
    </div>
  </div>

  <div class="card cg" id="torneo-proximo-card" style="display:none">
    <h2>🎮 Próximo partido</h2>
    <p style="font-size:1rem;text-align:center;margin-bottom:4px"><b id="proximo-txt"></b></p>
    <p style="font-size:.78rem;color:var(--muted);text-align:center;margin-bottom:14px" id="despues-txt"></p>
    <button type="button" class="btn-primary" onclick="jugarProximo()">¡Jugar! 🎮</button>
  </div>

  <div class="card" id="torneo-grupos-card">
    <h2>📋 Fase de grupos</h2>
    <div id="torneo-grupos"></div>
  </div>

  <div class="card cg" id="torneo-ko-card" style="display:none">
    <h2>⚔️ Eliminación directa</h2>
    <div id="torneo-ko"></div>
  </div>

  <div class="card cr" id="torneo-campeon-card" style="display:none">
    <div class="campeon-banner">
      <div class="trofeo">🏆</div>
      <div>Campeón del torneo</div>
      <div class="nombre" id="torneo-campeon-nombre"></div>
    </div>
  </div>

  <div style="text-align:center;padding:6px 0 2px">
    <button type="button" class="btn-ghost-danger" onclick="cancelarTorneo()">Cancelar torneo</button>
  </div>
</div>

</section>

<section id="tab-ajustes" class="tab-panel" style="display:none">
<form id="cfg" method="POST" action="/save">
<div class="grid">

  <div class="card">
    <h2>🔊 Audio</h2>
    <p class="sec-lbl" style="color:var(--celeste)">SP1 — Comentarios</p>
    <div class="field">
      <label>Volumen <b id="vv">%VOL_VOZ%</b></label>
      <input type="range" name="volumenVoz" min="0" max="30" value="%VOL_VOZ%" oninput="sl(this,'vv')">
    </div>
    <p class="sec-lbl" style="color:var(--muted);margin-top:14px">SP2 — Ambiente</p>
    <div class="field">
      <label>Volumen <b id="va">%VOL_AMB%</b></label>
      <input type="range" name="volumenAmbiente" min="0" max="30" value="%VOL_AMB%" oninput="sl(this,'va')">
    </div>
  </div>

  <div class="card cg">
    <h2>🎮 Modalidad</h2>
    <div class="field">
      <label>Modo de juego</label>
      <div class="toggle-row">
        <button type="button" class="toggle-btn %MODO_GOLES_ACTIVE%" onclick="setModo(0)">Por goles</button>
        <button type="button" class="toggle-btn %MODO_TIEMPO_ACTIVE%" onclick="setModo(1)">Por tiempo</button>
      </div>
      <input type="hidden" name="modoJuego" id="modoJuego" value="%MODO%">
    </div>
    <div class="field" id="row-goles">
      <label>Goles para ganar <b id="gm">%GOLES_MAX%</b></label>
      <input type="range" name="golesMax" min="4" max="10" value="%GOLES_MAX%" oninput="sl(this,'gm')">
    </div>
    <div class="field" id="row-tiempo">
      <label>Duración (min) <b id="dm">%DUR_MIN%</b></label>
      <input type="range" name="duracionMin" min="3" max="8" value="%DUR_MIN%" oninput="sl(this,'dm')">
    </div>
    <div class="field">
      <label>Cambio display seg <b id="id">%INTERV_DISP%</b></label>
      <input type="range" name="intervaloDisplay" min="2" max="30" value="%INTERV_DISP%" oninput="sl(this,'id')">
    </div>
  </div>

  <div class="card" style="grid-column:1/-1">
    <h2>🖥️ Textos de la farola</h2>
    <div class="rng-grid">
      <div>
        <div class="field"><label>Al bootear el ESP32</label><input class="ti" type="text" name="txtBoot" maxlength="18" value="%TXT_BOOT%"></div>
        <div class="field"><label>Al iniciar partido</label><input class="ti" type="text" name="txtArr" maxlength="18" value="%TXT_ARR%"></div>
        <div class="field"><label>Al pausar</label><input class="ti" type="text" name="txtPau" maxlength="18" value="%TXT_PAU%"></div>
        <div class="field"><label>Al reanudar</label><input class="ti" type="text" name="txtRea" maxlength="18" value="%TXT_REA%"></div>
      </div>
      <div>
        <div class="field"><label>Al cancelar</label><input class="ti" type="text" name="txtCan" maxlength="18" value="%TXT_CAN%"></div>
        <div class="field"><label>En cada gol</label><input class="ti" type="text" name="txtGol" maxlength="18" value="%TXT_GOL%"></div>
        <div class="field"><label>Fin — ganó celeste</label><input class="ti" type="text" name="txtGC" maxlength="26" value="%TXT_GC%"></div>
        <div class="field"><label>Fin — ganó blanco</label><input class="ti" type="text" name="txtGB" maxlength="26" value="%TXT_GB%"></div>
        <div class="field"><label>Fin — empate</label><input class="ti" type="text" name="txtEmp" maxlength="26" value="%TXT_EMP%"></div>
        <div class="field"><label>Anuncio próximo torneo (prefijo)</label><input class="ti" type="text" name="txtPrep" maxlength="18" value="%TXT_PREP%"></div>
      </div>
    </div>
  </div>

  <div class="card cc">
    <h2>🎙 Comentarista</h2>
    <div class="field">
      <label>Intervalo mín. seg <b id="icn">%INTERV_COM_MIN%</b></label>
      <input type="range" name="intervaloComentariosMin" min="5" max="120" value="%INTERV_COM_MIN%" oninput="sl(this,'icn')">
    </div>
    <div class="field">
      <label>Intervalo máx. seg <b id="icx">%INTERV_COM_MAX%</b></label>
      <input type="range" name="intervaloComentariosMax" min="5" max="120" value="%INTERV_COM_MAX%" oninput="sl(this,'icx')">
    </div>
    <div class="field">
      <label>Stats serial seg <b id="ist">%INTERV_STATS%</b></label>
      <input type="range" name="intervaloStats" min="3" max="30" value="%INTERV_STATS%" oninput="sl(this,'ist')">
    </div>
    <div class="field">
      <label>Diff goles → goleada <b id="gd">%GOLEADA_DIFF%</b></label>
      <input type="range" name="goleadaDiff" min="2" max="6" value="%GOLEADA_DIFF%" oninput="sl(this,'gd')">
    </div>
    <div class="field">
      <label>Goles → caliente <b id="cg">%CALIENTE_GOL%</b></label>
      <input type="range" name="calienteGoles" min="2" max="10" value="%CALIENTE_GOL%" oninput="sl(this,'cg')">
    </div>
    <div class="field">
      <label>Duración primeros min seg <b id="pms">%PRIM_MINS_SEGS%</b></label>
      <input type="range" name="primerosMinsSegs" min="10" max="30" value="%PRIM_MINS_SEGS%" oninput="sl(this,'pms')">
    </div>
    <div class="field">
      <label>Sin goles → aburrido seg <b id="uas">%UMBRAL_ABUR%</b></label>
      <input type="range" name="umbralAburridoSegs" min="30" max="600" value="%UMBRAL_ABUR%" oninput="sl(this,'uas')">
    </div>
    <div class="field">
      <label>Último tramo seg (tiempo) <b id="uts">%ULTI_TRAMO%</b></label>
      <input type="range" name="ultimoTramoSegs" min="10" max="120" value="%ULTI_TRAMO%" oninput="sl(this,'uts')">
    </div>
  </div>

  <div class="card cr" style="grid-column:1/-1">
    <h2>🎵 Rangos de audio — pistas MP3</h2>
    <div class="rng-grid">
      <div>
        <p class="sec-lbl" style="color:var(--accent);padding-top:0">Comentarios</p>
        <table class="rt">
          <thead><tr><th>Estado</th><th>Desde</th><th>Hasta</th></tr></thead>
          <tbody>
            <tr><td><span class="rl"><span class="rd"></span>inicio</span></td><td><input class="ni" type="number" name="cInD" min="0" max="255" value="%C_IN_D%"></td><td><input class="ni" type="number" name="cInH" min="0" max="255" value="%C_IN_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>primeros min.</span></td><td><input class="ni" type="number" name="cPrD" min="0" max="255" value="%C_PR_D%"></td><td><input class="ni" type="number" name="cPrH" min="0" max="255" value="%C_PR_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>parejo</span></td><td><input class="ni" type="number" name="cPaD" min="0" max="255" value="%C_PA_D%"></td><td><input class="ni" type="number" name="cPaH" min="0" max="255" value="%C_PA_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>caliente</span></td><td><input class="ni" type="number" name="cCaD" min="0" max="255" value="%C_CA_D%"></td><td><input class="ni" type="number" name="cCaH" min="0" max="255" value="%C_CA_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>goleada</span></td><td><input class="ni" type="number" name="cGoD" min="0" max="255" value="%C_GO_D%"></td><td><input class="ni" type="number" name="cGoH" min="0" max="255" value="%C_GO_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>definido</span></td><td><input class="ni" type="number" name="cDeD" min="0" max="255" value="%C_DE_D%"></td><td><input class="ni" type="number" name="cDeH" min="0" max="255" value="%C_DE_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>ú.tramo empate c/goles</span></td><td><input class="ni" type="number" name="cUtEmpD" min="0" max="255" value="%C_UTEMP_D%"></td><td><input class="ni" type="number" name="cUtEmpH" min="0" max="255" value="%C_UTEMP_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>ú.tramo goleada</span></td><td><input class="ni" type="number" name="cUtGolD" min="0" max="255" value="%C_UTGOL_D%"></td><td><input class="ni" type="number" name="cUtGolH" min="0" max="255" value="%C_UTGOL_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>ú.tramo ajustado</span></td><td><input class="ni" type="number" name="cUtAjD" min="0" max="255" value="%C_UTAJ_D%"></td><td><input class="ni" type="number" name="cUtAjH" min="0" max="255" value="%C_UTAJ_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>ú.tramo aburrido</span></td><td><input class="ni" type="number" name="cUtAbD" min="0" max="255" value="%C_UTAB_D%"></td><td><input class="ni" type="number" name="cUtAbH" min="0" max="255" value="%C_UTAB_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd" style="opacity:.35"></span><span style="opacity:.45">último tramo (gen.)</span></span></td><td><input class="ni" type="number" name="cUtGenD" min="0" max="255" value="%C_UTGEN_D%"></td><td><input class="ni" type="number" name="cUtGenH" min="0" max="255" value="%C_UTGEN_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>aburrido</span></td><td><input class="ni" type="number" name="cAbD" min="0" max="255" value="%C_AB_D%"></td><td><input class="ni" type="number" name="cAbH" min="0" max="255" value="%C_AB_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd"></span>tranquilo</span></td><td><input class="ni" type="number" name="cTrD" min="0" max="255" value="%C_TR_D%"></td><td><input class="ni" type="number" name="cTrH" min="0" max="255" value="%C_TR_H%"></td></tr>
          </tbody>
        </table>
      </div>
      <div>
        <p class="sec-lbl" style="color:var(--pink);padding-top:0">Goles ⚽</p>
        <table class="rt">
          <thead><tr><th>Tipo</th><th>Desde</th><th>Hasta</th></tr></thead>
          <tbody>
            <tr><td><span class="rl"><span class="rd g"></span>normal</span></td><td><input class="ni" type="number" name="gNorD" min="0" max="255" value="%G_NOR_D%"></td><td><input class="ni" type="number" name="gNorH" min="0" max="255" value="%G_NOR_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd g"></span>efusivo</span></td><td><input class="ni" type="number" name="gEfD" min="0" max="255" value="%G_EF_D%"></td><td><input class="ni" type="number" name="gEfH" min="0" max="255" value="%G_EF_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd g"></span>empate</span></td><td><input class="ni" type="number" name="gEmD" min="0" max="255" value="%G_EM_D%"></td><td><input class="ni" type="number" name="gEmH" min="0" max="255" value="%G_EM_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd g"></span>caliente</span></td><td><input class="ni" type="number" name="gCaD" min="0" max="255" value="%G_CA_D%"></td><td><input class="ni" type="number" name="gCaH" min="0" max="255" value="%G_CA_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd g"></span>agónico</span></td><td><input class="ni" type="number" name="gAgD" min="0" max="255" value="%G_AG_D%"></td><td><input class="ni" type="number" name="gAgH" min="0" max="255" value="%G_AG_H%"></td></tr>
            <tr><td><span class="rl"><span class="rd g"></span>ag. empate</span></td><td><input class="ni" type="number" name="gAeD" min="0" max="255" value="%G_AE_D%"></td><td><input class="ni" type="number" name="gAeH" min="0" max="255" value="%G_AE_H%"></td></tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>

  <div class="card cr" style="grid-column:1/-1">
    <h2>🔔 Pitidos &amp; Finales &amp; SP2</h2>
    <div class="rng-grid">
      <div>
        <p class="sec-lbl" style="color:var(--accent);padding-top:0">Pitidos</p>
        <table class="rt">
          <thead><tr><th>Tipo</th><th>Desde</th><th>Hasta</th></tr></thead>
          <tbody>
            <tr><td><span class="rl">inicio</span></td><td><input class="ni" type="number" name="pItD" min="0" max="255" value="%P_IT_D%"></td><td><input class="ni" type="number" name="pItH" min="0" max="255" value="%P_IT_H%"></td></tr>
            <tr><td><span class="rl">final</span></td><td><input class="ni" type="number" name="pFiD" min="0" max="255" value="%P_FI_D%"></td><td><input class="ni" type="number" name="pFiH" min="0" max="255" value="%P_FI_H%"></td></tr>
          </tbody>
        </table>
        <p class="sec-lbl" style="color:var(--pink);padding-top:12px">Final del partido</p>
        <table class="rt">
          <thead><tr><th>Resultado</th><th>Desde</th><th>Hasta</th></tr></thead>
          <tbody>
            <tr><td><span class="rl">empate</span></td><td><input class="ni" type="number" name="fEmD" min="0" max="255" value="%F_EM_D%"></td><td><input class="ni" type="number" name="fEmH" min="0" max="255" value="%F_EM_H%"></td></tr>
            <tr><td><span class="rl">aplastante</span></td><td><input class="ni" type="number" name="fApD" min="0" max="255" value="%F_AP_D%"></td><td><input class="ni" type="number" name="fApH" min="0" max="255" value="%F_AP_H%"></td></tr>
            <tr><td><span class="rl">ajustada</span></td><td><input class="ni" type="number" name="fAjD" min="0" max="255" value="%F_AJ_D%"></td><td><input class="ni" type="number" name="fAjH" min="0" max="255" value="%F_AJ_H%"></td></tr>
            <tr><td><span class="rl">normal</span></td><td><input class="ni" type="number" name="fNoD" min="0" max="255" value="%F_NO_D%"></td><td><input class="ni" type="number" name="fNoH" min="0" max="255" value="%F_NO_H%"></td></tr>
          </tbody>
        </table>
      </div>
      <div>
        <p class="sec-lbl" style="color:#4caf50;padding-top:0">SP2 — Ambiente (pistas 1–17)</p>
        <p style="font-size:.72rem;color:var(--muted);margin-bottom:10px;line-height:1.5">
          Ambiente genérico suena la mayor parte del partido.<br>
          Hinchada suena <b style="color:var(--text)">1 sola vez</b> por partido (tras el gol #<b id="hg" style="color:var(--text)">%HINCH_GOL%</b>).<br>
          Caliente suena <b style="color:var(--text)">máx. 2 veces</b>, solo si el partido está caliente.<br>
          Reacción gol suena instantáneo en cada gol.
        </p>
        <div class="field">
          <label>Hinchada después del gol # <b id="hgb">%HINCH_GOL%</b></label>
          <input type="range" name="hincGol" min="1" max="10" value="%HINCH_GOL%" oninput="sl(this,'hgb');document.getElementById('hg').textContent=this.value">
        </div>
        <table class="rt">
          <thead><tr><th>Tipo</th><th>Desde</th><th>Hasta</th></tr></thead>
          <tbody>
            <tr><td><span class="rl">genérico (normal)</span></td><td><input class="ni" type="number" name="aGeD" min="1" max="255" value="%A_GE_D%"></td><td><input class="ni" type="number" name="aGeH" min="1" max="255" value="%A_GE_H%"></td></tr>
            <tr><td><span class="rl">hinchada (1x)</span></td><td><input class="ni" type="number" name="hMuD" min="1" max="255" value="%H_MU_D%"></td><td><input class="ni" type="number" name="hMuH" min="1" max="255" value="%H_MU_H%"></td></tr>
            <tr><td><span class="rl">caliente (máx 2x)</span></td><td><input class="ni" type="number" name="mCaD" min="1" max="255" value="%M_CA_D%"></td><td><input class="ni" type="number" name="mCaH" min="1" max="255" value="%M_CA_H%"></td></tr>
            <tr><td><span class="rl">reacción gol</span></td><td><input class="ni" type="number" name="aGoD" min="1" max="255" value="%A_GO_D%"></td><td><input class="ni" type="number" name="aGoH" min="1" max="255" value="%A_GO_H%"></td></tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>

</div>
<div class="save-bar">
  <button type="submit" class="btn-save">GUARDAR CONFIGURACIÓN</button>
</div>
</form>
<button id="fab-save" onclick="document.getElementById('cfg').requestSubmit()">💾 GUARDAR</button>

<div class="grid" style="margin-top:14px">
  <div class="card cw" id="wifi-card">
    <h2>📶 WiFi</h2>
    <div id="wifi-conn-status" style="display:none;background:rgba(105,240,174,.06);border:1px solid rgba(105,240,174,.2);border-radius:10px;padding:10px 14px;margin-bottom:14px;text-align:center">
      <p style="font-size:.82rem;color:var(--muted)">Conectado a <b id="ssid-label" style="color:var(--green)"></b></p>
      <a href="http://metegol.local" target="_blank" style="font-size:.78rem;color:var(--accent)">🌐 metegol.local</a>
    </div>
    <div id="saved-nets" style="margin-bottom:14px"></div>
    <p class="sec-lbl" style="color:var(--green)">Agregar red WiFi</p>
    <div class="field">
      <label>SSID</label>
      <input type="text" id="wSSID" placeholder="Nombre de la red" style="width:100%;padding:9px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.88rem;outline:none">
    </div>
    <div class="field">
      <label>Contraseña</label>
      <input type="password" id="wPass" placeholder="••••••" style="width:100%;padding:9px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.88rem;outline:none">
    </div>
    <button type="button" onclick="agregarRed()" style="width:100%;padding:11px;background:linear-gradient(90deg,var(--purple),#651fff);border:none;border-radius:10px;color:#fff;font-weight:700;font-size:.88rem;cursor:pointer">GUARDAR RED</button>
    <div id="wifi-msg" style="font-size:.78rem;color:var(--muted);margin-top:10px;text-align:center;min-height:18px"></div>
  </div>
</div>
</section>
</div>

<div id="toast">✓ Guardado</div>

<script>
  function sl(el,id){
    document.getElementById(id).textContent=el.value;
    el.style.setProperty('--p',((el.value-el.min)/(el.max-el.min)*100)+'%');
  }
  document.querySelectorAll('input[type=range]').forEach(el=>{
    el.style.setProperty('--p',((el.value-el.min)/(el.max-el.min)*100)+'%');
  });
  function setModo(v){
    document.getElementById('modoJuego').value=v;
    document.querySelectorAll('.toggle-btn').forEach((b,i)=>b.classList.toggle('active',i===v));
    document.getElementById('row-goles').style.display=v===0?'':'none';
    document.getElementById('row-tiempo').style.display=v===1?'':'none';
  }
  setModo(parseInt(document.getElementById('modoJuego').value));
  function actualizarEstadoWiFi(){
    fetch('/wifiStatus').then(r=>r.json()).then(d=>{
      const cs=document.getElementById('wifi-conn-status');
      cs.style.display=d.connected?'block':'none';
      if(d.connected)document.getElementById('ssid-label').textContent=d.ssid;
      const sn=document.getElementById('saved-nets');
      if(d.saved&&d.saved.length>0){
        sn.innerHTML='<p class="sec-lbl" style="color:var(--muted)">Redes guardadas ('+d.saved.length+'/3)</p>'+
          d.saved.map((s,i)=>`<div style="display:flex;align-items:center;justify-content:space-between;padding:7px 0;border-bottom:1px solid var(--border)">
            <span style="font-size:.84rem;color:${s===d.ssid?'var(--green)':'var(--text)'}">
              ${s===d.ssid?'✓ ':''}<b>${s}</b>
            </span>
            <button onclick="eliminarRed(${i})" style="background:transparent;border:1px solid #f44336;color:#f44336;padding:3px 10px;border-radius:12px;font-size:.72rem;cursor:pointer">✕ borrar</button>
          </div>`).join('');
      } else {
        sn.innerHTML='';
      }
    }).catch(()=>{});
  }
  function agregarRed(){
    const ssid=document.getElementById('wSSID').value.trim();
    const pass=document.getElementById('wPass').value;
    const msg=document.getElementById('wifi-msg');
    if(!ssid){msg.style.color='var(--pink)';msg.textContent='Ingresá el SSID';return;}
    msg.style.color='var(--accent)';msg.textContent='Guardando...';
    fetch('/wifi',{method:'POST',body:new URLSearchParams({ssid,pass})}).then(r=>r.json()).then(()=>{
      document.getElementById('wSSID').value='';document.getElementById('wPass').value='';
      actualizarEstadoWiFi();
      let n=0;const t=setInterval(()=>{
        n++;msg.textContent='Conectando'+'.'.repeat(n%4);
        fetch('/wifiStatus').then(r=>r.json()).then(d=>{
          if(d.connected&&d.ssid===ssid){clearInterval(t);msg.style.color='var(--green)';msg.textContent='✓ Conectado a '+ssid;actualizarEstadoWiFi();}
          else if(n>13){clearInterval(t);msg.style.color='var(--muted)';msg.textContent='Red guardada (no en rango ahora)';}
        }).catch(()=>{});
      },1500);
    }).catch(()=>{msg.style.color='var(--pink)';msg.textContent='Error';});
  }
  function eliminarRed(idx){
    if(!confirm('¿Borrar esta red WiFi guardada?'))return;
    fetch('/wifi-delete',{method:'POST',body:new URLSearchParams({idx})}).then(r=>r.json()).then(d=>{
      if(d.ok)actualizarEstadoWiFi();
    }).catch(()=>{});
  }
  actualizarEstadoWiFi();setInterval(actualizarEstadoWiFi,5000);
  const EC={
    inicio:'Inicio',primeros_min:'Primeros min.',parejo:'Parejo',
    caliente:'Caliente 🔥',goleada:'Goleada 💥',definido:'Definido',
    ultimo_tramo:'Último tramo ⚡',aburrido:'Aburrido 😴',tranquilo:'Tranquilo',
    en_espera:'En espera',terminado:'Finalizado',pausado:'Pausado ⏸'
  };
  const ECC={
    parejo:'#7c4dff',caliente:'#ff9100',goleada:'#f44336',
    ultimo_tramo:'#ff4081',aburrido:'#607d8b',tranquilo:'#546e7a',
    definido:'#0097a7',inicio:'#00e5ff',primeros_min:'#00b8d4'
  };
  function actualizarMarcador(){
    fetch('/estado').then(r=>r.json()).then(d=>{
      window._partidoTerminado = !!d.terminado;
      const g=d.goles||[0,0];
      const lbl=document.getElementById('partido-label');
      const pm=document.getElementById('pmeta');
      const gw=document.getElementById('ganador-wrap');
      const bs=document.getElementById('btn-start');
      const bst=document.getElementById('btn-stop');
      document.getElementById('sc-c').textContent=g[0];
      document.getElementById('sc-b').textContent=g[1];
      if(d.activo){
        lbl.textContent='Partido en curso';
        pm.style.display='flex';gw.style.display='none';
        bs.textContent='Reiniciar';bst.style.display='inline-block';
        document.getElementById('tiempo-juego').textContent=d.tiempoJuego||'00:00';
        const badge=document.getElementById('estado-badge');
        badge.textContent=EC[d.estado]||d.estado;
        badge.style.color=ECC[d.estado]||'var(--accent)';
        const tw=document.getElementById('timer-wrap');
        if(d.modo===1&&d.tiempoRestante>0){
          tw.style.display='block';
          const s=d.tiempoRestante;
          document.getElementById('timer').textContent=String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
        }else{tw.style.display='none';}
      }else if(d.pausado){
        lbl.textContent='Partido pausado ⏸';
        pm.style.display='none';gw.style.display='none';
        bs.textContent='Reanudar';bst.style.display='inline-block';
      }else if(d.terminado){
        lbl.textContent='Partido finalizado';
        pm.style.display='none';
        gw.style.display='block';
        gw.textContent=g[0]>g[1]?'🏆 Ganó Celeste!':g[1]>g[0]?'🏆 Ganó Blanco!':'🤝 Empate!';
        bs.textContent='Nuevo partido';bst.style.display='none';
      }else{
        lbl.textContent='En espera';
        pm.style.display='none';gw.style.display='none';
        bs.textContent='Iniciar partido';bst.style.display='none';
        document.getElementById('sc-c').textContent='0';
        document.getElementById('sc-b').textContent='0';
      }
    }).catch(()=>{});
  }
  function iniciarPartido(){fetch('/start',{method:'POST'}).then(()=>actualizarMarcador()).catch(()=>{});}
  function pararPartido(){fetch('/stop',{method:'POST'}).then(()=>actualizarMarcador()).catch(()=>{});}
  actualizarMarcador();setInterval(actualizarMarcador,3000);
  document.getElementById('cfg').addEventListener('submit',function(e){
    e.preventDefault();
    fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(this))}).then(r=>r.json()).then(j=>{
      if(j.ok){const t=document.getElementById('toast');t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2500);}
    }).catch(()=>{});
  });

  function showTab(name){
    ['partido','torneo','ajustes'].forEach(t=>{
      document.getElementById('tab-'+t).style.display = t===name ? '' : 'none';
      document.getElementById('tabbtn-'+t).classList.toggle('active', t===name);
    });
    if(name==='torneo') actualizarTorneo();
  }

  function torneoRenderNombres(n){
    const wrap=document.getElementById('torneo-nombres');
    const prevVals=Array.from(wrap.querySelectorAll('input')).map(i=>i.value);
    wrap.innerHTML='';
    for(let i=0;i<n;i++){
      const inp=document.createElement('input');
      inp.type='text'; inp.placeholder='Jugador '+(i+1); inp.value=prevVals[i]||'';
      wrap.appendChild(inp);
    }
  }
  function torneoCantidadCambio(v){
    document.getElementById('tn').textContent=v;
    const n=parseInt(v);
    const gSlider=document.getElementById('tGrupos');
    gSlider.max=Math.max(1,Math.min(4,Math.floor(n/2)));
    if(parseInt(gSlider.value)>parseInt(gSlider.max))gSlider.value=gSlider.max;
    document.getElementById('tg').textContent=gSlider.value;
    torneoRenderNombres(n);
  }
  torneoRenderNombres(4);

  function crearTorneo(){
    const n=parseInt(document.getElementById('tCant').value);
    const nGrupos=parseInt(document.getElementById('tGrupos').value);
    const nombres=Array.from(document.querySelectorAll('#torneo-nombres input')).map(i=>i.value.trim());
    const body=new URLSearchParams({n,nGrupos});
    nombres.forEach((nm,i)=>body.append('nombre'+i,nm));
    fetch('/torneo/crear',{method:'POST',body}).then(r=>r.json()).then(j=>{
      if(j.ok){actualizarTorneo();return;}
      if(j.error==='partido_en_curso')alert('Hay un partido individual en curso — cancelalo antes de armar un torneo.');
      else if(j.error==='torneo_activo')alert('Ya hay un torneo en curso — cancelalo antes de armar uno nuevo.');
      else alert('No se pudo armar el torneo.');
    });
  }
  function jugarProximo(){
    fetch('/torneo/jugarProximo',{method:'POST'}).then(r=>r.json()).then(j=>{
      if(j.ok) fetch('/start',{method:'POST'}).then(()=>{showTab('partido');actualizarTorneo();});
    });
  }
  function confirmarTorneo(){
    fetch('/torneo/confirmar',{method:'POST'}).then(r=>r.json()).then(()=>actualizarTorneo());
  }
  function cancelarTorneo(){
    if(!confirm('¿Cancelar el torneo en curso?'))return;
    fetch('/torneo/cancelar',{method:'POST'}).then(()=>actualizarTorneo());
  }

  function actualizarTorneo(){
    fetch('/torneo/estado').then(r=>r.json()).then(d=>{
      document.getElementById('torneo-setup-card').style.display=d.activo?'none':'';
      document.getElementById('torneo-activo').style.display=d.activo?'':'none';
      if(!d.activo)return;

      document.getElementById('torneo-campeon-card').style.display=d.fase===2?'':'none';
      if(d.fase===2)document.getElementById('torneo-campeon-nombre').textContent=d.campeon||'';
      document.getElementById('torneo-ko-card').style.display=d.fase>=1?'':'none';
      document.getElementById('torneo-grupos-card').style.display=d.fase===0?'':'none';

      const ej=document.getElementById('torneo-en-juego');
      const btnConf=document.getElementById('btn-confirmar-torneo');
      const pc=document.getElementById('torneo-proximo-card');
      if(d.partidoEnJuego>=0){
        ej.style.display='';
        pc.style.display='none';
        const lista=d.enJuegoEsKO?d.partidosKO:d.partidosGrupo;
        const m=lista.find(x=>x.idx===d.partidoEnJuego);
        document.getElementById('en-juego-txt').innerHTML=m?('En juego: <b>'+m.pA+'</b> vs <b>'+(m.pB||'???')+'</b>'):'';
        btnConf.style.display=(window._partidoTerminado===true)?'inline-block':'none';
      }else{
        ej.style.display='none';
        if(d.proximo){
          pc.style.display='';
          document.getElementById('proximo-txt').textContent=d.proximo.pA+' vs '+d.proximo.pB;
          document.getElementById('despues-txt').textContent=d.despues?('Después: '+d.despues.pA+' vs '+d.despues.pB):'';
        }else{
          pc.style.display='none';
        }
      }

      const gWrap=document.getElementById('torneo-grupos');
      let gHtml='';
      d.grupos.forEach((g,gi)=>{
        gHtml+='<p class="grupo-titulo">Grupo '+String.fromCharCode(65+gi)+'</p>';
        gHtml+='<table class="rt"><thead><tr><th>Jugador</th><th>PJ</th><th>PG</th><th>PE</th><th>PP</th><th>DIF</th><th>Pts</th></tr></thead><tbody>';
        g.tabla.forEach(f=>{
          gHtml+='<tr><td>'+f.nombre+'</td><td>'+f.pj+'</td><td>'+f.pg+'</td><td>'+f.pe+'</td><td>'+f.pp+'</td><td>'+f.dif+'</td><td><b>'+f.pts+'</b></td></tr>';
        });
        gHtml+='</tbody></table>';
      });
      gHtml+='<p class="grupo-titulo">Partidos pendientes</p><div class="partidos-lista">';
      const pendientes=d.partidosGrupo.filter(m=>!m.jugado);
      if(pendientes.length===0)gHtml+='<p class="empty-hint">Fase de grupos completa</p>';
      pendientes.forEach(m=>{
        gHtml+='<div class="partido-row"><span>'+m.pA+'</span><span class="vs">vs</span><span>'+m.pB+'</span></div>';
      });
      gHtml+='</div><p class="grupo-titulo">Jugados</p><div class="partidos-lista">';
      const jugados=d.partidosGrupo.filter(m=>m.jugado);
      if(jugados.length===0)gHtml+='<p class="empty-hint">Todavía no se jugó ninguno</p>';
      jugados.forEach(m=>{
        gHtml+='<div class="partido-row"><span>'+m.pA+'</span><span class="res">'+m.golA+' - '+m.golB+'</span><span>'+m.pB+'</span></div>';
      });
      gHtml+='</div>';
      gWrap.innerHTML=gHtml;

      const koWrap=document.getElementById('torneo-ko');
      let koHtml='';
      const rondas=[...new Set(d.partidosKO.map(m=>m.ronda))].sort((a,b)=>a-b);
      rondas.forEach(r=>{
        const nombreRonda=(r===rondas[rondas.length-1])?'Final':('Ronda '+(r+1));
        koHtml+='<p class="ronda-titulo">'+nombreRonda+'</p><div class="partidos-lista">';
        d.partidosKO.filter(m=>m.ronda===r).forEach(m=>{
          if(m.pB===null){
            koHtml+='<div class="partido-row"><span>'+m.pA+'</span><span class="vs">pasa de ronda (bye)</span><span></span></div>';
          }else if(m.jugado){
            koHtml+='<div class="partido-row"><span>'+m.pA+'</span><span class="res">'+m.golA+' - '+m.golB+'</span><span>'+m.pB+'</span></div>';
          }else{
            koHtml+='<div class="partido-row"><span>'+m.pA+'</span><span class="vs">vs</span><span>'+m.pB+'</span></div>';
          }
        });
        koHtml+='</div>';
      });
      koWrap.innerHTML=koHtml||'<p class="empty-hint">Todavía no hay cuadro de eliminación</p>';
    }).catch(()=>{});
  }
  actualizarTorneo();setInterval(actualizarTorneo,3000);
</script>
</body>
</html>
)rawhtml";

// ── Handlers HTTP ─────────────────────────────────────────────────────────────

static String buildPage() {
    String html = FPSTR(HTML);
    html.replace("%VOL_VOZ%",    String(config.volumenVoz));
    html.replace("%VOL_AMB%",    String(config.volumenAmbiente));
    html.replace("%PISTA_AMB%",  String(config.pistaAmbiente));
    html.replace("%MODO%",       String(config.modoJuego));
    html.replace("%MODO_GOLES_ACTIVE%", config.modoJuego == 0 ? "active" : "");
    html.replace("%MODO_TIEMPO_ACTIVE%", config.modoJuego == 1 ? "active" : "");
    html.replace("%GOLES_MAX%",    String(config.golesMax));
    html.replace("%DUR_MIN%",      String(config.duracionMin));
    html.replace("%INTERV_DISP%",  String(config.intervaloDisplay));
    html.replace("%BRILLO%",     String(config.brillo));
    html.replace("%VEL_SCROLL%", String(config.velocidadScroll));

    html.replace("%TXT_BOOT%", config.textoBoot);
    html.replace("%TXT_ARR%",  config.textoArranca);
    html.replace("%TXT_PAU%",  config.textoPausa);
    html.replace("%TXT_REA%",  config.textoReanuda);
    html.replace("%TXT_CAN%",  config.textoCancelado);
    html.replace("%TXT_GOL%",  config.textoGol);
    html.replace("%TXT_GC%",   config.textoGanadorCeleste);
    html.replace("%TXT_GB%",   config.textoGanadorBlanco);
    html.replace("%TXT_EMP%",  config.textoEmpate);
    html.replace("%TXT_PREP%", config.textoPreparense);
    // Comentarista — thresholds
    html.replace("%INTERV_COM_MIN%", String(config.intervaloComentariosMin));
    html.replace("%INTERV_COM_MAX%", String(config.intervaloComentariosMax));
    html.replace("%INTERV_STATS%",   String(config.intervaloStats));
    html.replace("%GOLEADA_DIFF%",String(config.goleadaDiff));
    html.replace("%CALIENTE_GOL%",String(config.calienteGoles));
    html.replace("%HINCH_GOL%",    String(config.hinchadaGol));
    html.replace("%PRIM_MINS_SEGS%", String(config.primerosMinsSegs));
    html.replace("%ULTI_TRAMO%",     String(config.ultimoTramoSegs));

    html.replace("%UMBRAL_ABUR%",    String(config.umbralAburridoSegs));
    // Comentarista — rangos estado
    html.replace("%C_IN_D%", String(config.comentInicio.desde));
    html.replace("%C_IN_H%", String(config.comentInicio.hasta));
    html.replace("%C_PR_D%", String(config.comentPrimerosMins.desde));
    html.replace("%C_PR_H%", String(config.comentPrimerosMins.hasta));
    html.replace("%C_PA_D%", String(config.comentParejo.desde));
    html.replace("%C_PA_H%", String(config.comentParejo.hasta));
    html.replace("%C_CA_D%", String(config.comentCaliente.desde));
    html.replace("%C_CA_H%", String(config.comentCaliente.hasta));
    html.replace("%C_GO_D%", String(config.comentGoleada.desde));
    html.replace("%C_GO_H%", String(config.comentGoleada.hasta));
    html.replace("%C_DE_D%", String(config.comentDefinido.desde));
    html.replace("%C_DE_H%", String(config.comentDefinido.hasta));
    html.replace("%C_UTEMP_D%", String(config.comentUltimoTramoEmpateGoles.desde));
    html.replace("%C_UTEMP_H%", String(config.comentUltimoTramoEmpateGoles.hasta));
    html.replace("%C_UTGOL_D%", String(config.comentUltimoTramoGoleada.desde));
    html.replace("%C_UTGOL_H%", String(config.comentUltimoTramoGoleada.hasta));
    html.replace("%C_UTAJ_D%",  String(config.comentUltimoTramoAjustado.desde));
    html.replace("%C_UTAJ_H%",  String(config.comentUltimoTramoAjustado.hasta));
    html.replace("%C_UTAB_D%",  String(config.comentUltimoTramoAburrido.desde));
    html.replace("%C_UTAB_H%",  String(config.comentUltimoTramoAburrido.hasta));
    html.replace("%C_UTGEN_D%", String(config.comentUltimoTramoGeneral.desde));
    html.replace("%C_UTGEN_H%", String(config.comentUltimoTramoGeneral.hasta));
    html.replace("%C_AB_D%", String(config.comentAburrido.desde));
    html.replace("%C_AB_H%", String(config.comentAburrido.hasta));
    html.replace("%C_TR_D%", String(config.comentTranquilo.desde));
    html.replace("%C_TR_H%", String(config.comentTranquilo.hasta));
    // Goles — rangos contextuales
    html.replace("%G_NOR_D%", String(config.golNormal.desde));
    html.replace("%G_NOR_H%", String(config.golNormal.hasta));
    html.replace("%G_EF_D%",  String(config.golEfusivo.desde));
    html.replace("%G_EF_H%",  String(config.golEfusivo.hasta));
    html.replace("%G_EM_D%",  String(config.golEmpate.desde));
    html.replace("%G_EM_H%",  String(config.golEmpate.hasta));
    html.replace("%G_CA_D%",  String(config.golCaliente.desde));
    html.replace("%G_CA_H%",  String(config.golCaliente.hasta));
    html.replace("%G_AG_D%",  String(config.golAgonico.desde));
    html.replace("%G_AG_H%",  String(config.golAgonico.hasta));
    html.replace("%G_AE_D%",  String(config.golAgonicoEmpate.desde));
    html.replace("%G_AE_H%",  String(config.golAgonicoEmpate.hasta));
    // Pitidos
    html.replace("%P_IT_D%",  String(config.pitidoInicio.desde));
    html.replace("%P_IT_H%",  String(config.pitidoInicio.hasta));
    html.replace("%P_FI_D%",  String(config.pitidoFinal.desde));
    html.replace("%P_FI_H%",  String(config.pitidoFinal.hasta));
    // Finales
    html.replace("%F_EM_D%",  String(config.finalEmpate.desde));
    html.replace("%F_EM_H%",  String(config.finalEmpate.hasta));
    html.replace("%F_AP_D%",  String(config.finalAplastante.desde));
    html.replace("%F_AP_H%",  String(config.finalAplastante.hasta));
    html.replace("%F_AJ_D%",  String(config.finalAjustada.desde));
    html.replace("%F_AJ_H%",  String(config.finalAjustada.hasta));
    html.replace("%F_NO_D%",  String(config.finalNormal.desde));
    html.replace("%F_NO_H%",  String(config.finalNormal.hasta));
    // SP2 ambiente
    html.replace("%A_GE_D%",  String(config.ambienteGenerico.desde));
    html.replace("%A_GE_H%",  String(config.ambienteGenerico.hasta));
    html.replace("%H_MU_D%",  String(config.hinchadaMusica.desde));
    html.replace("%H_MU_H%",  String(config.hinchadaMusica.hasta));
    html.replace("%M_CA_D%",  String(config.momentoCaliente.desde));
    html.replace("%M_CA_H%",  String(config.momentoCaliente.hasta));
    html.replace("%A_GO_D%",  String(config.ambienteGol.desde));
    html.replace("%A_GO_H%",  String(config.ambienteGol.hasta));
    return html;
}

static void handleRoot() {
    Serial.printf("\n[WEB] GET / — %s\n", server.client().remoteIP().toString().c_str());
    server.send(200, "text/html", buildPage());
}

static void handleSave() {
    Serial.printf("\n[WEB] POST /save — %s\n", server.client().remoteIP().toString().c_str());
    if (server.hasArg("volumenVoz"))      config.volumenVoz      = server.arg("volumenVoz").toInt();
    if (server.hasArg("volumenAmbiente")) config.volumenAmbiente = server.arg("volumenAmbiente").toInt();
    if (server.hasArg("modoJuego"))       config.modoJuego       = server.arg("modoJuego").toInt();
    if (server.hasArg("golesMax"))        config.golesMax        = constrain(server.arg("golesMax").toInt(), 4, 10);
    if (server.hasArg("duracionMin"))     config.duracionMin     = constrain(server.arg("duracionMin").toInt(), 3, 8);
    if (server.hasArg("brillo"))           config.brillo           = server.arg("brillo").toInt();
    if (server.hasArg("velocidadScroll"))  config.velocidadScroll  = server.arg("velocidadScroll").toInt();
    if (server.hasArg("intervaloDisplay")) config.intervaloDisplay = constrain(server.arg("intervaloDisplay").toInt(), 2, 30);
    if (server.hasArg("pistaAmbiente"))    config.pistaAmbiente    = server.arg("pistaAmbiente").toInt();
    // Display — textos customizables
    if (server.hasArg("txtBoot")) strlcpy(config.textoBoot,           server.arg("txtBoot").c_str(), sizeof(config.textoBoot));
    if (server.hasArg("txtArr"))  strlcpy(config.textoArranca,        server.arg("txtArr").c_str(),  sizeof(config.textoArranca));
    if (server.hasArg("txtPau"))  strlcpy(config.textoPausa,          server.arg("txtPau").c_str(),  sizeof(config.textoPausa));
    if (server.hasArg("txtRea"))  strlcpy(config.textoReanuda,        server.arg("txtRea").c_str(),  sizeof(config.textoReanuda));
    if (server.hasArg("txtCan"))  strlcpy(config.textoCancelado,      server.arg("txtCan").c_str(),  sizeof(config.textoCancelado));
    if (server.hasArg("txtGol"))  strlcpy(config.textoGol,            server.arg("txtGol").c_str(),  sizeof(config.textoGol));
    if (server.hasArg("txtGC"))   strlcpy(config.textoGanadorCeleste, server.arg("txtGC").c_str(),   sizeof(config.textoGanadorCeleste));
    if (server.hasArg("txtGB"))   strlcpy(config.textoGanadorBlanco,  server.arg("txtGB").c_str(),   sizeof(config.textoGanadorBlanco));
    if (server.hasArg("txtEmp"))  strlcpy(config.textoEmpate,         server.arg("txtEmp").c_str(),  sizeof(config.textoEmpate));
    if (server.hasArg("txtPrep")) strlcpy(config.textoPreparense,     server.arg("txtPrep").c_str(), sizeof(config.textoPreparense));
    // Comentarista — thresholds
    if (server.hasArg("intervaloComentariosMin")) config.intervaloComentariosMin = server.arg("intervaloComentariosMin").toInt();
    if (server.hasArg("intervaloComentariosMax")) config.intervaloComentariosMax = server.arg("intervaloComentariosMax").toInt();
    if (server.hasArg("intervaloStats"))          config.intervaloStats          = constrain(server.arg("intervaloStats").toInt(), 3, 30);
    if (server.hasArg("goleadaDiff"))          config.goleadaDiff          = server.arg("goleadaDiff").toInt();
    if (server.hasArg("calienteGoles"))        config.calienteGoles        = server.arg("calienteGoles").toInt();
    if (server.hasArg("hincGol"))              config.hinchadaGol          = constrain(server.arg("hincGol").toInt(), 1, 10);

    if (server.hasArg("primerosMinsSegs"))     config.primerosMinsSegs     = constrain(server.arg("primerosMinsSegs").toInt(), 10, 30);
    if (server.hasArg("ultimoTramoSegs"))      config.ultimoTramoSegs      = server.arg("ultimoTramoSegs").toInt();
    if (server.hasArg("umbralAburridoSegs"))   config.umbralAburridoSegs   = server.arg("umbralAburridoSegs").toInt();
    // Comentarista — rangos estado
    if (server.hasArg("cInD")) config.comentInicio.desde          = server.arg("cInD").toInt();
    if (server.hasArg("cInH")) config.comentInicio.hasta          = server.arg("cInH").toInt();
    if (server.hasArg("cPrD")) config.comentPrimerosMins.desde    = server.arg("cPrD").toInt();
    if (server.hasArg("cPrH")) config.comentPrimerosMins.hasta    = server.arg("cPrH").toInt();
    if (server.hasArg("cPaD")) config.comentParejo.desde          = server.arg("cPaD").toInt();
    if (server.hasArg("cPaH")) config.comentParejo.hasta          = server.arg("cPaH").toInt();
    if (server.hasArg("cCaD")) config.comentCaliente.desde        = server.arg("cCaD").toInt();
    if (server.hasArg("cCaH")) config.comentCaliente.hasta        = server.arg("cCaH").toInt();
    if (server.hasArg("cGoD")) config.comentGoleada.desde         = server.arg("cGoD").toInt();
    if (server.hasArg("cGoH")) config.comentGoleada.hasta         = server.arg("cGoH").toInt();
    if (server.hasArg("cDeD")) config.comentDefinido.desde        = server.arg("cDeD").toInt();
    if (server.hasArg("cDeH")) config.comentDefinido.hasta        = server.arg("cDeH").toInt();
    if (server.hasArg("cUtEmpD")) config.comentUltimoTramoEmpateGoles.desde = server.arg("cUtEmpD").toInt();
    if (server.hasArg("cUtEmpH")) config.comentUltimoTramoEmpateGoles.hasta = server.arg("cUtEmpH").toInt();
    if (server.hasArg("cUtGolD")) config.comentUltimoTramoGoleada.desde     = server.arg("cUtGolD").toInt();
    if (server.hasArg("cUtGolH")) config.comentUltimoTramoGoleada.hasta     = server.arg("cUtGolH").toInt();
    if (server.hasArg("cUtAjD"))  config.comentUltimoTramoAjustado.desde    = server.arg("cUtAjD").toInt();
    if (server.hasArg("cUtAjH"))  config.comentUltimoTramoAjustado.hasta    = server.arg("cUtAjH").toInt();
    if (server.hasArg("cUtAbD"))  config.comentUltimoTramoAburrido.desde    = server.arg("cUtAbD").toInt();
    if (server.hasArg("cUtAbH"))  config.comentUltimoTramoAburrido.hasta    = server.arg("cUtAbH").toInt();
    if (server.hasArg("cUtGenD")) config.comentUltimoTramoGeneral.desde     = server.arg("cUtGenD").toInt();
    if (server.hasArg("cUtGenH")) config.comentUltimoTramoGeneral.hasta     = server.arg("cUtGenH").toInt();
    if (server.hasArg("cAbD")) config.comentAburrido.desde        = server.arg("cAbD").toInt();
    if (server.hasArg("cAbH")) config.comentAburrido.hasta        = server.arg("cAbH").toInt();
    if (server.hasArg("cTrD")) config.comentTranquilo.desde       = server.arg("cTrD").toInt();
    if (server.hasArg("cTrH")) config.comentTranquilo.hasta       = server.arg("cTrH").toInt();
    // Goles — rangos contextuales
    if (server.hasArg("gNorD")) config.golNormal.desde            = server.arg("gNorD").toInt();
    if (server.hasArg("gNorH")) config.golNormal.hasta            = server.arg("gNorH").toInt();
    if (server.hasArg("gEfD"))  config.golEfusivo.desde           = server.arg("gEfD").toInt();
    if (server.hasArg("gEfH"))  config.golEfusivo.hasta           = server.arg("gEfH").toInt();
    if (server.hasArg("gEmD"))  config.golEmpate.desde            = server.arg("gEmD").toInt();
    if (server.hasArg("gEmH"))  config.golEmpate.hasta            = server.arg("gEmH").toInt();
    if (server.hasArg("gCaD"))  config.golCaliente.desde          = server.arg("gCaD").toInt();
    if (server.hasArg("gCaH"))  config.golCaliente.hasta          = server.arg("gCaH").toInt();
    if (server.hasArg("gAgD"))  config.golAgonico.desde           = server.arg("gAgD").toInt();
    if (server.hasArg("gAgH"))  config.golAgonico.hasta           = server.arg("gAgH").toInt();
    if (server.hasArg("gAeD"))  config.golAgonicoEmpate.desde     = server.arg("gAeD").toInt();
    if (server.hasArg("gAeH"))  config.golAgonicoEmpate.hasta     = server.arg("gAeH").toInt();
    // Pitidos
    if (server.hasArg("pItD"))  config.pitidoInicio.desde         = server.arg("pItD").toInt();
    if (server.hasArg("pItH"))  config.pitidoInicio.hasta         = server.arg("pItH").toInt();
    if (server.hasArg("pFiD"))  config.pitidoFinal.desde          = server.arg("pFiD").toInt();
    if (server.hasArg("pFiH"))  config.pitidoFinal.hasta          = server.arg("pFiH").toInt();
    // Finales
    if (server.hasArg("fEmD"))  config.finalEmpate.desde          = server.arg("fEmD").toInt();
    if (server.hasArg("fEmH"))  config.finalEmpate.hasta          = server.arg("fEmH").toInt();
    if (server.hasArg("fApD"))  config.finalAplastante.desde      = server.arg("fApD").toInt();
    if (server.hasArg("fApH"))  config.finalAplastante.hasta      = server.arg("fApH").toInt();
    if (server.hasArg("fAjD"))  config.finalAjustada.desde        = server.arg("fAjD").toInt();
    if (server.hasArg("fAjH"))  config.finalAjustada.hasta        = server.arg("fAjH").toInt();
    if (server.hasArg("fNoD"))  config.finalNormal.desde          = server.arg("fNoD").toInt();
    if (server.hasArg("fNoH"))  config.finalNormal.hasta          = server.arg("fNoH").toInt();
    // SP2 ambiente — sin restricción de rango (el usuario define cuántas pistas tiene en su SD)
    if (server.hasArg("aGeD"))  config.ambienteGenerico.desde  = server.arg("aGeD").toInt();
    if (server.hasArg("aGeH"))  config.ambienteGenerico.hasta  = server.arg("aGeH").toInt();
    if (server.hasArg("hMuD"))  config.hinchadaMusica.desde    = server.arg("hMuD").toInt();
    if (server.hasArg("hMuH"))  config.hinchadaMusica.hasta    = server.arg("hMuH").toInt();
    if (server.hasArg("mCaD"))  config.momentoCaliente.desde   = server.arg("mCaD").toInt();
    if (server.hasArg("mCaH"))  config.momentoCaliente.hasta   = server.arg("mCaH").toInt();
    if (server.hasArg("aGoD"))  config.ambienteGol.desde       = server.arg("aGoD").toInt();
    if (server.hasArg("aGoH"))  config.ambienteGol.hasta       = server.arg("aGoH").toInt();

    guardarConfig();
    _pendingVolUpdate = true;  // aplica volumen en el próximo loop, no aquí
    Serial.printf("\n[Config] Guardada — SP1 vol:%d SP2 vol:%d modo:%s\n",
        config.volumenVoz, config.volumenAmbiente,
        config.modoJuego == 0 ? "goles" : "tiempo");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleEstado() {
    char buf[200];
    if (_partido) {
        char marcador[16];
        _partido->getResultado(marcador, sizeof(marcador));
        uint32_t elapsed  = millis() - _partido->inicio;
        uint32_t durMs    = (uint32_t)config.duracionMin * 60000UL;
        uint32_t restante = (config.modoJuego == 1 && elapsed < durMs)
                            ? (durMs - elapsed) / 1000 : 0;
        uint32_t mm = elapsed / 60000, ss = (elapsed % 60000) / 1000;
        char tiempo[8]; snprintf(tiempo, sizeof(tiempo), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
        const char* estado = comentaristaGetEstado(*_partido);
        snprintf(buf, sizeof(buf),
            "{\"goles\":[%d,%d],\"marcador\":\"%s\",\"modo\":%d,"
            "\"tiempoRestante\":%lu,\"tiempoJuego\":\"%s\","
            "\"estado\":\"%s\",\"activo\":%s,\"terminado\":%s,\"pausado\":%s}",
            _partido->goles[0], _partido->goles[1], marcador, config.modoJuego,
            (unsigned long)restante, tiempo, estado,
            _partido->activo    ? "true" : "false",
            _partido->terminado ? "true" : "false",
            _partido->pausado   ? "true" : "false");
    } else {
        snprintf(buf, sizeof(buf),
            "{\"goles\":[0,0],\"marcador\":\"0 - 0\",\"modo\":%d,"
            "\"tiempoRestante\":0,\"tiempoJuego\":\"00:00\","
            "\"estado\":\"en_espera\",\"activo\":false,\"terminado\":false,\"pausado\":false}",
            config.modoJuego);
    }
    server.send(200, "application/json", buf);
}

static void handleStart() {
    if (_partido) {
        if (_partido->pausado) {
            _partido->activo  = true;
            _partido->pausado = false;
            resetearDeteccionGoles();
            displayMarcador(_partido->goles[0], _partido->goles[1]);
            Serial.println("\n[JUEGO] Partido reanudado (web)");
        } else {
            vozPitidoInicio();
            comentaristaReiniciar();
            ambienteReiniciar();
            _partido->resetear();
            _partido->activo    = true;
            _partido->terminado = false;
            resetearDeteccionGoles();
            displayMarcador(0, 0);
            Serial.println("\n[JUEGO] ¡Partido iniciado!");
        }
    } else {
        // SP2 arranca solo via ambienteActualizar() en el loop
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStop() {
    if (_partido) {
        _partido->activo    = false;
        _partido->pausado   = false;
        _partido->terminado = true;
        Serial.println("\n[JUEGO] Partido detenido manualmente.");
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleConfigBrumeGet() {
    Serial.printf("\n[WEB] GET /configBrume — %s\n", server.client().remoteIP().toString().c_str());
    static char buf[900];
    snprintf(buf, sizeof(buf),
        "{"
        "\"intervaloComentariosMin\":%d,\"intervaloComentariosMax\":%d,\"intervaloStats\":%d,"
        "\"reglas\":{\"goleadaDiff\":%d,\"calienteGoles\":%d,"
          "\"primerosMinsSegs\":%d,\"ultimoTramoSegs\":%d,\"umbralAburridoSegs\":%d},"
        "\"comentarios\":{"
          "\"inicio\":{\"desde\":%d,\"hasta\":%d},"
          "\"primeros_minutos\":{\"desde\":%d,\"hasta\":%d},"
          "\"parejo\":{\"desde\":%d,\"hasta\":%d},"
          "\"caliente\":{\"desde\":%d,\"hasta\":%d},"
          "\"goleada\":{\"desde\":%d,\"hasta\":%d},"
          "\"definido\":{\"desde\":%d,\"hasta\":%d},"
          "\"ultimo_tramo\":{\"empate_goles\":{\"desde\":%d,\"hasta\":%d},\"goleada\":{\"desde\":%d,\"hasta\":%d},\"ajustado\":{\"desde\":%d,\"hasta\":%d},\"aburrido\":{\"desde\":%d,\"hasta\":%d}},"
          "\"aburrido\":{\"desde\":%d,\"hasta\":%d},"
          "\"tranquilo\":{\"desde\":%d,\"hasta\":%d}},"
        "\"goles\":{"
          "\"normal\":{\"desde\":%d,\"hasta\":%d},"
          "\"efusivo\":{\"desde\":%d,\"hasta\":%d},"
          "\"empate\":{\"desde\":%d,\"hasta\":%d},"
          "\"caliente\":{\"desde\":%d,\"hasta\":%d},"
          "\"agonico\":{\"desde\":%d,\"hasta\":%d},"
          "\"agonico_empate\":{\"desde\":%d,\"hasta\":%d}}"
        "}",
        config.intervaloComentariosMin, config.intervaloComentariosMax, config.intervaloStats,
        config.goleadaDiff, config.calienteGoles,
        config.primerosMinsSegs, config.ultimoTramoSegs, config.umbralAburridoSegs,
        config.comentInicio.desde,        config.comentInicio.hasta,
        config.comentPrimerosMins.desde,  config.comentPrimerosMins.hasta,
        config.comentParejo.desde,        config.comentParejo.hasta,
        config.comentCaliente.desde,      config.comentCaliente.hasta,
        config.comentGoleada.desde,       config.comentGoleada.hasta,
        config.comentDefinido.desde,               config.comentDefinido.hasta,
        config.comentUltimoTramoEmpateGoles.desde, config.comentUltimoTramoEmpateGoles.hasta,
        config.comentUltimoTramoGoleada.desde,     config.comentUltimoTramoGoleada.hasta,
        config.comentUltimoTramoAjustado.desde,    config.comentUltimoTramoAjustado.hasta,
        config.comentUltimoTramoAburrido.desde,    config.comentUltimoTramoAburrido.hasta,
        config.comentAburrido.desde,               config.comentAburrido.hasta,
        config.comentTranquilo.desde,     config.comentTranquilo.hasta,
        config.golNormal.desde,           config.golNormal.hasta,
        config.golEfusivo.desde,          config.golEfusivo.hasta,
        config.golEmpate.desde,           config.golEmpate.hasta,
        config.golCaliente.desde,         config.golCaliente.hasta,
        config.golAgonico.desde,          config.golAgonico.hasta,
        config.golAgonicoEmpate.desde,    config.golAgonicoEmpate.hasta
    );
    server.send(200, "application/json", buf);
}

static void handleTorneoCrear() {
    // No se puede armar un torneo nuevo con un partido individual en curso, ni
    // pisar un torneo que ya está activo — hay que cancelarlo primero.
    if (_partido && (_partido->activo || _partido->pausado)) {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"partido_en_curso\"}");
        return;
    }
    if (torneo.activo) {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"torneo_activo\"}");
        return;
    }
    uint8_t n = constrain(server.arg("n").toInt(), 2, TORNEO_MAX_JUG);
    uint8_t nGrupos = server.arg("nGrupos").toInt();
    String nombres[TORNEO_MAX_JUG];
    for (uint8_t i = 0; i < n; i++) {
        String key = "nombre" + String(i);
        nombres[i] = server.hasArg(key) ? server.arg(key) : "";
        nombres[i].trim();
        if (nombres[i].isEmpty()) nombres[i] = "Jugador " + String(i + 1);
    }
    bool ok = torneoCrear(nombres, n, nGrupos);
    Serial.printf("\n[Torneo] Crear (%d jugadores, %d grupos) — %s\n", n, nGrupos, ok ? "ok" : "error");
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleTorneoEstado() {
    server.send(200, "application/json", torneoEstadoJSON());
}

// El orden de partidos lo arma el sistema — el usuario solo confirma "¡Jugar!"
// sobre el próximo que corresponde, nunca elige cuál.
static void handleTorneoJugarProximo() {
    bool ok = torneoJugarProximo();
    if (ok) {
        uint8_t idx = torneo.partidoEnJuego;
        uint8_t pA, pB;
        if (torneo.enJuegoEsKO) { pA = torneo.partidosKO[idx].pA;    pB = torneo.partidosKO[idx].pB; }
        else                    { pA = torneo.partidosGrupo[idx].pA; pB = torneo.partidosGrupo[idx].pB; }
        static char texto[40];   // MD_Parola guarda el puntero, no una copia — tiene que ser estático
        snprintf(texto, sizeof(texto), "%s vs %s",
            torneo.participantes[pA].nombre, torneo.participantes[pB].nombre);
        displayTexto(texto, config.velocidadScroll);
        Serial.printf("\n[Torneo] Jugar %s vs %s\n",
            torneo.participantes[pA].nombre, torneo.participantes[pB].nombre);
    }
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleTorneoConfirmar() {
    uint8_t golA = _partido ? _partido->goles[0] : 0;
    uint8_t golB = _partido ? _partido->goles[1] : 0;
    bool ok = torneoConfirmar(golA, golB);
    Serial.printf("\n[Torneo] Confirmar resultado %d-%d — %s\n", golA, golB, ok ? "ok" : "error");
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleTorneoCancelar() {
    torneoCancelar();
    Serial.println("\n[Torneo] Cancelado");
    server.send(200, "application/json", "{\"ok\":true}");
}

// ── Init ─────────────────────────────────────────────────────────────────────

void webConfigInit(Partido* p) {
    _partido = p;
    cargarConfig();
    cargarWiFiCreds();
    torneoInit();

    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    IPAddress apIp = WiFi.softAPIP();

    MDNS.begin("metegol");
    MDNS.addService("http", "tcp", 80);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("\n[WiFi] AP — cliente conectado    MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
            info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
            info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
            info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
    }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("\n[WiFi] AP — cliente desconectado MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
            info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
            info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
            info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
    }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

    // Conexión STA no bloqueante — prueba cada red guardada con 10s de timeout
    if (_nNets > 0) {
        _tryNetIdx  = 0;
        _staStartMs = millis();
        Serial.printf("\n[WiFi] Probando %d red(es) guardada(s)...\n", _nNets);
        WiFi.begin(_staSSID[0], _staPass[0]);
    } else {
        _staGaveUp = true;
    }

    // Captive portal
    dns.start(53, "*", apIp);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/estado", HTTP_GET, handleEstado);
    server.on("/start",  HTTP_POST, handleStart);
    server.on("/stop",   HTTP_POST, handleStop);
    server.on("/configBrume", HTTP_GET, handleConfigBrumeGet);

    server.on("/torneo/crear",     HTTP_POST, handleTorneoCrear);
    server.on("/torneo/estado",    HTTP_GET,  handleTorneoEstado);
    server.on("/torneo/jugarProximo", HTTP_POST, handleTorneoJugarProximo);
    server.on("/torneo/confirmar", HTTP_POST, handleTorneoConfirmar);
    server.on("/torneo/cancelar",  HTTP_POST, handleTorneoCancelar);

    server.on("/wifiStatus", HTTP_GET, [](){
        String json = "{\"connected\":";
        json += WiFi.isConnected() ? "true" : "false";
        json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
        json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
        json += ",\"saved\":[";
        for (uint8_t i = 0; i < _nNets; i++) {
            if (i) json += ",";
            json += "\"" + String(_staSSID[i]) + "\"";
        }
        json += "]}";
        server.send(200, "application/json", json);
    });

    server.on("/wifi", HTTP_POST, [](){
        String ssid = server.arg("ssid"); ssid.trim();
        String pass = server.arg("pass");
        if (ssid.isEmpty()) { server.send(400, "application/json", "{\"error\":\"SSID vacío\"}"); return; }
        addOrUpdateNet(ssid.c_str(), pass.c_str());
        _staGaveUp  = false;
        _tryNetIdx  = _nNets - 1;  // intentar primero la recién agregada
        _staStartMs = millis();
        WiFi.disconnect(false);
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.printf("\n[WiFi] Red '%s' guardada, intentando conexión...\n", ssid.c_str());
        server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/wifi-delete", HTTP_POST, [](){
        uint8_t idx = (uint8_t)server.arg("idx").toInt();
        if (idx >= _nNets) { server.send(400, "application/json", "{\"error\":\"idx inválido\"}"); return; }
        for (uint8_t i = idx; i < _nNets - 1; i++) {
            strlcpy(_staSSID[i], _staSSID[i+1], sizeof(_staSSID[i]));
            strlcpy(_staPass[i], _staPass[i+1], sizeof(_staPass[i]));
        }
        memset(_staSSID[_nNets-1], 0, sizeof(_staSSID[_nNets-1]));
        memset(_staPass[_nNets-1], 0, sizeof(_staPass[_nNets-1]));
        _nNets--;
        guardarWiFiCreds();
        Serial.printf("\n[WiFi] Red eliminada, quedan %d\n", _nNets);
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // Android: espera 204 para confirmar internet
    server.on("/generate_204", HTTP_GET, [](){
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302);
    });

    // Windows: espera este texto exacto
    server.on("/connecttest.txt", HTTP_GET, [](){
        server.send(200, "text/plain", "Microsoft Connect Test");
    });
    server.on("/ncsi.txt", HTTP_GET, [](){
        server.send(200, "text/plain", "Microsoft NCSI");
    });
    server.on("/redirect", HTTP_GET, [](){
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302);
    });

    // iOS/macOS
    server.on("/hotspot-detect.html", HTTP_GET, [](){
        server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });

    server.onNotFound([](){
        // Redirigir captive portal solo si el cliente viene del AP (192.168.4.x)
        // Clientes STA (red de casa) reciben 404 normal
        IPAddress client = server.client().remoteIP();
        if (client[0] == 192 && client[1] == 168 && client[2] == 4) {
            server.sendHeader("Location", "http://192.168.4.1/");
            server.send(302);
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    server.begin();

    // ── Banner de estado WiFi ─────────────────────────────────────────────────
    Serial.println();
    Serial.println("┌────────────────── WiFi ──────────────────┐");
    Serial.printf ("│  AP    : %-8s   %s             │\n", WIFI_SSID, apIp.toString().c_str());
    if (_nNets > 0) {
        Serial.printf("│  STA   : %d red(es) guardada(s)            │\n", _nNets);
        for (uint8_t i = 0; i < _nNets; i++)
            Serial.printf("│    [%d] %-36s│\n", i, _staSSID[i]);
        Serial.println("│         conectando en background...      │");
    } else {
        Serial.println("│  STA   : sin configurar                  │");
    }
    Serial.println("│  Acceso: http://192.168.4.1/             │");
    Serial.println("└──────────────────────────────────────────┘");
    Serial.println();
}

void webConfigLoop() {
    dns.processNextRequest();
    server.handleClient();

    if (_pendingVolUpdate) {
        _pendingVolUpdate = false;
        vozSetVolumen(config.volumenVoz);
        ambienteSetVolumen(config.volumenAmbiente);
    }

    if (!_staAnunciado && !_staGaveUp) {
        if (WiFi.isConnected()) {
            anunciarSTA();
        } else if (millis() - _staStartMs > 10000) {
            _tryNetIdx++;
            if (_tryNetIdx < _nNets) {
                WiFi.disconnect(false);
                Serial.printf("\n[WiFi] '%s' sin respuesta — probando '%s'...\n",
                    _staSSID[_tryNetIdx-1], _staSSID[_tryNetIdx]);
                WiFi.begin(_staSSID[_tryNetIdx], _staPass[_tryNetIdx]);
                _staStartMs = millis();
            } else {
                _staGaveUp = true;
                WiFi.disconnect(true);
                Serial.println("\n[WiFi] Ninguna red disponible — solo AP");
                Serial.println("[WiFi] Acceso: http://192.168.4.1/  o  http://metegol.local/");
            }
        }
    }
    if (_staAnunciado && !WiFi.isConnected()) {
        _staAnunciado = false;
        _staGaveUp    = false;
        _tryNetIdx    = 0;
        _staStartMs   = millis();
        Serial.println("\n[WiFi] STA desconectado — reintentando redes...");
        if (_nNets > 0) WiFi.begin(_staSSID[0], _staPass[0]);
    }
}
