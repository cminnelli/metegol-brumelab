#include "WebConfig.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include "Comentarista.h"
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
    config.intervaloStats          = prefs.getUShort("intervStats",    3);
    config.goleadaDiff             = prefs.getUChar("goleadaDiff",    3);
    config.calienteGoles           = prefs.getUChar("calienteGol",    4);
    config.inicioSegs              = prefs.getUShort("inicioSegs",   30);
    config.primerosMinsSegs        = prefs.getUShort("primMinsSegs", 120);
    config.ultimoTramoSegs         = prefs.getUShort("ultiTramoSeg",  60);
    config.umbralAburridoSegs      = prefs.getUShort("umbralAbur",  180);
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
</style>
</head>
<body>
<div class="wrap">
<header>
  <h1>⚽ Metegol</h1>
  <p>Sistema de comentarios</p>
</header>

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
    <div class="field">
      <label>Pista <b id="pa">%PISTA_AMB%</b></label>
      <input type="range" name="pistaAmbiente" min="1" max="3" value="%PISTA_AMB%" oninput="sl(this,'pa')">
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
      <label>Primeros minutos seg <b id="pms">%PRIM_MINS_SEGS%</b></label>
      <input type="range" name="primerosMinsSegs" min="5" max="180" value="%PRIM_MINS_SEGS%" oninput="sl(this,'pms')">
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
            <tr><td><span class="rl"><span class="rd"></span>último tramo</span></td><td><input class="ni" type="number" name="cUtD" min="0" max="255" value="%C_UT_D%"></td><td><input class="ni" type="number" name="cUtH" min="0" max="255" value="%C_UT_H%"></td></tr>
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

</div>
<div class="save-bar">
  <button type="submit" class="btn-save">GUARDAR CONFIGURACIÓN</button>
</div>
</form>

<div class="grid" style="margin-top:14px">
  <div class="card cw" id="wifi-card">
    <h2>📶 Acceso WiFi</h2>
    <div id="state-disconnected">
      <div style="background:rgba(105,240,174,.06);border:1px solid rgba(105,240,174,.15);border-radius:10px;padding:13px;margin-bottom:14px">
        <p style="font-size:.86rem;color:var(--text);margin-bottom:5px"><b>Conectate a tu red</b></p>
        <p style="font-size:.78rem;color:var(--muted);line-height:1.6">AP activo: <b style="color:var(--text)">Metegol</b>. Con tu WiFi accedés por <b style="color:var(--accent)">metegol.local</b> sin cambiar de red.</p>
      </div>
      <div class="field">
        <label>Red (SSID)</label>
        <input type="text" id="wSSID" placeholder="Nombre de tu WiFi" style="width:100%;padding:9px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.88rem;outline:none">
      </div>
      <div class="field">
        <label>Contraseña</label>
        <input type="password" id="wPass" placeholder="••••••" style="width:100%;padding:9px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.88rem;outline:none">
      </div>
      <button type="button" onclick="conectarWifi()" style="width:100%;padding:11px;background:linear-gradient(90deg,var(--purple),#651fff);border:none;border-radius:10px;color:#fff;font-weight:700;font-size:.88rem;cursor:pointer">CONECTAR</button>
      <div id="wifi-msg" style="font-size:.78rem;color:var(--muted);margin-top:10px;text-align:center;min-height:18px"></div>
    </div>
    <div id="state-connected" style="display:none">
      <div style="text-align:center;padding:6px 0 14px">
        <div style="font-size:2rem;margin-bottom:7px">✅</div>
        <p style="color:var(--muted);font-size:.83rem;margin-bottom:4px">Conectado a <b id="ssid-label" style="color:var(--text)"></b></p>
        <p style="color:var(--muted);font-size:.78rem;margin-bottom:14px">Accedé desde cualquier dispositivo:</p>
        <a href="http://metegol.local" target="_blank" style="display:inline-block;padding:11px 26px;background:linear-gradient(90deg,var(--accent),#0097a7);border-radius:28px;color:#000;font-weight:700;text-decoration:none">🌐 metegol.local</a>
      </div>
      <div style="border-top:1px solid var(--border);padding-top:11px;text-align:center">
        <button type="button" onclick="mostrarCambioRed()" style="background:transparent;border:1px solid var(--border);color:var(--muted);padding:7px 18px;border-radius:20px;font-size:.78rem;cursor:pointer">Cambiar red</button>
      </div>
      <div id="cambio-red" style="display:none;margin-top:12px">
        <div class="field"><label>Nueva red</label><input type="text" id="wSSID2" placeholder="SSID" style="width:100%;padding:9px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.88rem;outline:none"></div>
        <div class="field"><label>Contraseña</label><input type="password" id="wPass2" placeholder="••••••" style="width:100%;padding:9px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.88rem;outline:none"></div>
        <button type="button" onclick="cambiarRed()" style="width:100%;padding:10px;background:linear-gradient(90deg,var(--purple),#651fff);border:none;border-radius:10px;color:#fff;font-weight:700;cursor:pointer">CAMBIAR RED</button>
        <div id="wifi-msg2" style="font-size:.78rem;color:var(--muted);margin-top:10px;text-align:center"></div>
      </div>
    </div>
  </div>
