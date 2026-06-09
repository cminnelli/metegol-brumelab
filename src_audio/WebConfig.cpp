#include "WebConfig.h"
#include "AudioVoz.h"
#include "AudioAmbiente.h"
#include <Arduino.h>
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
      <input type="range" name="golesMax" min="1" max="10" value="%GOLES_MAX%"
             oninput="document.getElementById('gm').textContent=this.value">
    </div>
    <div class="field" id="row-tiempo">
      <label>Duración (minutos) <span id="dm">%DUR_MIN%</span></label>
      <input type="range" name="duracionMin" min="1" max="30" value="%DUR_MIN%"
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
    return html;
}

static void handleRoot() {
    server.send(200, "text/html", buildPage());
}

static void handleSave() {
    if (server.hasArg("volumenVoz"))      config.volumenVoz      = server.arg("volumenVoz").toInt();
    if (server.hasArg("volumenAmbiente")) config.volumenAmbiente = server.arg("volumenAmbiente").toInt();
    if (server.hasArg("modoJuego"))       config.modoJuego       = server.arg("modoJuego").toInt();
    if (server.hasArg("golesMax"))        config.golesMax        = server.arg("golesMax").toInt();
    if (server.hasArg("duracionMin"))     config.duracionMin     = server.arg("duracionMin").toInt();
    if (server.hasArg("brillo"))          config.brillo          = server.arg("brillo").toInt();
    if (server.hasArg("velocidadScroll")) config.velocidadScroll = server.arg("velocidadScroll").toInt();
    if (server.hasArg("pistaAmbiente"))   config.pistaAmbiente   = server.arg("pistaAmbiente").toInt();

    guardarConfig();
    _pendingVolUpdate = true;  // aplica volumen en el próximo loop, no aquí
    Serial.printf("[Config] Guardada — SP1 vol:%d SP2 vol:%d modo:%s\n",
        config.volumenVoz, config.volumenAmbiente,
        config.modoJuego == 0 ? "goles" : "tiempo");
    server.send(200, "application/json", "{\"ok\":true}");
}

// ── Init ─────────────────────────────────────────────────────────────────────

void webConfigInit() {
    cargarConfig();
    cargarWiFiCreds();

    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WiFi] AP: %s — IP: %s\n", WIFI_SSID, ip.toString().c_str());

    // Intentar conexión STA si hay credenciales guardadas
    if (strlen(_staSSID) > 0) {
        Serial.printf("[WiFi] Conectando a '%s'...\n", _staSSID);
        WiFi.begin(_staSSID, _staPass);
        uint8_t intentos = 0;
        while (WiFi.status() != WL_CONNECTED && intentos < 20) {
            delay(500); intentos++;
        }
        if (WiFi.isConnected())
            Serial.printf("[WiFi] STA conectado — IP: %s\n", WiFi.localIP().toString().c_str());
        else
            Serial.println("[WiFi] STA no disponible, solo AP activo");
    }

    // Captive portal: redirige cualquier dominio al ESP32
    dns.start(53, "*", ip);
    Serial.println("[WiFi] DNS captive portal activo");

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);

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
        // Solo redirigir si parece un captive portal probe (host != 192.168.4.1)
        String host = server.hostHeader();
        if (host.length() > 0 && !host.startsWith("192.168.4.1")) {
            server.sendHeader("Location", "http://192.168.4.1/");
            server.send(302);
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    server.begin();
    Serial.println("[WiFi] Servidor HTTP listo");
}

void webConfigLoop() {
    dns.processNextRequest();
    server.handleClient();

    if (_pendingVolUpdate) {
        _pendingVolUpdate = false;
        vozSetVolumen(config.volumenVoz);
        ambienteSetVolumen(config.volumenAmbiente);
    }

    // Anunciar cuando el STA conecta
    static bool _staAnunciado = false;
    if (!_staAnunciado && WiFi.isConnected()) {
        _staAnunciado = true;
        MDNS.begin("metegol");
        MDNS.addService("http", "tcp", 80);
        Serial.println("╔══════════════════════════════════════════╗");
        Serial.printf ("║  IP local  : http://%s\n", WiFi.localIP().toString().c_str());
        Serial.println("║  mDNS      : http://metegol.local        ║");
        Serial.println("╚══════════════════════════════════════════╝");
    }
    if (_staAnunciado && !WiFi.isConnected()) {
        _staAnunciado = false;
        Serial.println("[WiFi] STA desconectado, reintentando...");
        WiFi.reconnect();
    }
}
