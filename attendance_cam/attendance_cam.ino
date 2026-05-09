#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <HTTPClient.h>

// =====================================================
//                   CONFIGURATION
// =====================================================
const char* PI_SERVER_URL = "http://192.168.4.2:5000/upload";
const char* WIFI_SSID     = "ESP32-Camera";
const char* WIFI_PASS     = "12345678";

// =====================================================
//              CAMERA PINS - Freenove ESP32-S3-WROOM
// =====================================================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// =====================================================
//              HTTP SERVER HANDLES
// =====================================================
httpd_handle_t ui_httpd     = NULL;   // port 80 — web page + capture
httpd_handle_t stream_httpd = NULL;   // port 81 — MJPEG stream

#define STREAM_BOUNDARY    "mjpegstream"
#define STREAM_PART        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

// =====================================================
//                   WEB PAGE HTML
// =====================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Attendance Cam</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background-color: #1a1a1a;
      text-align: center;
      color: white;
      font-family: Helvetica, Arial, sans-serif;
      padding: 20px;
    }
    h2 { margin-bottom: 6px; font-size: 26px; letter-spacing: 1px; }
    .subtitle { color: #aaa; font-size: 13px; margin-bottom: 15px; }
    #video {
      width: 100%;
      max-width: 420px;
      transform: rotate(180deg);
      border: 2px solid #444;
      border-radius: 10px;
      display: block;
      margin: 0 auto;
      background: #000;
      min-height: 200px;
    }
    .btn {
      background-color: #007bff;
      border: none;
      color: white;
      padding: 18px 40px;
      font-size: 22px;
      font-weight: bold;
      margin: 20px auto 10px auto;
      cursor: pointer;
      border-radius: 50px;
      width: 80%;
      max-width: 300px;
      display: block;
      box-shadow: 0 5px #0056b3;
      transition: all 0.1s;
    }
    .btn:active  { background-color: #0056b3; box-shadow: none; transform: translateY(4px); }
    .btn:disabled { background-color: #555; box-shadow: 0 5px #333; cursor: not-allowed; }
    #status {
      font-size: 20px;
      font-weight: bold;
      margin-top: 12px;
      min-height: 32px;
      color: #00ff00;
    }
    #log {
      margin: 12px auto;
      max-width: 420px;
      background: #111;
      border: 1px solid #333;
      border-radius: 8px;
      padding: 10px;
      font-size: 13px;
      color: #aaa;
      text-align: left;
      min-height: 60px;
      max-height: 140px;
      overflow-y: auto;
    }
  </style>
</head>
<body>
  <h2>Attendance Cam</h2>
  <p class="subtitle">Freenove ESP32-S3-WROOM</p>

  <img src="http://192.168.4.1:81/stream" id="video"
       onerror="this.style.border='2px solid red'" >

  <p id="status">Ready</p>

  <button class="btn" id="captureBtn" onclick="capture()">CAPTURE</button>

  <div id="log">Waiting for capture...</div>

  <script>
    function addLog(msg) {
      var log = document.getElementById('log');
      var d   = document.createElement('div');
      d.textContent = new Date().toLocaleTimeString() + ' > ' + msg;
      log.appendChild(d);
      log.scrollTop = log.scrollHeight;
    }

    function setStatus(msg, color) {
      var el = document.getElementById('status');
      el.innerHTML   = msg;
      el.style.color = color;
    }

    function capture() {
      var btn = document.getElementById('captureBtn');
      setStatus('Capturing...', 'yellow');
      btn.disabled = true;
      addLog('Capturing photo...');

      var xhr = new XMLHttpRequest();
      xhr.timeout = 30000;

      xhr.onreadystatechange = function() {
        if (this.readyState !== 4) return;
        btn.disabled = false;
        if (this.status === 200) {
          var r = this.responseText.trim();
          setStatus(r, '#00ff00');
          addLog('Pi: ' + r);
        } else {
          setStatus('Error: ' + this.responseText, 'red');
          addLog('Error: ' + this.responseText);
        }
        setTimeout(function(){ setStatus('Ready', '#00ff00'); }, 3000);
      };

      xhr.ontimeout = function() {
        btn.disabled = false;
        setStatus('Timeout - Pi took too long', 'red');
        addLog('Timeout after 30s');
      };

      xhr.onerror = function() {
        btn.disabled = false;
        setStatus('Connection Error', 'red');
        addLog('Connection error');
      };

      xhr.open('GET', '/capture', true);
      xhr.send();
      addLog('Request sent to ESP32...');
    }
  </script>
</body>
</html>
)rawliteral";

