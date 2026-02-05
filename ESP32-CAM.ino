/*
 * ======================================================
 * ESP32-CAM REMOTE ACCESS SERVER v3.0
 * Tối ưu cho ESP32-CAM Viewer & Ngrok
 * Author: ESP32-CAM Remote
 * Date: 2024
 * ======================================================
 * 
 * ĐẶC ĐIỂM:
 * 1. Hỗ trợ CORS đầy đủ cho ESP32-CAM Viewer
 * 2. Stream ổn định với FPS có thể điều chỉnh
 * 3. Tự động lấy Public IP
 * 4. Giao diện web responsive
 * 5. Hỗ trợ ngrok hoàn hảo
 * 
 * KẾT NỐI CHÂN ESP32-CAM AI-Thinker:
 * FLASH LED: GPIO 4
 * PSRAM: KẾT NỐI ĐỂ HOẠT ĐỘNG TỐT
 * ======================================================
 */

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

// ================= CẤU HÌNH WIFI =================
// ⚠️ BẠN PHẢI SỬA 2 DÒNG NÀY ⚠️
const char* ssid = "Tang 3";           // Tên WiFi của bạn
const char* password = "01111957v";    // Mật khẩu WiFi

// ================= CẤU HÌNH CAMERA =================
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
// Định nghĩa chân camera AI-Thinker
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

// ================= BIẾN TOÀN CỤC =================
WebServer server(80);  // Server trên cổng 80

// Cài đặt camera mặc định
int framesize = 9;     // VGA (640x480) - TỐI ƯU CHO REMOTE
int quality = 12;      // Chất lượng (1-63, càng nhỏ càng tốt)
int brightness = 0;    // Độ sáng (-2 đến 2)
int contrast = 0;      // Độ tương phản (-2 đến 2)
int saturation = 0;    // Độ bão hòa (-2 đến 2)
bool flashState = false;  // Trạng thái đèn flash

// Biến hệ thống
unsigned long startTime = 0;
unsigned long frameCount = 0;
unsigned long lastFrameTime = 0;
float fps = 0;
String publicIP = "Đang lấy...";
String ngrokURL = "";

// ================= KHAI BÁO HÀM =================
void setupCamera();
String getPublicIP();
void handleRoot();
void handleStream();
void handleCapture();
void handleStatus();
void handleControl();
void handleReboot();
void handleWifiScan();
void handleInfo();
void handleOptions();
void sendCORSHeaders();
void printSystemInfo();

// ================= THIẾT LẬP CAMERA =================
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
    
    // CẤU HÌNH TỐI ƯU CHO REMOTE STREAMING
    config.frame_size = FRAMESIZE_VGA;    // 640x480 - TỐI ƯU BĂNG THÔNG
    config.jpeg_quality = quality;        // Chất lượng ảnh
    config.fb_count = 2;                  // 2 frame buffer để ổn định
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Lỗi khởi tạo camera: 0x%x\n", err);
        delay(1000);
        ESP.restart();
    }
    
    // Cài đặt thông số camera
    sensor_t *s = esp_camera_sensor_get();
    s->set_brightness(s, brightness);
    s->set_contrast(s, contrast);
    s->set_saturation(s, saturation);
    
    Serial.println("✅ Camera đã khởi tạo (640x480)");
}

// ================= LẤY PUBLIC IP =================
String getPublicIP() {
    if (WiFi.status() != WL_CONNECTED) {
        return "Không kết nối WiFi";
    }
    
    HTTPClient http;
    String ip = "";
    
    // Thử nhiều dịch vụ IP
    String services[] = {
        "http://api.ipify.org",
        "http://icanhazip.com",
        "http://ifconfig.me/ip"
    };
    
    for (int i = 0; i < 3; i++) {
        http.begin(services[i]);
        http.setTimeout(3000);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            ip = http.getString();
            ip.trim();
            if (ip.length() > 0) {
                http.end();
                return ip;
            }
        }
        http.end();
        delay(100);
    }
    
    return "Không lấy được IP";
}

// ================= GỬI CORS HEADERS =================
void sendCORSHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    server.sendHeader("Access-Control-Max-Age", "86400");
}

