#include "WebConfig.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include <Arduino.h>

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

static char _staSSID[33] = "";
static char _staPass[65] = "";
static bool _staAnunciado = false;

static void anunciarSTA() {
    if (_staAnunciado) return;
    _staAnunciado = true;
    MDNS.begin("metegol");
    MDNS.addService("http", "tcp", 80);
    Serial.println();
    Serial.println("───────────────── Metegol online ─────────────────");
    Serial.printf ("  IP      : http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("  DNS     : http://metegol.local/");
    Serial.println("  Config  : http://metegol.local/configBrume");
    Serial.println("──────────────────────────────────────────────────");
}

static void cargarWiFiCreds() {
    prefs.begin("metegol-wifi", true);
    strlcpy(_staSSID, prefs.getString("ssid", "").c_str(), sizeof(_staSSID));
    strlcpy(_staPass, prefs.getString("pass", "").c_str(), sizeof(_staPass));
    prefs.end();
}

static void guardarWiFiCreds(const char* ssid, const char* pass) {
    prefs.begin("metegol-wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

// ── Persistencia ─────────────────────────────────────────────────────────────

static void cargarConfig() {
    prefs.begin("metegol", true);
    config.volumenVoz      = prefs.getUChar("volVoz",     30);
    config.volumenAmbiente = prefs.getUChar("volAmb",     20);
    config.modoJuego       = prefs.getUChar("modo",        0);
    config.golesMax        = prefs.getUChar("golesMax",    4);
    config.duracionMin     = prefs.getUShort("durMin",     5);
    config.brillo          = prefs.getUChar("brillo",      5);
    config.velocidadScroll = prefs.getUChar("velScroll",  40);
    config.pistaAmbiente   = prefs.getUChar("pistaAmb",    3);

    // Comentarista — thresholds
    config.intervaloComentariosMin = prefs.getUShort("intervComMin",  10);
    config.intervaloComentariosMax = prefs.getUShort("intervComMax",  30);
    config.intervaloStats          = prefs.getUShort("intervStats",    4);
    config.goleadaDiff             = prefs.getUChar("goleadaDiff",    3);
    config.calienteGoles           = prefs.getUChar("calienteGol",    4);
    config.inicioSegs              = prefs.getUShort("inicioSegs",   30);
    config.primerosMinsSegs        = prefs.getUShort("primMinsSegs", 120);
    config.ultimoTramoPorc         = prefs.getUChar("ultiTramoPc",   10);
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
    config.comentUltimoTramo.desde  = prefs.getUChar("cUtD",  37);
    config.comentUltimoTramo.hasta  = prefs.getUChar("cUtH",  42);
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
    prefs.end();
}

static void guardarConfig() {
    prefs.begin("metegol", false);
    prefs.putUChar("volVoz",    config.volumenVoz);
    prefs.putUChar("volAmb",    config.volumenAmbiente);
    prefs.putUChar("modo",      config.modoJuego);
    prefs.putUChar("golesMax",  config.golesMax);
    prefs.putUShort("durMin",   config.duracionMin);
    prefs.putUChar("brillo",    config.brillo);
    prefs.putUChar("velScroll", config.velocidadScroll);
    prefs.putUChar("pistaAmb",  config.pistaAmbiente);

    // Comentarista — thresholds
    prefs.putUShort("intervComMin",  config.intervaloComentariosMin);
    prefs.putUShort("intervComMax",  config.intervaloComentariosMax);
    prefs.putUShort("intervStats",   config.intervaloStats);
    prefs.putUChar("goleadaDiff",    config.goleadaDiff);
    prefs.putUChar("calienteGol",    config.calienteGoles);
    prefs.putUShort("inicioSegs",    config.inicioSegs);
    prefs.putUShort("primMinsSegs",  config.primerosMinsSegs);
    prefs.putUChar("ultiTramoPc",    config.ultimoTramoPorc);
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
    prefs.putUChar("cUtD",  config.comentUltimoTramo.desde);
    prefs.putUChar("cUtH",  config.comentUltimoTramo.hasta);
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
    prefs.end();
}

// ── HTML ─────────────────────────────────────────────────────────────────────

static const char HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Metegol Config</title>
<style>
  :root {
    --bg: #0f0f13;
    --card: #1a1a24;
    --border: #2a2a3a;
    --accent: #00e5ff;
    --accent2: #ff4081;
    --text: #e0e0e0;
    --muted: #888;
    --radius: 14px;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Segoe UI', sans-serif;
    min-height: 100vh;
    padding: 20px;
  }
  header {
    text-align: center;
    padding: 24px 0 32px;
  }
  header h1 {
    font-size: 2rem;
    font-weight: 700;
    letter-spacing: 3px;
    text-transform: uppercase;
    background: linear-gradient(90deg, var(--accent), var(--accent2));
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
  }
  header p { color: var(--muted); font-size: .85rem; margin-top: 6px; }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 16px;
    max-width: 900px;
    margin: 0 auto;
  }
  .card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 22px;
  }
  .card h2 {
    font-size: .75rem;
    font-weight: 600;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--accent);
    margin-bottom: 18px;
  }
  .field { margin-bottom: 18px; }
  .field:last-child { margin-bottom: 0; }
  label {
    display: flex;
    justify-content: space-between;
    font-size: .85rem;
    color: var(--muted);
    margin-bottom: 8px;
  }
  label span {
    font-weight: 700;
    color: var(--text);
    min-width: 28px;
    text-align: right;
  }
  input[type=range] {
    -webkit-appearance: none;
    width: 100%;
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    outline: none;
  }
  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 18px; height: 18px;
    border-radius: 50%;
    background: var(--accent);
    cursor: pointer;
    box-shadow: 0 0 8px var(--accent);
  }
  .toggle-row {
    display: flex;
    gap: 10px;
  }
  .toggle-btn {
    flex: 1;
    padding: 10px;
    border: 1px solid var(--border);
    border-radius: 8px;
    background: transparent;
    color: var(--muted);
    font-size: .85rem;
    cursor: pointer;
    transition: all .2s;
  }
  .toggle-btn.active {
    border-color: var(--accent);
    color: var(--accent);
    background: rgba(0,229,255,.08);
  }
  .save-wrap {
    max-width: 900px;
    margin: 24px auto 0;
    text-align: center;
  }
  .btn-save {
    padding: 14px 48px;
    background: linear-gradient(90deg, var(--accent), #0097a7);
    border: none;
    border-radius: 30px;
    color: #000;
    font-weight: 700;
    font-size: 1rem;
    letter-spacing: 1px;
    cursor: pointer;
    transition: opacity .2s;
    box-shadow: 0 4px 20px rgba(0,229,255,.3);
  }
  .btn-save:hover { opacity: .85; }
  #toast {
    position: fixed;
    bottom: 30px; left: 50%;
    transform: translateX(-50%) translateY(80px);
    background: var(--accent);
    color: #000;
    padding: 12px 28px;
    border-radius: 30px;
    font-weight: 700;
    font-size: .9rem;
    transition: transform .3s;
    pointer-events: none;
  }
  #toast.show { transform: translateX(-50%) translateY(0); }
