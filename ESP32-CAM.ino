#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include "esp_timer.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= CONFIGURATION =================
// WiFi Credentials - BẠN CẦN SỬA PHẦN NÀY
const char* ssid = "Tang 3";      // WiFi của bạn
const char* password = "01111957v";  // Password WiFi

// Camera Model
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#endif

// Server Port
WebServer server(80);

// Camera Settings
int framesize = 10;    // SVGA (800x600)
int quality = 12;      // Lower quality for faster streaming
int brightness = 0;
int contrast = 0;
int saturation = 0;
bool flashState = false;

// System Variables
unsigned long startTime = 0;
unsigned long frameCount = 0;
unsigned long lastFrameTime = 0;
float fps = 0;
String publicIP = "";

// ================= CAMERA INITIALIZATION =================
void setupCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    // Lower resolution for better remote streaming
    config.frame_size = FRAMESIZE_SVGA;  // 800x600
    config.jpeg_quality = quality;
    config.fb_count = 1;  // Reduce to save memory
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    s->set_brightness(s, brightness);
    s->set_contrast(s, contrast);
    s->set_saturation(s, saturation);
    
    Serial.println("✓ Camera initialized");
}

// ================= GET PUBLIC IP =================
String getPublicIP() {
    HTTPClient http;
    String ip = "";
    
    http.begin("http://api.ipify.org");
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        ip = http.getString();
        Serial.println("Public IP: " + ip);
    } else {
        Serial.println("Failed to get public IP");
    }
    
    http.end();
    return ip;
}

// ================= STREAM HANDLER =================
void handleStream() {
    WiFiClient client = server.client();
    
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    response += "Pragma: no-cache\r\n";
    response += "Expires: 0\r\n";
    response += "\r\n";
    
    client.print(response);
    
    while (client.connected()) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            delay(100);
            continue;
        }
        
        String frame = "--frame\r\n";
        frame += "Content-Type: image/jpeg\r\n";
        frame += "Content-Length: " + String(fb->len) + "\r\n";
        frame += "\r\n";
        
        client.print(frame);
        client.write(fb->buf, fb->len);
        client.print("\r\n");
        
        frameCount++;
        unsigned long currentTime = millis();
        if (currentTime - lastFrameTime >= 1000) {
            fps = (frameCount * 1000.0) / (currentTime - lastFrameTime);
            frameCount = 0;
            lastFrameTime = currentTime;
        }
        
        esp_camera_fb_return(fb);
        
        // Add delay to control frame rate (reduce bandwidth)
        delay(50);  // ~20 FPS max
        
        // Check client connection
        if (!client.connected()) {
            break;
        }
    }
}

// ================= CAPTURE HANDLER ================= (ĐÃ SỬA)
void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    // Sử dụng WiFiClient để gửi dữ liệu nhị phân
    WiFiClient client = server.client();
    
    String header = "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: image/jpeg\r\n";
    header += "Content-Disposition: inline; filename=capture.jpg\r\n";
    header += "Access-Control-Allow-Origin: *\r\n";
    header += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    header += "Pragma: no-cache\r\n";
    header += "Expires: 0\r\n";
    header += "Content-Length: " + String(fb->len) + "\r\n";
    header += "\r\n";
    
    client.print(header);
    client.write(fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
}