// ================= XỬ LÝ STREAM VIDEO =================
void handleStream() {
    sendCORSHeaders();
    
    WiFiClient client = server.client();
    
    // Gửi headers MJPEG stream
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    response += "Pragma: no-cache\r\n";
    response += "Expires: 0\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    
    client.print(response);
    
    unsigned long lastFrame = millis();
    unsigned long frameInterval = 100; // 10 FPS (100ms mỗi frame)
    
    while (client.connected()) {
        unsigned long now = millis();
        if (now - lastFrame >= frameInterval) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb) {
                Serial.println("⚠️ Không chụp được frame");
                delay(10);
                continue;
            }
            
            // Gửi frame
            String frameHeader = "--frame\r\n";
            frameHeader += "Content-Type: image/jpeg\r\n";
            frameHeader += "Content-Length: " + String(fb->len) + "\r\n";
            frameHeader += "\r\n";
            
            client.print(frameHeader);
            client.write(fb->buf, fb->len);
            client.print("\r\n");
            
            // Tính FPS
            frameCount++;
            if (now - lastFrameTime >= 1000) {
                fps = (frameCount * 1000.0) / (now - lastFrameTime);
                frameCount = 0;
                lastFrameTime = now;
            }
            
            esp_camera_fb_return(fb);
            lastFrame = now;
        }
        
        // Kiểm tra kết nối client
        delay(1);
        if (!client.connected()) {
            break;
        }
    }
}

// ================= XỬ LÝ CHỤP ẢNH =================
void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        sendCORSHeaders();
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    // Gửi headers CORS và image
    sendCORSHeaders();
    server.sendHeader("Content-Type", "image/jpeg");
    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Content-Length", String(fb->len));
    
    // Gửi ảnh
    WiFiClient client = server.client();
    server.send(200, "image/jpeg", "");
    client.write(fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
}

// ================= XỬ LÝ STATUS (JSON) =================
void handleStatus() {
    StaticJsonDocument<512> doc;
    
    doc["free_heap"] = esp_get_free_heap_size();
    doc["uptime"] = millis() / 1000;
    doc["fps"] = fps;
    doc["framesize"] = framesize;
    doc["quality"] = quality;
    doc["brightness"] = brightness;
    doc["camera_name"] = "ESP32-CAM-Remote";
    doc["flash_state"] = flashState;
    doc["local_ip"] = WiFi.localIP().toString();
    doc["public_ip"] = publicIP;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["ssid"] = ssid;
    doc["version"] = "3.0-ngrok-optimized";
    
    String json;
    serializeJson(doc, json);
    
    sendCORSHeaders();
    server.send(200, "application/json", json);
}

// ================= XỬ LÝ ĐIỀU KHIỂN =================
void handleControl() {
    bool settingsChanged = false;
    String message = "";
    
    if (server.hasArg("flash")) {
        flashState = server.arg("flash").toInt();
        pinMode(4, OUTPUT);
        digitalWrite(4, flashState ? HIGH : LOW);
        message += "Flash: " + String(flashState ? "ON" : "OFF");
        settingsChanged = true;
    }
    
    if (server.hasArg("quality")) {
        quality = constrain(server.arg("quality").toInt(), 1, 63);
        sensor_t *s = esp_camera_sensor_get();
        s->set_quality(s, quality);
        message += " | Quality: " + String(quality);
        settingsChanged = true;
    }
    
    if (server.hasArg("brightness")) {
        brightness = constrain(server.arg("brightness").toInt(), -2, 2);
        sensor_t *s = esp_camera_sensor_get();
        s->set_brightness(s, brightness);
        message += " | Brightness: " + String(brightness);
        settingsChanged = true;
    }
    
    if (server.hasArg("framesize")) {
        framesize = constrain(server.arg("framesize").toInt(), 0, 13);
        sensor_t *s = esp_camera_sensor_get();
        s->set_framesize(s, (framesize_t)framesize);
        message += " | Resolution: " + String(framesize);
        settingsChanged = true;
    }
    
    StaticJsonDocument<256> doc;
    doc["success"] = settingsChanged;
    doc["message"] = message;
    
    JsonObject settings = doc.createNestedObject("settings");
    settings["quality"] = quality;
    settings["brightness"] = brightness;
    settings["framesize"] = framesize;
    settings["flash"] = flashState;
    
    String json;
    serializeJson(doc, json);
    
    sendCORSHeaders();
    server.send(200, "application/json", json);
}

// ================= XỬ LÝ REBOOT =================
void handleReboot() {
    sendCORSHeaders();
    server.send(200, "application/json", "{\"message\":\"ESP32-CAM sẽ reboot sau 1 giây...\"}");
    delay(1000);
    ESP.restart();
}