</style>
</head>
<body>
<header>
  <h1>⚽ Metegol</h1>
  <p>Configuración del sistema</p>
</header>

<div style="max-width:900px;margin:0 auto 16px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:22px;text-align:center">
  <div id="partido-label" style="font-size:.7rem;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin-bottom:10px">En espera</div>
  <div id="marcador" style="font-size:3rem;font-weight:700;letter-spacing:6px;color:var(--accent)">- - -</div>
  <div id="timer-wrap" style="display:none;margin-top:10px">
    <div style="font-size:.65rem;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin-bottom:2px">Tiempo restante</div>
    <div id="timer" style="font-size:1.8rem;font-weight:600;color:var(--muted)">--:--</div>
  </div>
  <div id="ganador-wrap" style="display:none;margin-top:8px;font-size:1rem;font-weight:600;color:var(--accent)"></div>
  <div style="margin-top:14px">
    <button id="btn-start" onclick="iniciarPartido()" style="padding:10px 32px;background:var(--accent);border:none;border-radius:20px;color:#000;font-weight:700;cursor:pointer;font-size:.9rem">Iniciar partido</button>
  </div>
</div>

<form id="cfg" method="POST" action="/save">
<div class="grid">

  <div class="card">
    <h2>🔊 Audio — Voz (SP1)</h2>
    <div class="field">
      <label>Volumen <span id="vv">%VOL_VOZ%</span></label>
      <input type="range" name="volumenVoz" min="0" max="30" value="%VOL_VOZ%"
             oninput="document.getElementById('vv').textContent=this.value">
    </div>
  </div>

  <div class="card">
    <h2>🎵 Audio — Ambiente (SP2)</h2>
    <div class="field">
      <label>Volumen <span id="va">%VOL_AMB%</span></label>
      <input type="range" name="volumenAmbiente" min="0" max="30" value="%VOL_AMB%"
             oninput="document.getElementById('va').textContent=this.value">
    </div>
    <div class="field">
      <label>Pista ambiente <span id="pa">%PISTA_AMB%</span></label>
      <input type="range" name="pistaAmbiente" min="1" max="3" value="%PISTA_AMB%"
             oninput="document.getElementById('pa').textContent=this.value">
    </div>
  </div>

  <div class="card">
    <h2>🎮 Modalidad del juego</h2>
    <div class="field">
      <label>Modo</label>
      <div class="toggle-row">
        <button type="button" class="toggle-btn %MODO_GOLES_ACTIVE%" onclick="setModo(0)">Por goles</button>
        <button type="button" class="toggle-btn %MODO_TIEMPO_ACTIVE%" onclick="setModo(1)">Por tiempo</button>
      </div>
      <input type="hidden" name="modoJuego" id="modoJuego" value="%MODO%">
    </div>
    <div class="field" id="row-goles">
      <label>Goles para ganar <span id="gm">%GOLES_MAX%</span></label>
      <input type="range" name="golesMax" min="4" max="10" value="%GOLES_MAX%"
             oninput="document.getElementById('gm').textContent=this.value">
    </div>
    <div class="field" id="row-tiempo">
      <label>Duración (minutos) <span id="dm">%DUR_MIN%</span></label>
      <input type="range" name="duracionMin" min="3" max="8" value="%DUR_MIN%"
             oninput="document.getElementById('dm').textContent=this.value">
    </div>
  </div>

  <div class="card">
    <h2>💡 Display LED</h2>
    <div class="field">
      <label>Brillo <span id="br">%BRILLO%</span></label>
      <input type="range" name="brillo" min="0" max="15" value="%BRILLO%"
             oninput="document.getElementById('br').textContent=this.value">
    </div>
    <div class="field">
      <label>Velocidad scroll (ms) <span id="vs">%VEL_SCROLL%</span></label>
      <input type="range" name="velocidadScroll" min="10" max="100" value="%VEL_SCROLL%"
             oninput="document.getElementById('vs').textContent=this.value">
    </div>
  </div>

  <div class="card">
    <h2>💬 Comentarista</h2>
    <div class="field">
      <label>Intervalo mínimo (seg) <span id="icn">%INTERV_COM_MIN%</span></label>
      <input type="range" name="intervaloComentariosMin" min="5" max="120" value="%INTERV_COM_MIN%"
             oninput="document.getElementById('icn').textContent=this.value">
    </div>
    <div class="field">
      <label>Intervalo máximo (seg) <span id="icx">%INTERV_COM_MAX%</span></label>
      <input type="range" name="intervaloComentariosMax" min="5" max="120" value="%INTERV_COM_MAX%"
             oninput="document.getElementById('icx').textContent=this.value">
    </div>
    <div class="field">
      <label>Intervalo stats serial (seg) <span id="ist">%INTERV_STATS%</span></label>
      <input type="range" name="intervaloStats" min="3" max="30" value="%INTERV_STATS%"
             oninput="document.getElementById('ist').textContent=this.value">
    </div>
    <div class="field">
      <label>Diff. goles para goleada <span id="gd">%GOLEADA_DIFF%</span></label>
      <input type="range" name="goleadaDiff" min="2" max="6" value="%GOLEADA_DIFF%"
             oninput="document.getElementById('gd').textContent=this.value">
    </div>
    <div class="field">
      <label>Total goles para "caliente" <span id="cg">%CALIENTE_GOL%</span></label>
      <input type="range" name="calienteGoles" min="2" max="10" value="%CALIENTE_GOL%"
             oninput="document.getElementById('cg').textContent=this.value">
    </div>
    <div class="field">
      <label>Segundos fase inicio <span id="ise">%INICIO_SEGS%</span></label>
      <input type="range" name="inicioSegs" min="5" max="120" value="%INICIO_SEGS%"
             oninput="document.getElementById('ise').textContent=this.value">
    </div>
    <div class="field">
      <label>Segundos primeros minutos <span id="pms">%PRIM_MINS_SEGS%</span></label>
      <input type="range" name="primerosMinsSegs" min="30" max="300" value="%PRIM_MINS_SEGS%"
             oninput="document.getElementById('pms').textContent=this.value">
    </div>
    <div class="field">
      <label>% tiempo para último tramo <span id="utp">%ULTI_TRAMO%</span></label>
      <input type="range" name="ultimoTramoPorc" min="5" max="30" value="%ULTI_TRAMO%"
             oninput="document.getElementById('utp').textContent=this.value">
    </div>
  </div>

  <div class="card">
    <h2>🎵 Rangos de Audio</h2>
    <p style="font-size:.8rem;color:var(--muted);margin-bottom:14px">Pistas MP3 por estado del partido</p>
    <table style="width:100%;border-collapse:collapse;font-size:.82rem">
      <thead><tr style="color:var(--muted)">
        <th style="text-align:left;padding:4px 0;font-weight:500">Estado / Gol</th>
        <th style="padding:4px 8px;font-weight:500">Desde</th>
        <th style="padding:4px 8px;font-weight:500">Hasta</th>
      </tr></thead>
      <tbody>
        <tr><td colspan="3" style="padding:6px 0 2px;color:var(--muted);font-size:.75rem;letter-spacing:1px">COMENTARIOS</td></tr>
        <tr><td style="padding:4px 0">inicio</td>
          <td style="padding:3px 8px"><input type="number" name="cInD" min="0" max="255" value="%C_IN_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cInH" min="0" max="255" value="%C_IN_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">primeros_minutos</td>
          <td style="padding:3px 8px"><input type="number" name="cPrD" min="0" max="255" value="%C_PR_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cPrH" min="0" max="255" value="%C_PR_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">parejo</td>
          <td style="padding:3px 8px"><input type="number" name="cPaD" min="0" max="255" value="%C_PA_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cPaH" min="0" max="255" value="%C_PA_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">caliente</td>
          <td style="padding:3px 8px"><input type="number" name="cCaD" min="0" max="255" value="%C_CA_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cCaH" min="0" max="255" value="%C_CA_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">goleada</td>
          <td style="padding:3px 8px"><input type="number" name="cGoD" min="0" max="255" value="%C_GO_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cGoH" min="0" max="255" value="%C_GO_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">definido</td>
          <td style="padding:3px 8px"><input type="number" name="cDeD" min="0" max="255" value="%C_DE_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cDeH" min="0" max="255" value="%C_DE_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">ultimo_tramo</td>
          <td style="padding:3px 8px"><input type="number" name="cUtD" min="0" max="255" value="%C_UT_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cUtH" min="0" max="255" value="%C_UT_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">aburrido</td>
          <td style="padding:3px 8px"><input type="number" name="cAbD" min="0" max="255" value="%C_AB_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cAbH" min="0" max="255" value="%C_AB_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0">tranquilo</td>
          <td style="padding:3px 8px"><input type="number" name="cTrD" min="0" max="255" value="%C_TR_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="cTrH" min="0" max="255" value="%C_TR_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td colspan="3" style="padding:10px 0 2px;color:var(--muted);font-size:.75rem;letter-spacing:1px">GOLES ⚽</td></tr>
        <tr><td style="padding:4px 0;color:var(--accent2)">gol_normal</td>
          <td style="padding:3px 8px"><input type="number" name="gNorD" min="0" max="255" value="%G_NOR_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="gNorH" min="0" max="255" value="%G_NOR_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0;color:var(--accent2)">gol_efusivo</td>
          <td style="padding:3px 8px"><input type="number" name="gEfD" min="0" max="255" value="%G_EF_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="gEfH" min="0" max="255" value="%G_EF_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0;color:var(--accent2)">gol_empate</td>
          <td style="padding:3px 8px"><input type="number" name="gEmD" min="0" max="255" value="%G_EM_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="gEmH" min="0" max="255" value="%G_EM_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0;color:var(--accent2)">gol_caliente</td>
          <td style="padding:3px 8px"><input type="number" name="gCaD" min="0" max="255" value="%G_CA_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="gCaH" min="0" max="255" value="%G_CA_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0;color:var(--accent2)">gol_agonico</td>
          <td style="padding:3px 8px"><input type="number" name="gAgD" min="0" max="255" value="%G_AG_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="gAgH" min="0" max="255" value="%G_AG_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
        <tr><td style="padding:4px 0;color:var(--accent2)">gol_agonico_empate</td>
          <td style="padding:3px 8px"><input type="number" name="gAeD" min="0" max="255" value="%G_AE_D%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td>
          <td style="padding:3px 8px"><input type="number" name="gAeH" min="0" max="255" value="%G_AE_H%" style="width:52px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:6px;color:var(--text);font-size:.85rem;padding:4px 6px;text-align:center;outline:none"></td></tr>
      </tbody>
    </table>
  </div>