// ================= STATUS HANDLER =================
void handleStatus() {
    String json = "{";
    json += "\"free_heap\":" + String(esp_get_free_heap_size());
    json += ",\"uptime\":" + String(millis() / 1000);
    json += ",\"fps\":" + String(fps, 1);
    json += ",\"framesize\":" + String(framesize);
    json += ",\"quality\":" + String(quality);
    json += ",\"brightness\":" + String(brightness);
    json += ",\"camera_name\":\"ESP32-CAM-Remote\"";
    json += ",\"flash_state\":" + String(flashState);
    json += ",\"local_ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"public_ip\":\"" + publicIP + "\"";
    json += ",\"wifi_rssi\":" + String(WiFi.RSSI());
    json += ",\"ssid\":\"" + String(ssid) + "\"";
    json += ",\"version\":\"2.0-remote\"";
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ================= CONTROL HANDLER =================
void handleControl() {
    String message = "";
    bool settingsChanged = false;
    
    if (server.hasArg("flash")) {
        flashState = server.arg("flash").toInt();
        pinMode(4, OUTPUT);
        digitalWrite(4, flashState ? HIGH : LOW);
        message += "Flash set to: " + String(flashState ? "ON" : "OFF");
        settingsChanged = true;
    }
    
    if (server.hasArg("quality")) {
        quality = constrain(server.arg("quality").toInt(), 1, 63);
        sensor_t *s = esp_camera_sensor_get();
        s->set_quality(s, quality);
        message += " Quality set to: " + String(quality);
        settingsChanged = true;
    }
    
    if (server.hasArg("brightness")) {
        brightness = constrain(server.arg("brightness").toInt(), -2, 2);
        sensor_t *s = esp_camera_sensor_get();
        s->set_brightness(s, brightness);
        message += " Brightness set to: " + String(brightness);
        settingsChanged = true;
    }
    
    if (server.hasArg("framesize")) {
        framesize = constrain(server.arg("framesize").toInt(), 7, 12);
        sensor_t *s = esp_camera_sensor_get();
        s->set_framesize(s, (framesize_t)framesize);
        message += " Resolution set to: " + String(framesize);
        settingsChanged = true;
    }
    
    String json = "{";
    json += "\"success\":" + String(settingsChanged ? "true" : "false") + ",";
    json += "\"message\":\"" + message + "\",";
    json += "\"settings\":{";
    json += "\"quality\":" + String(quality) + ",";
    json += "\"brightness\":" + String(brightness) + ",";
    json += "\"framesize\":" + String(framesize) + ",";
    json += "\"flash\":" + String(flashState);
    json += "}}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ================= REBOOT HANDLER =================
void handleReboot() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"message\":\"Rebooting...\"}");
    delay(100);
    ESP.restart();
}