// =====================================================
//                   HANDLERS
// =====================================================

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_handler(httpd_req_t *req) {
  Serial.println("-----------------------------");
  Serial.println("[ESP32] Capture request received");

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[ESP32] ERROR: Camera capture failed!");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  Serial.printf("[ESP32] Photo: %u bytes (%.1f KB)\n", fb->len, fb->len / 1024.0f);
  Serial.printf("[ESP32] Sending to Pi: %s\n", PI_SERVER_URL);

  HTTPClient http;
  http.begin(PI_SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(30000);

  int code = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  String reply;
  if (code > 0) {
    reply = http.getString();
    Serial.printf("[ESP32] Pi replied (%d): %s\n", code, reply.c_str());
    httpd_resp_send(req, reply.c_str(), HTTPD_RESP_USE_STRLEN);
  } else {
    reply = "Send Failed: " + String(code);
    Serial.printf("[ESP32] ERROR: %s\n", reply.c_str());
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_send(req, reply.c_str(), HTTPD_RESP_USE_STRLEN);
  }

  http.end();
  Serial.println("-----------------------------");
  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb  = NULL;
  esp_err_t    res = ESP_OK;
  char         hdr[128];

  res = httpd_resp_set_type(req,
        "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ESP32] Stream: frame capture failed");
      res = ESP_FAIL;
    } else {
      size_t hlen = snprintf(hdr, sizeof(hdr), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, hdr, hlen);
      if (res == ESP_OK)
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      if (res == ESP_OK)
        res = httpd_resp_send_chunk(req, "\r\n", 2);
      esp_camera_fb_return(fb);
    }
    if (res != ESP_OK) break;
    delay(30);   // ~33 fps cap
  }

  Serial.println("[ESP32] Stream ended");
  return res;
}

// =====================================================
//               START BOTH HTTP SERVERS
// =====================================================
void startCameraServer() {
  // --- UI server — port 80 ---
  httpd_config_t ui_cfg  = HTTPD_DEFAULT_CONFIG();
  ui_cfg.server_port     = 80;
  ui_cfg.ctrl_port       = 32768;

  httpd_uri_t idx_uri = { "/",       HTTP_GET, index_handler,   NULL };
  httpd_uri_t cap_uri = { "/capture",HTTP_GET, capture_handler, NULL };

  if (httpd_start(&ui_httpd, &ui_cfg) == ESP_OK) {
    httpd_register_uri_handler(ui_httpd, &idx_uri);
    httpd_register_uri_handler(ui_httpd, &cap_uri);
    Serial.println("[ESP32] UI     server on port 80");
  } else {
    Serial.println("[ESP32] ERROR: UI server failed to start!");
  }

  // --- Stream server — port 81 ---
  httpd_config_t st_cfg  = HTTPD_DEFAULT_CONFIG();
  st_cfg.server_port     = 81;
  st_cfg.ctrl_port       = 32769;

  httpd_uri_t str_uri = { "/stream", HTTP_GET, stream_handler,  NULL };

  if (httpd_start(&stream_httpd, &st_cfg) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &str_uri);
    Serial.println("[ESP32] Stream server on port 81");
  } else {
    Serial.println("[ESP32] ERROR: Stream server failed to start!");
  }
}

// =====================================================
//                      SETUP
// =====================================================
void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(300);

  Serial.println("\n=============================");
  Serial.println("  ESP32-S3 Attendance Cam");
  Serial.println("  Freenove ESP32-S3-WROOM");
  Serial.println("=============================");

  // ---------- Camera config ----------
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.grab_mode    = CAMERA_GRAB_LATEST;   // always freshest frame

  if (psramFound()) {
    Serial.println("[ESP32] PSRAM found - max quality");
    cfg.frame_size   = FRAMESIZE_UXGA;
    cfg.jpeg_quality = 4;
    cfg.fb_count     = 2;
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("[ESP32] No PSRAM - fallback QVGA");
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 10;
    cfg.fb_count     = 1;
    cfg.fb_location  = CAMERA_FB_IN_DRAM;
  }

  // ---------- Init camera ----------
  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[ESP32] ERROR: Camera init failed (0x%x)\n", err);
    return;
  }
  Serial.println("[ESP32] Camera OK");

  // ---------- Sensor tweaks ----------
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize    (s, FRAMESIZE_UXGA);
  s->set_quality      (s, 4);
  s->set_brightness   (s, 1);
  s->set_contrast     (s, 1);
  s->set_saturation   (s, 0);
  s->set_whitebal     (s, 1);
  s->set_awb_gain     (s, 1);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2         (s, 1);

  // ---------- WiFi AP ----------
  WiFi.softAP(WIFI_SSID, WIFI_PASS, 6);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.printf("[ESP32] WiFi AP: %s | IP: %s\n",
                WIFI_SSID, WiFi.softAPIP().toString().c_str());

  // ---------- HTTP servers ----------
  startCameraServer();

  Serial.println("[ESP32] Ready!");
  Serial.println("[ESP32] Web UI : http://192.168.4.1");
  Serial.println("[ESP32] Stream : http://192.168.4.1:81/stream");
  Serial.printf ("[ESP32] Pi URL : %s\n", PI_SERVER_URL);
  Serial.println("=============================");
}

// =====================================================
//                      LOOP
// =====================================================
void loop() {
  delay(1);
}