</div>

<div class="save-wrap">
  <button type="submit" class="btn-save">GUARDAR CONFIGURACIÓN</button>
</div>
</form>

<div class="grid" style="margin-top:16px">
  <div class="card" id="wifi-card">
    <h2>📶 Acceso por tu WiFi</h2>

    <!-- Estado: no conectado -->
    <div id="state-disconnected">
      <div style="background:rgba(0,229,255,.06);border:1px solid rgba(0,229,255,.2);border-radius:10px;padding:16px;margin-bottom:18px">
        <p style="font-size:.9rem;color:var(--text);margin-bottom:8px"><b>¿Cómo funciona?</b></p>
        <p style="font-size:.82rem;color:var(--muted);line-height:1.6">
          Ahora estás conectado al AP <b style="color:var(--text)">Metegol</b>.<br>
          Si ingresás tu WiFi de casa, el próximo acceso lo hacés directamente desde
          <b style="color:var(--accent)">http://metegol.local</b> sin tener que cambiar de red.
        </p>
        <div style="display:flex;gap:8px;margin-top:12px;font-size:.8rem;color:var(--muted)">
          <span style="background:rgba(0,229,255,.1);padding:4px 10px;border-radius:20px">1. Ingresá tu red</span>
          <span style="background:rgba(0,229,255,.1);padding:4px 10px;border-radius:20px">2. Conectar</span>
          <span style="background:rgba(0,229,255,.1);padding:4px 10px;border-radius:20px">3. Usá metegol.local</span>
        </div>
      </div>
      <div class="field">
        <label>Nombre de red (SSID)</label>
        <input type="text" id="wSSID" placeholder="Tu WiFi de casa"
          style="width:100%;padding:10px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:8px;color:var(--text);font-size:.9rem;outline:none">
      </div>
      <div class="field">
        <label>Contraseña</label>
        <input type="password" id="wPass" placeholder="Contraseña del WiFi"
          style="width:100%;padding:10px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:8px;color:var(--text);font-size:.9rem;outline:none">
      </div>
      <button type="button" onclick="conectarWifi()"
        style="width:100%;padding:12px;background:linear-gradient(90deg,#7c4dff,#651fff);border:none;border-radius:10px;color:#fff;font-weight:700;font-size:.95rem;cursor:pointer;letter-spacing:.5px">
        CONECTAR A MI WIFI
      </button>
      <div id="wifi-msg" style="font-size:.82rem;color:var(--muted);margin-top:12px;text-align:center;min-height:20px"></div>
    </div>

    <!-- Estado: conectado -->
    <div id="state-connected" style="display:none">
      <div style="text-align:center;padding:8px 0 16px">
        <div style="font-size:2rem;margin-bottom:8px">✅</div>
        <p style="color:var(--muted);font-size:.85rem;margin-bottom:4px">Conectado a <b id="ssid-label" style="color:var(--text)"></b></p>
        <p style="color:var(--muted);font-size:.82rem;margin-bottom:16px">Accedé al metegol desde cualquier dispositivo en tu red:</p>
        <a href="http://metegol.local" target="_blank"
          style="display:inline-block;padding:14px 28px;background:linear-gradient(90deg,var(--accent),#0097a7);border-radius:30px;color:#000;font-weight:700;font-size:1.05rem;text-decoration:none;box-shadow:0 4px 20px rgba(0,229,255,.3)">
          🌐 metegol.local
        </a>
      </div>
      <div style="border-top:1px solid var(--border);padding-top:14px;text-align:center">
        <button type="button" onclick="mostrarCambioRed()"
          style="background:transparent;border:1px solid var(--border);color:var(--muted);padding:8px 20px;border-radius:20px;font-size:.82rem;cursor:pointer">
          Cambiar red WiFi
        </button>
      </div>
      <div id="cambio-red" style="display:none;margin-top:14px">
        <div class="field">
          <label>Nueva red (SSID)</label>
          <input type="text" id="wSSID2" placeholder="Nombre de la red"
            style="width:100%;padding:10px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:8px;color:var(--text);font-size:.9rem;outline:none">
        </div>
        <div class="field">
          <label>Contraseña</label>
          <input type="password" id="wPass2" placeholder="Contraseña"
            style="width:100%;padding:10px;background:var(--border);border:1px solid rgba(255,255,255,.1);border-radius:8px;color:var(--text);font-size:.9rem;outline:none">
        </div>
        <button type="button" onclick="cambiarRed()"
          style="width:100%;padding:10px;background:linear-gradient(90deg,#7c4dff,#651fff);border:none;border-radius:10px;color:#fff;font-weight:700;cursor:pointer">
          CAMBIAR RED
        </button>
        <div id="wifi-msg2" style="font-size:.82rem;color:var(--muted);margin-top:10px;text-align:center"></div>
      </div>
    </div>
  </div>