// ================= WIFI SCAN HANDLER =================
void handleWifiScan() {
    String json = "[";
    int n = WiFi.scanNetworks();
    
    for (int i = 0; i < n; i++) {
        if (i) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"channel\":" + String(WiFi.channel(i)) + ",";
        json += "\"encryption\":" + String(WiFi.encryptionType(i));
        json += "}";
    }
    json += "]";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ================= INFO HANDLER =================
void handleInfo() {
    String info = "ESP32-CAM Remote Access Server\n";
    info += "=============================\n";
    info += "Local IP: " + WiFi.localIP().toString() + "\n";
    info += "Public IP: " + publicIP + "\n";
    info += "mDNS: esp32-cam.local\n";
    info += "SSID: " + String(ssid) + "\n";
    info += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    info += "Uptime: " + String(millis() / 1000) + "s\n";
    info += "Free Heap: " + String(esp_get_free_heap_size()) + " bytes\n";
    info += "FPS: " + String(fps, 1) + "\n";
    info += "\nAvailable Endpoints:\n";
    info += "  /stream    - Live video stream\n";
    info += "  /capture   - Single image capture\n";
    info += "  /status    - JSON status\n";
    info += "  /control   - Control camera\n";
    info += "  /wifi-scan - Scan WiFi networks\n";
    info += "  /reboot    - Reboot camera\n";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", info);
}

// ================= SIMPLE HTML PAGE =================
void handleRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>ESP32-CAM Remote</title>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#f5f5f5;}";
    html += ".container{max-width:1000px;margin:auto;background:white;padding:20px;border-radius:15px;box-shadow:0 5px 20px rgba(0,0,0,0.1);}";
    html += ".header{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:20px;border-radius:10px 10px 0 0;}";
    html += ".video-container{background:#000;padding:5px;margin:20px 0;text-align:center;border-radius:10px;}";
    html += "img{max-width:100%;height:auto;border-radius:8px;}";
    html += ".btn{padding:12px 20px;margin:8px;border:none;border-radius:8px;cursor:pointer;font-weight:bold;transition:0.3s;}";
    html += ".btn-primary{background:#4a6fa5;color:white;}";
    html += ".btn-primary:hover{background:#166088;transform:translateY(-2px);}";
    html += ".btn-danger{background:#dc3545;color:white;}";
    html += ".btn-success{background:#28a745;color:white;}";
    html += ".btn-warning{background:#ffc107;color:#333;}";
    html += ".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin:20px 0;}";
    html += ".stat-card{background:#f8f9fa;padding:15px;border-radius:10px;border-left:4px solid #4a6fa5;}";
    html += ".remote-access{background:#e3f2fd;padding:20px;border-radius:10px;margin:20px 0;}";
    html += ".api-list{background:#f8f9fa;padding:15px;border-radius:10px;}";
    html += "code{background:#eee;padding:2px 5px;border-radius:3px;font-family:monospace;}";
    html += "</style>";
    html += "<script>";
    html += "function updateStats(){";
    html += "fetch('/status').then(r=>r.json()).then(data=>{";
    html += "document.getElementById('fps').innerHTML=data.fps.toFixed(1);";
    html += "document.getElementById('uptime').innerHTML=data.uptime;";
    html += "document.getElementById('heap').innerHTML=Math.round(data.free_heap/1024)+' KB';";
    html += "document.getElementById('rssi').innerHTML=data.wifi_rssi+' dBm';";
    html += "document.getElementById('public-ip').innerHTML=data.public_ip||'Not available';";
    html += "});}";
    html += "setInterval(updateStats,5000);";
    html += "</script>";
    html += "</head><body>";
    
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<h1>📡 ESP32-CAM Remote Access</h1>";
    html += "<p>Access your camera from anywhere</p>";
    html += "</div>";
    
    html += "<div class='stats'>";
    html += "<div class='stat-card'><strong>📶 WiFi RSSI</strong><br><span id='rssi'>" + String(WiFi.RSSI()) + " dBm</span></div>";
    html += "<div class='stat-card'><strong>⏱️ Uptime</strong><br><span id='uptime'>" + String(millis()/1000) + "s</span></div>";
    html += "<div class='stat-card'><strong>📊 FPS</strong><br><span id='fps'>0.0</span></div>";
    html += "<div class='stat-card'><strong>💾 Free Heap</strong><br><span id='heap'>" + String(esp_get_free_heap_size()/1024) + " KB</span></div>";
    html += "</div>";
    
    html += "<div class='video-container'>";
    html += "<img src='/stream' alt='Live Stream' onerror=\"this.style.display='none';document.getElementById('stream-error').style.display='block'\">";
    html += "<div id='stream-error' style='display:none;color:white;padding:50px;'>Stream not available. Try /capture</div>";
    html += "</div>";
    
    html += "<div style='text-align:center;margin:20px 0;'>";
    html += "<button class='btn btn-primary' onclick=\"window.open('/capture?t='+Date.now(), '_blank')\">📸 Capture Image</button>";
    html += "<button class='btn btn-warning' onclick=\"if(confirm('Reboot camera?')){fetch('/reboot');alert('Camera will reboot...');}\">🔄 Reboot</button>";
    html += "<button class='btn btn-success' onclick=\"fetch('/capture?t='+Date.now()).then(r=>r.blob()).then(b=>{let a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='esp32-capture-'+new Date().toISOString()+'.jpg';a.click();})\">💾 Download</button>";
    html += "<button class='btn btn-danger' onclick=\"flashToggle=!flashToggle;fetch('/control?flash='+(flashToggle?1:0))\">💡 Toggle Flash</button>";
    html += "</div>";
    
    html += "<div class='remote-access'>";
    html += "<h3>🌐 Remote Access Methods:</h3>";
    html += "<p><strong>Local Access:</strong> <code>" + WiFi.localIP().toString() + "</code> or <code>esp32-cam.local</code></p>";
    html += "<p><strong>Public IP:</strong> <span id='public-ip'>" + publicIP + "</span></p>";
    html += "<p><strong>For GitHub Pages:</strong> Use your public IP with port forwarding or cloud relay</p>";
    html += "</div>";
    
    html += "<div class='api-list'>";
    html += "<h3>🔧 API Endpoints:</h3>";
    html += "<ul>";
    html += "<li><a href='/stream' target='_blank'><code>/stream</code></a> - Live video stream (MJPEG)</li>";
    html += "<li><a href='/capture' target='_blank'><code>/capture</code></a> - Single JPEG image</li>";
    html += "<li><a href='/status' target='_blank'><code>/status</code></a> - Camera status (JSON)</li>";
    html += "<li><code>/control?quality=10&brightness=0&flash=1</code> - Control settings</li>";
    html += "<li><a href='/wifi-scan' target='_blank'><code>/wifi-scan</code></a> - Scan WiFi networks</li>";
    html += "<li><a href='/reboot' target='_blank'><code>/reboot</code></a> - Reboot camera</li>";
    html += "</ul>";
    
    html += "<h3>⚡ Quick Commands:</h3>";
    html += "<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:10px;'>";
    html += "<button class='btn btn-primary' onclick=\"fetch('/control?quality=5')\">🎯 High Quality (5)</button>";
    html += "<button class='btn btn-primary' onclick=\"fetch('/control?quality=20')\">🚀 Fast Stream (20)</button>";
    html += "<button class='btn btn-primary' onclick=\"fetch('/control?brightness=2')\">🔆 Max Brightness</button>";
    html += "<button class='btn btn-primary' onclick=\"fetch('/control?brightness=-2')\">🌙 Min Brightness</button>";
    html += "<button class='btn btn-primary' onclick=\"fetch('/control?framesize=7')\">📱 Low Res (320x240)</button>";
    html += "<button class='btn btn-primary' onclick=\"fetch('/control?framesize=10')\">🖥️ Medium Res (800x600)</button>";
    html += "</div>";
    html += "</div>";
    
    html += "<div style='margin-top:30px;padding-top:20px;border-top:1px solid #ddd;color:#666;text-align:center;'>";
    html += "<p>ESP32-CAM Remote Access v2.0 | Public IP: " + publicIP + "</p>";
    html += "<p>Use with GitHub Pages: <a href='https://qmai8.github.io/esp32' target='_blank'>qmai8.github.io/esp32</a></p>";
    html += "</div>";
    
    html += "</div>"; // container
    html += "<script>updateStats();</script>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// ================= SETUP =================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("   ESP32-CAM REMOTE ACCESS SERVER");
    Serial.println("========================================");
    
    // Setup LED flash pin
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    
    // Initialize camera
    Serial.println("Initializing camera...");
    setupCamera();
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi connected!");
        Serial.print("  Local IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("  RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        
        // Get public IP
        Serial.println("Getting public IP...");
        publicIP = getPublicIP();
        
        // Setup mDNS
        if (MDNS.begin("esp32-cam")) {
            Serial.println("✓ mDNS: esp32-cam.local");
        } else {
            Serial.println("✗ mDNS failed!");
        }
    } else {
        Serial.println("✗ WiFi failed! Starting AP mode...");
        WiFi.softAP("ESP32-CAM-REMOTE", "12345678");
        Serial.print("  AP IP: ");
        Serial.println(WiFi.softAPIP());
        publicIP = "AP Mode Only";
    }
    
    // Setup server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/control", HTTP_GET, handleControl);
    server.on("/reboot", HTTP_GET, handleReboot);
    server.on("/wifi-scan", HTTP_GET, handleWifiScan);
    server.on("/info", HTTP_GET, handleInfo);
    
    // Enable CORS for all routes
    server.onNotFound([]() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(404, "text/plain", "Not Found");
    });
    
    // Start server
    server.begin();
    Serial.println("✓ HTTP server started on port 80");
    
    startTime = millis();
    lastFrameTime = millis();
    
    Serial.println("\n📡 AVAILABLE ENDPOINTS:");
    Serial.println("  Local:  http://" + WiFi.localIP().toString());
    Serial.println("  mDNS:   http://esp32-cam.local");
    Serial.println("  Public: http://" + publicIP + " (if port forwarded)");
    Serial.println("\n🔗 For GitHub Pages:");
    Serial.println("  Use URL: http://" + publicIP + ":80");
    Serial.println("  Share: https://qmai8.github.io/esp32?camera=" + publicIP + "&port=80");
    Serial.println("========================================\n");
}

// ================= MAIN LOOP =================
void loop() {
    server.handleClient();
    
    // Periodic tasks
    static unsigned long lastUpdateTime = 0;
    static unsigned long lastStatsTime = 0;
    unsigned long currentTime = millis();
    
    // Update public IP every 5 minutes
    if (currentTime - lastUpdateTime > 300000 && WiFi.status() == WL_CONNECTED) {
        publicIP = getPublicIP();
        lastUpdateTime = currentTime;
    }
    
    // Print stats every 60 seconds
    if (currentTime - lastStatsTime > 60000) {
        Serial.printf("[STATS] Heap: %dKB | FPS: %.1f | Uptime: %dmin | RSSI: %ddBm\n",
                     esp_get_free_heap_size() / 1024,
                     fps,
                     millis() / 60000,
                     WiFi.RSSI());
        
        Serial.printf("[IP] Local: %s | Public: %s\n", 
                     WiFi.localIP().toString().c_str(),
                     publicIP.c_str());
        
        lastStatsTime = currentTime;
    }
    
    delay(1);
}