// ================= SCAN WIFI =================
void handleWifiScan() {
    sendCORSHeaders();
    
    WiFi.scanNetworks(true); // Bắt đầu scan async
    delay(1000);
    
    int n = WiFi.scanComplete();
    String json = "[";
    
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
    
    server.send(200, "application/json", json);
}

// ================= XỬ LÝ OPTIONS (CORS PREFLIGHT) =================
void handleOptions() {
    sendCORSHeaders();
    server.send(200);
}

// ================= TRANG CHỦ HTML =================
void handleRoot() {
    String html = R"=====(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>📷 ESP32-CAM Remote v3.0</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: #333;
            line-height: 1.6;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            overflow: hidden;
            box-shadow: 0 15px 35px rgba(0, 0, 0, 0.2);
        }
        .header {
            background: linear-gradient(135deg, #4a6fa5 0%, #166088 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }
        .header h1 {
            font-size: 2.5rem;
            margin-bottom: 10px;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 15px;
        }
        .video-container {
            background: #000;
            margin: 20px;
            border-radius: 15px;
            overflow: hidden;
            position: relative;
            min-height: 480px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        #live-stream {
            max-width: 100%;
            max-height: 90vh;
            border-radius: 10px;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            padding: 20px;
        }
        .stat-card {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 12px;
            border-left: 5px solid #4a6fa5;
            transition: transform 0.3s;
        }
        .stat-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 5px 15px rgba(0,0,0,0.1);
        }
        .stat-card h3 {
            color: #4a6fa5;
            margin-bottom: 10px;
            font-size: 1.1rem;
        }
        .stat-value {
            font-size: 1.8rem;
            font-weight: bold;
            color: #333;
        }
        .control-panel {
            background: #e3f2fd;
            padding: 25px;
            margin: 20px;
            border-radius: 15px;
        }
        .btn-group {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin: 15px 0;
        }
        .btn {
            padding: 12px 24px;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            font-weight: bold;
            transition: all 0.3s;
            font-size: 0.95rem;
        }
        .btn-primary {
            background: #4a6fa5;
            color: white;
        }
        .btn-primary:hover {
            background: #166088;
            transform: scale(1.05);
        }
        .btn-success {
            background: #28a745;
            color: white;
        }
        .btn-danger {
            background: #dc3545;
            color: white;
        }
        .btn-warning {
            background: #ffc107;
            color: #333;
        }
        .remote-access-info {
            background: #fff3cd;
            border: 2px dashed #ffc107;
            padding: 20px;
            margin: 20px;
            border-radius: 15px;
        }
        .api-list {
            background: #f8f9fa;
            padding: 25px;
            margin: 20px;
            border-radius: 15px;
        }
        code {
            background: #eee;
            padding: 3px 8px;
            border-radius: 4px;
            font-family: 'Courier New', monospace;
            color: #d63384;
        }
        .footer {
            text-align: center;
            padding: 20px;
            color: #666;
            border-top: 1px solid #ddd;
            margin-top: 20px;
        }
        @media (max-width: 768px) {
            .header h1 { font-size: 1.8rem; }
            .video-container { margin: 10px; }
            .btn { width: 100%; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>📷 ESP32-CAM Remote v3.0</h1>
            <p>Camera streaming với hỗ trợ ESP32-CAM Viewer & Ngrok</p>
        </div>

        <div class="stats-grid">
            <div class="stat-card">
                <h3>📶 WiFi Strength</h3>
                <div class="stat-value" id="rssi">-- dBm</div>
            </div>
            <div class="stat-card">
                <h3>⚡ FPS</h3>
                <div class="stat-value" id="fps">0.0</div>
            </div>
            <div class="stat-card">
                <h3>⏱️ Uptime</h3>
                <div class="stat-value" id="uptime">0s</div>
            </div>
            <div class="stat-card">
                <h3>💾 Memory</h3>
                <div class="stat-value" id="heap">-- KB</div>
            </div>
        </div>

        <div class="video-container">
            <img id="live-stream" src="/stream" alt="Live Stream" 
                 onerror="this.style.display='none'; document.getElementById('stream-error').style.display='block';">
            <div id="stream-error" style="display:none; color:white; padding:50px; text-align:center;">
                <h3>⚠️ Stream không khả dụng</h3>
                <p>Thử truy cập <a href="/capture" style="color:#4a6fa5;">/capture</a> để kiểm tra camera</p>
            </div>
        </div>

        <div class="control-panel">
            <h2>🎮 Điều khiển nhanh</h2>
            <div class="btn-group">
                <button class="btn btn-primary" onclick="captureImage()">📸 Chụp ảnh</button>
                <button class="btn btn-warning" onclick="toggleFlash()">💡 Bật/Tắt Flash</button>
                <button class="btn btn-success" onclick="downloadImage()">💾 Tải ảnh</button>
                <button class="btn btn-danger" onclick="rebootCamera()">🔄 Reboot</button>
            </div>
            
            <div class="btn-group">
                <button class="btn btn-primary" onclick="setQuality(5)">🎯 Chất lượng cao (5)</button>
                <button class="btn btn-primary" onclick="setQuality(20)">🚀 Stream nhanh (20)</button>
                <button class="btn btn-primary" onclick="setBrightness(2)">🔆 Sáng tối đa</button>
                <button class="btn btn-primary" onclick="setBrightness(-2)">🌙 Tối tối đa</button>
            </div>
        </div>

        <div class="remote-access-info">
            <h2>🌐 Kết nối Remote với ESP32-CAM Viewer</h2>
            <p><strong>📍 Địa chỉ Local:</strong> <code id="local-ip">Đang tải...</code></p>
            <p><strong>🌍 Public IP:</strong> <code id="public-ip">Đang tải...</code></p>
            <p><strong>🔗 mDNS:</strong> <code>esp32-cam.local</code></p>
            
            <h3 style="margin-top:15px;">📡 Hướng dẫn dùng với Ngrok:</h3>
            <ol style="margin-left:20px; margin-top:10px;">
                <li>Chạy ESP32-CAM (code này) kết nối WiFi</li>
                <li>Trên máy tính, chạy: <code>ngrok http [IP-ESP32]:80</code></li>
                <li>Sao chép URL ngrok (vd: <code>xxxx.ngrok-free.app</code>)</li>
                <li>Mở ESP32-CAM Viewer → Cloud → nhập URL ngrok</li>
            </ol>
        </div>

        <div class="api-list">
            <h2>🔧 API Endpoints (cho ESP32-CAM Viewer)</h2>
            <ul style="margin-left:20px; margin-top:10px;">
                <li><code>GET /stream</code> - Video stream MJPEG</li>
                <li><code>GET /capture</code> - Ảnh JPEG (cho viewer)</li>
                <li><code>GET /status</code> - Trạng thái JSON</li>
                <li><code>GET /control?flash=1&quality=10</code> - Điều khiển</li>
                <li><code>OPTIONS /*</code> - CORS preflight (tự động)</li>
            </ul>
        </div>

        <div class="footer">
            <p>ESP32-CAM Remote Access v3.0 | Tối ưu cho ESP32-CAM Viewer & Ngrok</p>
            <p>Địa chỉ hiện tại: <span id="current-ip">Đang tải...</span></p>
            <p><a href="/info" style="color:#4a6fa5;">ℹ️ Thông tin hệ thống</a></p>
        </div>
    </div>

    <script>
        // Cập nhật thông số hệ thống
        function updateStats() {
            fetch('/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('rssi').textContent = data.wifi_rssi + ' dBm';
                    document.getElementById('fps').textContent = data.fps.toFixed(1);
                    document.getElementById('uptime').textContent = data.uptime + 's';
                    document.getElementById('heap').textContent = Math.round(data.free_heap/1024) + ' KB';
                    document.getElementById('local-ip').textContent = data.local_ip;
                    document.getElementById('public-ip').textContent = data.public_ip;
                    document.getElementById('current-ip').textContent = window.location.host;
                })
                .catch(e => console.log('Lỗi cập nhật:', e));
        }

        // Chụp ảnh
        function captureImage() {
            window.open('/capture?t=' + Date.now(), '_blank');
        }

        // Tải ảnh
        function downloadImage() {
            fetch('/capture?t=' + Date.now())
                .then(r => r.blob())
                .then(blob => {
                    const url = URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = 'esp32-capture-' + new Date().toISOString() + '.jpg';
                    document.body.appendChild(a);
                    a.click();
                    document.body.removeChild(a);
                    URL.revokeObjectURL(url);
                });
        }

        // Bật/tắt flash
        let flashOn = false;
        function toggleFlash() {
            flashOn = !flashOn;
            fetch('/control?flash=' + (flashOn ? 1 : 0))
                .then(() => alert('Flash: ' + (flashOn ? 'BẬT' : 'TẮT')));
        }

        // Đặt chất lượng
        function setQuality(q) {
            fetch('/control?quality=' + q)
                .then(() => alert('Chất lượng đặt: ' + q));
        }

        // Đặt độ sáng
        function setBrightness(b) {
            fetch('/control?brightness=' + b)
                .then(() => alert('Độ sáng đặt: ' + b));
        }

        // Reboot
        function rebootCamera() {
            if (confirm('Reboot ESP32-CAM?')) {
                fetch('/reboot')
                    .then(() => alert('Camera sẽ reboot...'));
            }
        }

        // Tự động cập nhật mỗi 3 giây
        setInterval(updateStats, 3000);
        updateStats();
        
        // Tự động reload stream nếu mất kết nối
        setInterval(() => {
            const img = document.getElementById('live-stream');
            if (img.style.display !== 'none') {
                img.src = '/stream?t=' + Date.now();
            }
        }, 30000);
    </script>
</body>
</html>
)=====";
    
    server.send(200, "text/html", html);
}

// ================= THÔNG TIN HỆ THỐNG =================
void handleInfo() {
    String info = "════════════════════════════════════════\n";
    info += "       ESP32-CAM REMOTE ACCESS v3.0\n";
    info += "════════════════════════════════════════\n\n";
    
    info += "📡 KẾT NỐI MẠNG:\n";
    info += "  SSID:        " + String(ssid) + "\n";
    info += "  Local IP:    " + WiFi.localIP().toString() + "\n";
    info += "  Public IP:   " + publicIP + "\n";
    info += "  MAC Address: " + WiFi.macAddress() + "\n";
    info += "  RSSI:        " + String(WiFi.RSSI()) + " dBm\n\n";
    
    info += "📊 HỆ THỐNG:\n";
    info += "  Uptime:      " + String(millis() / 1000) + " giây\n";
    info += "  Free Heap:   " + String(esp_get_free_heap_size()) + " bytes\n";
    info += "  FPS:         " + String(fps, 1) + "\n";
    info += "  Flash Size:  " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n\n";
    
    info += "🎥 CAMERA:\n";
    info += "  Resolution:  640x480 (VGA)\n";
    info += "  Quality:     " + String(quality) + "\n";
    info += "  Brightness:  " + String(brightness) + "\n";
    info += "  Flash:       " + String(flashState ? "ON" : "OFF") + "\n\n";
    
    info += "🔗 ENDPOINTS:\n";
    info += "  /            - Trang chủ\n";
    info += "  /stream      - Live stream\n";
    info += "  /capture     - Chụp ảnh\n";
    info += "  /status      - JSON status\n";
    info += "  /control     - Điều khiển\n";
    info += "  /wifi-scan   - Scan WiFi\n";
    info += "  /reboot      - Reboot\n";
    info += "  /info        - Thông tin này\n\n";
    
    info += "📱 CHO ESP32-CAM VIEWER:\n";
    info += "  Host: " + WiFi.localIP().toString() + "\n";
    info += "  Port: 80\n";
    info += "  URL:  http://" + WiFi.localIP().toString() + "/capture\n";
    info += "════════════════════════════════════════\n";
    
    sendCORSHeaders();
    server.send(200, "text/plain", info);
}

// ================= IN THÔNG TIN HỆ THỐNG =================
void printSystemInfo() {
    Serial.println("\n════════════════════════════════════════");
    Serial.println("   ESP32-CAM REMOTE ACCESS v3.0");
    Serial.println("════════════════════════════════════════");
    Serial.println("📡 WiFi: " + String(ssid));
    Serial.println("📍 Local IP:  " + WiFi.localIP().toString());
    Serial.println("🌍 Public IP: " + publicIP);
    Serial.println("📶 RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("🎥 Camera: 640x480 @ FPS: " + String(fps, 1));
    Serial.println("💾 Free Heap: " + String(esp_get_free_heap_size() / 1024) + " KB");
    Serial.println("\n🔗 CHO ESP32-CAM VIEWER:");
    Serial.println("  Direct IP: " + WiFi.localIP().toString() + ":80");
    Serial.println("  Capture URL: http://" + WiFi.localIP().toString() + "/capture");
    Serial.println("\n📡 CHO NGROK:");
    Serial.println("  Command: ngrok http " + WiFi.localIP().toString() + ":80");
    Serial.println("════════════════════════════════════════\n");
}

// ================= SETUP =================
void setup() {
    // Tắt brownout detector để ổn định
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(1000);
    
    Serial.println("\n\n");
    Serial.println("███████╗███████╗██████╗ ██╗██████╗ ");
    Serial.println("██╔════╝██╔════╝██╔══██╗╚═╝╚════██╗");
    Serial.println("███████╗███████╗██████╔╝██║ █████╔╝");
    Serial.println("╚════██║╚════██║██╔═══╝ ██║██╔═══╝ ");
    Serial.println("███████║███████║██║     ██║███████╗");
    Serial.println("╚══════╝╚══════╝╚═╝     ╚═╝╚══════╝");
    Serial.println("     CAM REMOTE v3.0 - NGROK READY");
    Serial.println("\n");
    
    // Cấu hình đèn flash
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    
    // Khởi tạo camera
    Serial.println("[1/4] 🎥 Khởi tạo camera...");
    setupCamera();
    
    // Kết nối WiFi
    Serial.println("[2/4] 📡 Kết nối WiFi: " + String(ssid));
    WiFi.begin(ssid, password);
    WiFi.setSleep(WIFI_PS_NONE); // Tắt sleep để ổn định
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        digitalWrite(4, !digitalRead(4)); // Nhấp nháy đèn khi kết nối
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(4, LOW);
        Serial.println("\n✅ WiFi connected!");
        Serial.println("   IP: " + WiFi.localIP().toString());
        Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
        
        // Lấy public IP
        Serial.println("[3/4] 🌍 Lấy public IP...");
        publicIP = getPublicIP();
        
        // Khởi tạo mDNS
        if (MDNS.begin("esp32-cam")) {
            Serial.println("✅ mDNS: esp32-cam.local");
        }
    } else {
        Serial.println("\n❌ WiFi failed! Starting AP mode...");
        WiFi.softAP("ESP32-CAM-REMOTE", "12345678");
        Serial.println("   AP IP: " + WiFi.softAPIP().toString());
        publicIP = "AP Mode";
    }
    
    // ================= CẤU HÌNH SERVER =================
    Serial.println("[4/4] 🚀 Cấu hình server...");
    
    // Đăng ký OPTIONS handler cho CORS
    server.on("/", HTTP_OPTIONS, handleOptions);
    server.on("/stream", HTTP_OPTIONS, handleOptions);
    server.on("/capture", HTTP_OPTIONS, handleOptions);
    server.on("/status", HTTP_OPTIONS, handleOptions);
    server.on("/control", HTTP_OPTIONS, handleOptions);
    server.on("/wifi-scan", HTTP_OPTIONS, handleOptions);
    server.on("/reboot", HTTP_OPTIONS, handleOptions);
    server.on("/info", HTTP_OPTIONS, handleOptions);
    
    // Đăng ký các route chính
    server.on("/", HTTP_GET, handleRoot);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/control", HTTP_GET, handleControl);
    server.on("/reboot", HTTP_GET, handleReboot);
    server.on("/wifi-scan", HTTP_GET, handleWifiScan);
    server.on("/info", HTTP_GET, handleInfo);
    
    // Xử lý 404 với CORS
    server.onNotFound([]() {
        sendCORSHeaders();
        server.send(404, "text/plain", "404: Not Found");
    });
    
    // Khởi động server
    server.begin();
    Serial.println("✅ HTTP server started on port 80");
    
    startTime = millis();
    lastFrameTime = millis();
    
    // In thông tin hệ thống
    printSystemInfo();
    
    // Nhấp nháy đèn 3 lần để báo hiệu hoàn tất
    for (int i = 0; i < 3; i++) {
        digitalWrite(4, HIGH);
        delay(100);
        digitalWrite(4, LOW);
        delay(100);
    }
}

// ================= LOOP CHÍNH =================
void loop() {
    server.handleClient();
    
    // Cập nhật public IP mỗi 5 phút
    static unsigned long lastIPUpdate = 0;
    if (millis() - lastIPUpdate > 300000 && WiFi.status() == WL_CONNECTED) {
        publicIP = getPublicIP();
        lastIPUpdate = millis();
    }
    
    // In thông tin hệ thống mỗi 30 giây
    static unsigned long lastStatsPrint = 0;
    if (millis() - lastStatsPrint > 30000) {
        Serial.printf("[STATS] Uptime: %dmin | Heap: %dKB | FPS: %.1f | RSSI: %ddBm\n",
                     millis() / 60000,
                     esp_get_free_heap_size() / 1024,
                     fps,
                     WiFi.RSSI());
        lastStatsPrint = millis();
    }
    
    delay(1);
}