</div>

<div id="toast">✓ Configuración guardada</div>

<script>
  function setModo(v) {
    document.getElementById('modoJuego').value = v;
    document.querySelectorAll('.toggle-btn').forEach((b,i) => b.classList.toggle('active', i===v));
    document.getElementById('row-goles').style.display = v===0?'':'none';
    document.getElementById('row-tiempo').style.display = v===1?'':'none';
  }
  setModo(parseInt(document.getElementById('modoJuego').value));

  function actualizarEstadoWiFi() {
    fetch('/wifiStatus').then(r=>r.json()).then(d=>{
      if (d.connected) {
        document.getElementById('state-disconnected').style.display = 'none';
        document.getElementById('state-connected').style.display = 'block';
        document.getElementById('ssid-label').textContent = d.ssid;
      } else {
        document.getElementById('state-disconnected').style.display = 'block';
        document.getElementById('state-connected').style.display = 'none';
      }
    }).catch(()=>{});
  }

  function mostrarCambioRed() {
    const cr = document.getElementById('cambio-red');
    cr.style.display = cr.style.display === 'none' ? 'block' : 'none';
  }

  function _doConectar(ssid, pass, msgEl) {
    if (!ssid) { msgEl.textContent = 'Ingresá el nombre de la red'; return; }
    msgEl.style.color = 'var(--accent)';
    msgEl.textContent = 'Conectando...';
    fetch('/wifi', { method:'POST', body: new URLSearchParams({ssid, pass}) })
      .then(r=>r.json()).then(()=>{
        let intentos = 0;
        const poll = setInterval(() => {
          intentos++;
          msgEl.textContent = 'Conectando' + '.'.repeat(intentos % 4);
          fetch('/wifiStatus').then(r=>r.json()).then(d=>{
            if (d.connected) {
              clearInterval(poll);
              msgEl.textContent = '';
              actualizarEstadoWiFi();
            } else if (intentos > 20) {
              clearInterval(poll);
              msgEl.style.color = '#ff4081';
              msgEl.textContent = 'No se pudo conectar. Verificá la contraseña.';
            }
          }).catch(()=>{});
        }, 1500);
      }).catch(()=>{ msgEl.textContent = 'Error al enviar'; });
  }
  actualizarEstadoWiFi();
  setInterval(actualizarEstadoWiFi, 5000);

  function conectarWifi() {
    _doConectar(
      document.getElementById('wSSID').value.trim(),
      document.getElementById('wPass').value,
      document.getElementById('wifi-msg')
    );
  }

  function cambiarRed() {
    _doConectar(
      document.getElementById('wSSID2').value.trim(),
      document.getElementById('wPass2').value,
      document.getElementById('wifi-msg2')
    );
  }

  function actualizarMarcador() {
    fetch('/estado').then(r=>r.json()).then(d=>{
      const lbl = document.getElementById('partido-label');
      const gw  = document.getElementById('ganador-wrap');
      const btn = document.getElementById('btn-start');
      const tw  = document.getElementById('timer-wrap');

      if (d.activo) {
        lbl.textContent = 'Partido en curso';
        document.getElementById('marcador').textContent = d.marcador || '0 - 0';
        gw.style.display = 'none';
        btn.textContent = 'Reiniciar';
        if (d.modo === 1 && d.tiempoRestante > 0) {
          tw.style.display = 'block';
          const s = d.tiempoRestante;
          document.getElementById('timer').textContent =
            String(Math.floor(s/60)).padStart(2,'0') + ':' + String(s%60).padStart(2,'0');
        } else {
          tw.style.display = 'none';
        }
      } else if (d.terminado) {
        lbl.textContent = 'Partido finalizado';
        document.getElementById('marcador').textContent = d.marcador || '0 - 0';
        tw.style.display = 'none';
        const g = d.goles || [0,0];
        gw.style.display = 'block';
        gw.textContent = g[0] > g[1] ? '¡Ganó equipo 1!' : g[1] > g[0] ? '¡Ganó equipo 2!' : '¡Empate!';
        btn.textContent = 'Nuevo partido';
      } else {
        lbl.textContent = 'En espera';
        document.getElementById('marcador').textContent = '- - -';
        tw.style.display = 'none';
        gw.style.display = 'none';
        btn.textContent = 'Iniciar partido';
      }
    }).catch(()=>{});
  }

  function iniciarPartido() {
    fetch('/start', {method:'POST'}).then(()=>actualizarMarcador()).catch(()=>{});
  }

  actualizarMarcador();
  setInterval(actualizarMarcador, 3000);

  document.getElementById('cfg').addEventListener('submit', function(e) {
    e.preventDefault();
    const data = new FormData(this);
    fetch('/save', { method:'POST', body: new URLSearchParams(data) })
      .then(r => r.json())
      .then(j => {
        if (j.ok) {
          const t = document.getElementById('toast');
          t.classList.add('show');
          setTimeout(() => t.classList.remove('show'), 3000);
        }
      }).catch(() => {});
  });
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
    html.replace("%GOLES_MAX%",  String(config.golesMax));
    html.replace("%DUR_MIN%",    String(config.duracionMin));
    html.replace("%BRILLO%",     String(config.brillo));
    html.replace("%VEL_SCROLL%", String(config.velocidadScroll));
    // Comentarista — thresholds
    html.replace("%INTERV_COM_MIN%", String(config.intervaloComentariosMin));
    html.replace("%INTERV_COM_MAX%", String(config.intervaloComentariosMax));
    html.replace("%INTERV_STATS%",   String(config.intervaloStats));
    html.replace("%GOLEADA_DIFF%",String(config.goleadaDiff));
    html.replace("%CALIENTE_GOL%",String(config.calienteGoles));
    html.replace("%INICIO_SEGS%", String(config.inicioSegs));
    html.replace("%ULTI_TRAMO%",  String(config.ultimoTramoPorc));
    // También agrego primerosMinsSegs al slider (si hay un placeholder en el HTML)
    html.replace("%PRIM_MINS_SEGS%", String(config.primerosMinsSegs));
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
    html.replace("%C_UT_D%", String(config.comentUltimoTramo.desde));
    html.replace("%C_UT_H%", String(config.comentUltimoTramo.hasta));
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
    return html;
}