</div>
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
      document.getElementById('state-disconnected').style.display=d.connected?'none':'block';
      document.getElementById('state-connected').style.display=d.connected?'block':'none';
      if(d.connected)document.getElementById('ssid-label').textContent=d.ssid;
    }).catch(()=>{});
  }
  function mostrarCambioRed(){const c=document.getElementById('cambio-red');c.style.display=c.style.display==='none'?'block':'none';}
  function _doConectar(ssid,pass,msgEl){
    if(!ssid){msgEl.textContent='Ingresá el SSID';return;}
    msgEl.style.color='var(--accent)';msgEl.textContent='Conectando...';
    fetch('/wifi',{method:'POST',body:new URLSearchParams({ssid,pass})}).then(r=>r.json()).then(()=>{
      let n=0;const t=setInterval(()=>{
        n++;msgEl.textContent='Conectando'+'.'.repeat(n%4);
        fetch('/wifiStatus').then(r=>r.json()).then(d=>{
          if(d.connected){clearInterval(t);msgEl.textContent='';actualizarEstadoWiFi();}
          else if(n>20){clearInterval(t);msgEl.style.color='var(--pink)';msgEl.textContent='No se pudo conectar.';}
        }).catch(()=>{});
      },1500);
    }).catch(()=>{msgEl.textContent='Error';});
  }
  function conectarWifi(){_doConectar(document.getElementById('wSSID').value.trim(),document.getElementById('wPass').value,document.getElementById('wifi-msg'));}
  function cambiarRed(){_doConectar(document.getElementById('wSSID2').value.trim(),document.getElementById('wPass2').value,document.getElementById('wifi-msg2'));}
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
    html.replace("%ULTI_TRAMO%",     String(config.ultimoTramoSegs));
    html.replace("%PRIM_MINS_SEGS%", String(config.primerosMinsSegs));
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
    if (server.hasArg("primerosMinsSegs"))     config.primerosMinsSegs     = server.arg("primerosMinsSegs").toInt();
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
            Serial.println("[JUEGO] Partido reanudado (web)");
        } else {
            comentaristaReiniciar();
            _partido->resetear();
            _partido->activo    = true;
            _partido->terminado = false;
            Serial.println("[JUEGO] ¡Partido iniciado!");
            ambientePlay(config.pistaAmbiente);
        }
    } else {
        ambientePlay(config.pistaAmbiente);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStop() {
    if (_partido) {
        _partido->activo    = false;
        _partido->terminado = true;
        Serial.println("[JUEGO] Partido detenido manualmente.");
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleConfigBrumeGet() {
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
        config.primerosMinsSegs, config.ultimoTramoSegs, config.umbralAburridoSegs,
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
    server.on("/stop",   HTTP_POST, handleStop);
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