static void handleRoot() {
    server.send(200, "text/html", buildPage());
}

static void handleSave() {
    if (server.hasArg("volumenVoz"))      config.volumenVoz      = server.arg("volumenVoz").toInt();
    if (server.hasArg("volumenAmbiente")) config.volumenAmbiente = server.arg("volumenAmbiente").toInt();
    if (server.hasArg("modoJuego"))       config.modoJuego       = server.arg("modoJuego").toInt();
    if (server.hasArg("golesMax"))        config.golesMax        = constrain(server.arg("golesMax").toInt(), 4, 10);
    if (server.hasArg("duracionMin"))     config.duracionMin     = constrain(server.arg("duracionMin").toInt(), 3, 8);
    if (server.hasArg("brillo"))          config.brillo          = server.arg("brillo").toInt();
    if (server.hasArg("velocidadScroll")) config.velocidadScroll = server.arg("velocidadScroll").toInt();
    if (server.hasArg("pistaAmbiente"))   config.pistaAmbiente   = server.arg("pistaAmbiente").toInt();
    // Comentarista — thresholds
    if (server.hasArg("intervaloComentariosMin")) config.intervaloComentariosMin = server.arg("intervaloComentariosMin").toInt();
    if (server.hasArg("intervaloComentariosMax")) config.intervaloComentariosMax = server.arg("intervaloComentariosMax").toInt();
    if (server.hasArg("intervaloStats"))          config.intervaloStats          = constrain(server.arg("intervaloStats").toInt(), 3, 30);
    if (server.hasArg("goleadaDiff"))          config.goleadaDiff          = server.arg("goleadaDiff").toInt();
    if (server.hasArg("calienteGoles"))        config.calienteGoles        = server.arg("calienteGoles").toInt();
    if (server.hasArg("inicioSegs"))           config.inicioSegs           = server.arg("inicioSegs").toInt();
    if (server.hasArg("primerosMinsSegs"))     config.primerosMinsSegs     = server.arg("primerosMinsSegs").toInt();
    if (server.hasArg("ultimoTramoPorc"))      config.ultimoTramoPorc      = server.arg("ultimoTramoPorc").toInt();
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
    if (server.hasArg("cUtD")) config.comentUltimoTramo.desde     = server.arg("cUtD").toInt();
    if (server.hasArg("cUtH")) config.comentUltimoTramo.hasta     = server.arg("cUtH").toInt();
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

    guardarConfig();
    _pendingVolUpdate = true;  // aplica volumen en el próximo loop, no aquí
    Serial.printf("[Config] Guardada — SP1 vol:%d SP2 vol:%d modo:%s\n",
        config.volumenVoz, config.volumenAmbiente,
        config.modoJuego == 0 ? "goles" : "tiempo");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleEstado() {
    char buf[160];
    if (_partido) {
        char marcador[16];
        _partido->getResultado(marcador, sizeof(marcador));
        uint32_t elapsed  = millis() - _partido->inicio;
        uint32_t durMs    = (uint32_t)config.duracionMin * 60000UL;
        uint32_t restante = (config.modoJuego == 1 && elapsed < durMs)
                            ? (durMs - elapsed) / 1000 : 0;
        snprintf(buf, sizeof(buf),
            "{\"goles\":[%d,%d],\"marcador\":\"%s\",\"modo\":%d,"
            "\"tiempoRestante\":%lu,\"activo\":%s,\"terminado\":%s}",
            _partido->goles[0], _partido->goles[1], marcador, config.modoJuego,
            (unsigned long)restante,
            _partido->activo    ? "true" : "false",
            _partido->terminado ? "true" : "false");
    } else {
        snprintf(buf, sizeof(buf),
            "{\"goles\":[0,0],\"marcador\":\"0 - 0\",\"modo\":%d,"
            "\"tiempoRestante\":0,\"activo\":false,\"terminado\":false}",
            config.modoJuego);
    }
    server.send(200, "application/json", buf);
}

static void handleStart() {
    if (_partido) {
        _partido->resetear();
        _partido->activo    = true;
        _partido->terminado = false;
        Serial.println("[JUEGO] ¡Partido iniciado!");
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleConfigBrumeGet() {
    static char buf[900];
    snprintf(buf, sizeof(buf),
        "{"
        "\"intervaloComentariosMin\":%d,\"intervaloComentariosMax\":%d,\"intervaloStats\":%d,"
        "\"reglas\":{\"goleadaDiff\":%d,\"calienteGoles\":%d,"
          "\"inicioSegs\":%d,\"primerosMinsSegs\":%d,\"ultimoTramoPorc\":%d},"
        "\"comentarios\":{"
          "\"inicio\":{\"desde\":%d,\"hasta\":%d},"
          "\"primeros_minutos\":{\"desde\":%d,\"hasta\":%d},"
          "\"parejo\":{\"desde\":%d,\"hasta\":%d},"
          "\"caliente\":{\"desde\":%d,\"hasta\":%d},"
          "\"goleada\":{\"desde\":%d,\"hasta\":%d},"
          "\"definido\":{\"desde\":%d,\"hasta\":%d},"
          "\"ultimo_tramo\":{\"desde\":%d,\"hasta\":%d},"
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
        config.inicioSegs, config.primerosMinsSegs, config.ultimoTramoPorc,
        config.comentInicio.desde,        config.comentInicio.hasta,
        config.comentPrimerosMins.desde,  config.comentPrimerosMins.hasta,
        config.comentParejo.desde,        config.comentParejo.hasta,
        config.comentCaliente.desde,      config.comentCaliente.hasta,
        config.comentGoleada.desde,       config.comentGoleada.hasta,
        config.comentDefinido.desde,      config.comentDefinido.hasta,
        config.comentUltimoTramo.desde,   config.comentUltimoTramo.hasta,
        config.comentAburrido.desde,      config.comentAburrido.hasta,
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

// ── Init ─────────────────────────────────────────────────────────────────────

void webConfigInit(Partido* p) {
    _partido = p;
    cargarConfig();
    cargarWiFiCreds();

    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    IPAddress apIp = WiFi.softAPIP();

    // Intentar conexión STA si hay credenciales guardadas
    bool staOk = false;
    wl_status_t staStatus = WL_IDLE_STATUS;
    if (strlen(_staSSID) > 0) {
        Serial.printf("[WiFi] Conectando a '%s'", _staSSID);
        WiFi.begin(_staSSID, _staPass);
        for (uint8_t i = 0; i < 30; i++) {   // 15 segundos máximo
            delay(500);
            if (WiFi.status() == WL_CONNECTED) break;
            if (i % 4 == 3) Serial.print(".");
        }
        Serial.println();
        staOk     = WiFi.isConnected();
        staStatus = WiFi.status();
        if (staOk) anunciarSTA();
    }

    // Captive portal
    dns.start(53, "*", apIp);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/estado", HTTP_GET, handleEstado);
    server.on("/start",  HTTP_POST, handleStart);
    server.on("/configBrume", HTTP_GET, handleConfigBrumeGet);

    server.on("/wifiStatus", HTTP_GET, [](){
        String json = "{\"connected\":";
        json += WiFi.isConnected() ? "true" : "false";
        json += ",\"ssid\":\"" + String(_staSSID) + "\"";
        json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
        server.send(200, "application/json", json);
    });

    server.on("/wifi", HTTP_POST, [](){
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        ssid.trim();
        strlcpy(_staSSID, ssid.c_str(), sizeof(_staSSID));
        strlcpy(_staPass, pass.c_str(), sizeof(_staPass));
        guardarWiFiCreds(_staSSID, _staPass);
        WiFi.begin(_staSSID, _staPass);
        Serial.printf("[WiFi] Intentando conectar a '%s'...\n", _staSSID);
        server.send(200, "application/json",
            "{\"msg\":\"Conectando... revisá el serial en 10 segundos\"}");
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
    if (staOk) {
        // anunciarSTA() ya imprimió el banner STA completo
    } else if (strlen(_staSSID) > 0) {
        const char* razon;
        switch (staStatus) {
            case WL_NO_SSID_AVAIL:  razon = "SSID no encontrado en rango";  break;
            case WL_CONNECT_FAILED: razon = "contrasena incorrecta";         break;
            default:                razon = "timeout — sin respuesta";       break;
        }
        Serial.printf ("│  STA   : %-32s│\n", _staSSID);
        Serial.printf ("│  Error : %-32s│\n", razon);
        Serial.printf ("│  Status: %d                                │\n", (int)staStatus);
        Serial.println("│  Acceso: http://192.168.4.1/             │");
        Serial.println("└──────────────────────────────────────────┘");
    } else {
        Serial.println("│  STA   : sin configurar                  │");
        Serial.println("│  Acceso: http://192.168.4.1/             │");
        Serial.println("└──────────────────────────────────────────┘");
    }
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

    // Anunciar cuando el STA conecta (caso: conecta después del boot)
    if (!_staAnunciado && WiFi.isConnected())
        anunciarSTA();
    if (_staAnunciado && !WiFi.isConnected()) {
        _staAnunciado = false;
        Serial.println("[WiFi] STA desconectado, reintentando...");
        WiFi.reconnect();
    }
}
