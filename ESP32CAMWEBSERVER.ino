/*
 * ======================================================
 * ESP32-CAM MODEM WIFI STREAMING v5.0
 * Stream qua modem WiFi, xem được qua WiFi/4G
 * Hỗ trợ MQTT + WebSocket + HTTP
 * ======================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "esp_camera.h"
#include "esp_timer.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================= CẤU HÌNH EEPROM =================
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 64
#define MQTT_SERVER_ADDR 128
#define MQTT_PORT_ADDR 192
#define DEVICE_NAME_ADDR 196
#define QUALITY_ADDR 260
#define FPS_ADDR 264

// ================= CẤU HÌNH MẶC ĐỊNH =================
char ssid[32] = "Tang 3";
char password[64] = "01111957v";
char mqtt_server[64] = "broker.emqx.io";
int mqtt_port = 1883;
char deviceName[32] = "ESP32-CAM-Modem";
int quality = 12;
int targetFPS = 10;

// ================= CẤU HÌNH CAMERA =================
#define CAMERA_MODEL_AI_THINKER
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

// ================= BIẾN TOÀN CỤC =================
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(8080);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String deviceId;
String topic_image;
String topic_control;
String topic_status;
String topic_discover;

bool mqttStreaming = false;
bool wsStreaming = false;
unsigned long lastMqttFrameTime = 0;
unsigned long lastWsFrameTime = 0;
unsigned long frameCount = 0;
unsigned long lastFpsCalc = 0;
float currentFps = 0;
bool flashState = false;
bool wsClients[WEBSOCKETS_SERVER_CLIENT_MAX] = {false};
unsigned long lastMqttReconnectAttempt = 0;

// ================= KHAI BÁO HÀM =================
void setupCamera();
void connectToWiFi();
boolean connectToMQTT();
void saveConfigToEEPROM();
void loadConfigFromEEPROM();
String generateDeviceId();
void handleRoot();
void handleCapture();
void handleStatus();
void handleControl();
void handleConfig();
void handleOptions();
void handleMjpegStream();
void handleDiscover();
void handleCameraJs();
void handleMqttMessage(char* topic, byte* payload, unsigned int length);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
void sendFrameToMQTT();
void sendFrameToWebSocket();
void sendCORSHeaders();
void updateCameraSettings();
void printSystemInfo();
void handleNotFound();

// ================= TẠO ID DUY NHẤT =================
String generateDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[18];
    sprintf(id, "cam-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(id);
}

// ================= LƯU CẤU HÌNH =================
void saveConfigToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    EEPROM.writeString(SSID_ADDR, ssid);
    EEPROM.writeString(PASS_ADDR, password);
    EEPROM.writeString(MQTT_SERVER_ADDR, mqtt_server);
    
    EEPROM.write(MQTT_PORT_ADDR, mqtt_port & 0xFF);
    EEPROM.write(MQTT_PORT_ADDR + 1, (mqtt_port >> 8) & 0xFF);
    
    EEPROM.writeString(DEVICE_NAME_ADDR, deviceName);
    EEPROM.write(QUALITY_ADDR, quality);
    EEPROM.write(FPS_ADDR, targetFPS);
    
    EEPROM.commit();
    EEPROM.end();
    
    Serial.println("Config saved to EEPROM");
}

// ================= ĐỌC CẤU HÌNH =================
void loadConfigFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    
    EEPROM.readString(SSID_ADDR, ssid, sizeof(ssid));
    EEPROM.readString(PASS_ADDR, password, sizeof(password));
    EEPROM.readString(MQTT_SERVER_ADDR, mqtt_server, sizeof(mqtt_server));
    
    int low = EEPROM.read(MQTT_PORT_ADDR);
    int high = EEPROM.read(MQTT_PORT_ADDR + 1);
    mqtt_port = low | (high << 8);
    
    if (mqtt_port == 65535 || mqtt_port <= 0) {
        mqtt_port = 1883;
    }
    
    EEPROM.readString(DEVICE_NAME_ADDR, deviceName, sizeof(deviceName));
    quality = EEPROM.read(QUALITY_ADDR);
    if (quality < 1 || quality > 63) quality = 12;
    
    targetFPS = EEPROM.read(FPS_ADDR);
    if (targetFPS < 1 || targetFPS > 30) targetFPS = 10;
    
    EEPROM.end();
    
    Serial.println("Config loaded from EEPROM");
    Serial.printf("MQTT Port: %d\n", mqtt_port);
}

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
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = quality;
    config.fb_count = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        delay(1000);
        ESP.restart();
    }

    Serial.println("Camera ready");
}

// ================= KẾT NỐI WIFI =================
void connectToWiFi() {
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        digitalWrite(4, !digitalRead(4));
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(4, LOW);
        Serial.println("\nWiFi connected");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi failed - Starting AP mode");
        WiFi.softAP("ESP32-CAM-Setup", "12345678");
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
    }
}

// ================= KẾT NỐI MQTT =================
boolean connectToMQTT() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(handleMqttMessage);
    mqttClient.setBufferSize(3072);
    
    Serial.printf("Connecting MQTT: %s:%d\n", mqtt_server, mqtt_port);
    
    String clientId = deviceId + "-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println("MQTT connected");
        
        mqttClient.subscribe(topic_control.c_str());
        mqttClient.subscribe(topic_discover.c_str());
        
        StaticJsonDocument<256> doc;
        doc["device"] = deviceName;
        doc["id"] = deviceId;
        doc["ip"] = WiFi.localIP().toString();
        doc["status"] = "online";
        doc["fps"] = targetFPS;
        doc["quality"] = quality;
        doc["http_port"] = 80;
        doc["ws_port"] = 8080;
        
        String msg;
        serializeJson(doc, msg);
        mqttClient.publish(topic_status.c_str(), msg.c_str(), true);
        
        return true;
    } else {
        Serial.printf("MQTT failed, rc=%d\n", mqttClient.state());
        return false;
    }
}

// ================= XỬ LÝ MQTT MESSAGE =================
void handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String message;
    
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.printf("MQTT: %s -> %s\n", topicStr.c_str(), message.c_str());
    
    if (topicStr == topic_control) {
        if (message == "flash_on") {
            flashState = true;
            digitalWrite(4, HIGH);
            mqttClient.publish(topic_control.c_str(), "flash_on_ok");
        }
        else if (message == "flash_off") {
            flashState = false;
            digitalWrite(4, LOW);
            mqttClient.publish(topic_control.c_str(), "flash_off_ok");
        }
        else if (message == "stream_start") {
            mqttStreaming = true;
            mqttClient.publish(topic_control.c_str(), "stream_started");
        }
        else if (message == "stream_stop") {
            mqttStreaming = false;
            mqttClient.publish(topic_control.c_str(), "stream_stopped");
        }
        else if (message.startsWith("quality:")) {
            quality = message.substring(8).toInt();
            if (quality < 1) quality = 1;
            if (quality > 63) quality = 63;
            updateCameraSettings();
            saveConfigToEEPROM();
            mqttClient.publish(topic_control.c_str(), "quality_updated");
        }
        else if (message.startsWith("fps:")) {
            targetFPS = message.substring(4).toInt();
            if (targetFPS < 1) targetFPS = 1;
            if (targetFPS > 30) targetFPS = 30;
            saveConfigToEEPROM();
            mqttClient.publish(topic_control.c_str(), "fps_updated");
        }
        else if (message == "reboot") {
            mqttClient.publish(topic_control.c_str(), "rebooting");
            delay(100);
            ESP.restart();
        }
    }
    else if (topicStr == topic_discover) {
        if (message == "scan" || message == "request") {
            StaticJsonDocument<256> doc;
            doc["device"] = deviceName;
            doc["id"] = deviceId;
            doc["ip"] = WiFi.localIP().toString();
            doc["status"] = "online";
            
            String msg;
            serializeJson(doc, msg);
            mqttClient.publish("esp32cam/discover/response", msg.c_str());
        }
    }
}

// ================= GỬI FRAME QUA MQTT =================
void sendFrameToMQTT() {
    if (!mqttClient.connected() || !mqttStreaming) return;
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;
    
    boolean sent = mqttClient.publish(topic_image.c_str(), fb->buf, fb->len, false);
    
    if (sent) {
        frameCount++;
    }
    
    esp_camera_fb_return(fb);
}

// ================= GỬI FRAME QUA WEBSOCKET =================
void sendFrameToWebSocket() {
    if (!wsStreaming) return;
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;
    
    for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
        if (wsClients[i]) {
            webSocket.sendBIN(i, fb->buf, fb->len);
        }
    }
    
    esp_camera_fb_return(fb);
    frameCount++;
}

// ================= WEBSOCKET EVENT =================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] WebSocket disconnected\n", num);
            wsClients[num] = false;
            
            // Check if any clients left
            wsStreaming = false;
            for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
                if (wsClients[i]) {
                    wsStreaming = true;
                    break;
                }
            }
            break;
            
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] WebSocket connected from %s\n", num, ip.toString().c_str());
                wsClients[num] = true;
                wsStreaming = true;
                
                StaticJsonDocument<256> doc;
                doc["type"] = "info";
                doc["device"] = deviceName;
                doc["id"] = deviceId;
                doc["fps"] = targetFPS;
                doc["quality"] = quality;
                
                String info;
                serializeJson(doc, info);
                webSocket.sendTXT(num, info);
            }
            break;
            
        case WStype_TEXT:
            {
                String msg = String((char*)payload);
                if (msg == "capture") {
                    camera_fb_t *fb = esp_camera_fb_get();
                    if (fb) {
                        webSocket.sendBIN(num, fb->buf, fb->len);
                        esp_camera_fb_return(fb);
                    }
                }
                else if (msg == "ping") {
                    webSocket.sendTXT(num, "pong");
                }
                else if (msg.startsWith("quality:")) {
                    quality = msg.substring(8).toInt();
                    if (quality < 1) quality = 1;
                    if (quality > 63) quality = 63;
                    updateCameraSettings();
                    saveConfigToEEPROM();
                    webSocket.sendTXT(num, "quality_updated");
                }
                else if (msg == "flash_on") {
                    flashState = true;
                    digitalWrite(4, HIGH);
                    webSocket.sendTXT(num, "flash_on");
                }
                else if (msg == "flash_off") {
                    flashState = false;
                    digitalWrite(4, LOW);
                    webSocket.sendTXT(num, "flash_off");
                }
            }
            break;
            
        case WStype_BIN:
            // Handle binary data if needed
            break;
    }
}

// ================= CẬP NHẬT CAMERA =================
void updateCameraSettings() {
    sensor_t *s = esp_camera_sensor_get();
    s->set_quality(s, quality);
}

// ================= GỬI CORS HEADERS =================
void sendCORSHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.sendHeader("Access-Control-Max-Age", "86400");
}

// ================= XỬ LÝ OPTIONS =================
void handleOptions() {
    sendCORSHeaders();
    server.send(200);
}

// ================= XỬ LÝ CAPTURE =================
void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        sendCORSHeaders();
        server.send(500, "text/plain", "Capture failed");
        return;
    }
    
    sendCORSHeaders();
    server.sendHeader("Content-Type", "image/jpeg");
    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Content-Length", String(fb->len));
    server.send(200, "image/jpeg", "");
    WiFiClient client = server.client();
    client.write(fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
}

// ================= XỬ LÝ MJPEG STREAM =================
void handleMjpegStream() {
    WiFiClient client = server.client();
    
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
    response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    response += "Pragma: no-cache\r\n";
    response += "Expires: 0\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "\r\n";
    
    client.write(response.c_str());
    
    unsigned long frameDelay = 1000 / targetFPS;
    unsigned long lastFrame = 0;
    
    while (client.connected()) {
        unsigned long now = millis();
        if (now - lastFrame >= frameDelay) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) {
                String head = "--frame\r\n";
                head += "Content-Type: image/jpeg\r\n";
                head += "Content-Length: " + String(fb->len) + "\r\n";
                head += "\r\n";
                
                client.write(head.c_str());
                client.write(fb->buf, fb->len);
                client.write("\r\n");
                
                esp_camera_fb_return(fb);
                lastFrame = now;
                frameCount++;
            }
        }
        
        // Check if client disconnected
        if (!client.connected()) {
            break;
        }
        
        delay(1);
    }
}

// ================= XỬ LÝ DISCOVER =================
void handleDiscover() {
    StaticJsonDocument<256> doc;
    
    doc["device"] = deviceName;
    doc["id"] = deviceId;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["status"] = "online";
    doc["fps"] = targetFPS;
    doc["quality"] = quality;
    doc["http_port"] = 80;
    doc["ws_port"] = 8080;
    doc["methods"] = "http,mjpeg,websocket,mqtt";
    
    String json;
    serializeJson(doc, json);
    
    sendCORSHeaders();
    server.send(200, "application/json", json);
}

// ================= XỬ LÝ STATUS =================
void handleStatus() {
    StaticJsonDocument<512> doc;
    
    doc["device"] = deviceName;
    doc["id"] = deviceId;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["fps"] = currentFps;
    doc["quality"] = quality;
    doc["target_fps"] = targetFPS;
    doc["flash"] = flashState;
    doc["heap"] = ESP.getFreeHeap();
    doc["mqtt"] = mqttClient.connected();
    doc["mqtt_server"] = String(mqtt_server) + ":" + String(mqtt_port);
    doc["ws_clients"] = webSocket.connectedClients();
    doc["streaming"] = wsStreaming || mqttStreaming;
    
    String json;
    serializeJson(doc, json);
    
    sendCORSHeaders();
    server.send(200, "application/json", json);
}

// ================= XỬ LÝ CONTROL =================
void handleControl() {
    if (server.hasArg("flash")) {
        flashState = server.arg("flash").toInt();
        digitalWrite(4, flashState ? HIGH : LOW);
    }
    
    if (server.hasArg("quality")) {
        quality = constrain(server.arg("quality").toInt(), 1, 63);
        updateCameraSettings();
        saveConfigToEEPROM();
    }
    
    if (server.hasArg("fps")) {
        targetFPS = constrain(server.arg("fps").toInt(), 1, 30);
        saveConfigToEEPROM();
    }
    
    if (server.hasArg("stream")) {
        mqttStreaming = server.arg("stream").toInt() == 1;
    }
    
    if (server.hasArg("reboot")) {
        server.send(200, "text/plain", "Rebooting...");
        delay(100);
        ESP.restart();
        return;
    }
    
    handleStatus();
}

// ================= TRANG CẤU HÌNH =================
void handleConfig() {
    if (server.method() == HTTP_POST) {
        if (server.hasArg("ssid")) {
            strcpy(ssid, server.arg("ssid").c_str());
        }
        if (server.hasArg("password")) {
            strcpy(password, server.arg("password").c_str());
        }
        if (server.hasArg("mqtt_server")) {
            strcpy(mqtt_server, server.arg("mqtt_server").c_str());
        }
        if (server.hasArg("mqtt_port")) {
            mqtt_port = server.arg("mqtt_port").toInt();
        }
        if (server.hasArg("device_name")) {
            strcpy(deviceName, server.arg("device_name").c_str());
        }
        if (server.hasArg("quality")) {
            quality = server.arg("quality").toInt();
        }
        if (server.hasArg("fps")) {
            targetFPS = server.arg("fps").toInt();
        }
        
        saveConfigToEEPROM();
        
        String html = "<!DOCTYPE html><html><head><title>Success</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f2f5}.card{background:white;padding:30px;border-radius:10px;max-width:400px;margin:0 auto}.success{color:#28a745}</style>";
        html += "</head><body>";
        html += "<div class='card'>";
        html += "<h2 class='success'>Configuration Saved!</h2>";
        html += "<p>ESP32-CAM will reboot in 3 seconds...</p>";
        html += "<p><a href='/'>Back to Stream</a></p>";
        html += "</div></body></html>";
        
        server.send(200, "text/html", html);
        delay(3000);
        ESP.restart();
        return;
    }
    
    String html = "<!DOCTYPE html><html><head><title>ESP32-CAM Config</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial;background:#f0f2f5;padding:20px}";
    html += ".container{max-width:600px;margin:0 auto;background:white;padding:30px;border-radius:20px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
    html += "h2{color:#4a6fa5;text-align:center}";
    html += "label{display:block;margin:15px 0 5px;color:#666}";
    html += "input[type=text],input[type=password],input[type=number]{width:100%;padding:10px;border:2px solid #ddd;border-radius:8px}";
    html += ".info{background:#e3f2fd;padding:15px;border-radius:8px;margin:20px 0}";
    html += "button{background:#4a6fa5;color:white;border:none;padding:15px;border-radius:8px;cursor:pointer;width:100%;font-size:16px}";
    html += "button:hover{background:#166088}";
    html += "code{background:#f0f0f0;padding:2px 5px;border-radius:4px}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<h2>⚙️ ESP32-CAM Configuration</h2>";
    
    html += "<div class='info'>";
    html += "Device ID: <code>" + deviceId + "</code><br>";
    html += "Local IP: <code>" + WiFi.localIP().toString() + "</code><br>";
    html += "Stream URL: <code>http://" + WiFi.localIP().toString() + "/stream</code><br>";
    html += "MQTT Topic: <code>" + topic_image + "</code><br>";
    html += "</div>";
    
    html += "<form method='POST'>";
    html += "<label>WiFi SSID:</label>";
    html += "<input type='text' name='ssid' value='" + String(ssid) + "'>";
    
    html += "<label>WiFi Password:</label>";
    html += "<input type='password' name='password' value='" + String(password) + "'>";
    
    html += "<label>MQTT Server:</label>";
    html += "<input type='text' name='mqtt_server' value='" + String(mqtt_server) + "'>";
    
    html += "<label>MQTT Port:</label>";
    html += "<input type='number' name='mqtt_port' value='" + String(mqtt_port) + "'>";
    
    html += "<label>Device Name:</label>";
    html += "<input type='text' name='device_name' value='" + String(deviceName) + "'>";
    
    html += "<label>Quality (1-63):</label>";
    html += "<input type='number' name='quality' min='1' max='63' value='" + String(quality) + "'>";
    
    html += "<label>Target FPS (1-30):</label>";
    html += "<input type='number' name='fps' min='1' max='30' value='" + String(targetFPS) + "'>";
    
    html += "<button type='submit'>Save & Reboot</button>";
    html += "</form>";
    
    html += "<p style='text-align:center;margin-top:20px'><a href='/'>← Back to Stream</a></p>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

// ================= XỬ LÝ CAMERA.JS =================
void handleCameraJs() {
    String js = R"rawliteral(
// ESP32-CAM JavaScript Client
class ESP32CAM {
    constructor(options = {}) {
        this.ip = options.ip || window.location.hostname;
        this.port = options.port || 80;
        this.wsPort = options.wsPort || 8080;
        this.useMQTT = options.useMQTT || false;
        this.mqttBroker = options.mqttBroker || 'broker.emqx.io';
        this.mqttPort = options.mqttPort || 1883;
        this.deviceId = options.deviceId || '';
        
        this.ws = null;
        this.mqtt = null;
        this.streaming = false;
        this.frameCount = 0;
        this.fps = 0;
        this.lastFrameTime = Date.now();
        this.onFrame = options.onFrame || null;
        this.onConnect = options.onConnect || null;
        this.onDisconnect = options.onDisconnect || null;
    }
    
    // HTTP Methods
    async capture() {
        try {
            const response = await fetch(`http://${this.ip}:${this.port}/capture?t=${Date.now()}`);
            const blob = await response.blob();
            return URL.createObjectURL(blob);
        } catch (error) {
            console.error('Capture error:', error);
            return null;
        }
    }
    
    async getStatus() {
        try {
            const response = await fetch(`http://${this.ip}:${this.port}/status`);
            return await response.json();
        } catch (error) {
            console.error('Status error:', error);
            return null;
        }
    }
    
    async control(params) {
        try {
            const url = new URL(`http://${this.ip}:${this.port}/control`);
            Object.keys(params).forEach(key => url.searchParams.append(key, params[key]));
            const response = await fetch(url);
            return await response.json();
        } catch (error) {
            console.error('Control error:', error);
            return null;
        }
    }
    
    // MJPEG Stream
    startMjpegStream(imgElement) {
        if (!imgElement) return;
        imgElement.src = `http://${this.ip}:${this.port}/stream?t=${Date.now()}`;
        this.streaming = true;
    }
    
    stopMjpegStream(imgElement) {
        if (imgElement) {
            imgElement.src = '';
        }
        this.streaming = false;
    }
    
    // WebSocket Stream
    connectWebSocket() {
        return new Promise((resolve, reject) => {
            try {
                this.ws = new WebSocket(`ws://${this.ip}:${this.wsPort}`);
                
                this.ws.onopen = () => {
                    console.log('WebSocket connected');
                    if (this.onConnect) this.onConnect('websocket');
                    resolve();
                };
                
                this.ws.onmessage = (event) => {
                    if (event.data instanceof Blob) {
                        const url = URL.createObjectURL(event.data);
                        this.frameCount++;
                        
                        const now = Date.now();
                        const diff = now - this.lastFrameTime;
                        if (diff >= 1000) {
                            this.fps = this.frameCount;
                            this.frameCount = 0;
                            this.lastFrameTime = now;
                        }
                        
                        if (this.onFrame) {
                            this.onFrame({
                                url: url,
                                fps: this.fps,
                                size: event.data.size
                            });
                        }
                    } else {
                        console.log('WS message:', event.data);
                    }
                };
                
                this.ws.onerror = (error) => {
                    console.error('WebSocket error:', error);
                    reject(error);
                };
                
                this.ws.onclose = () => {
                    console.log('WebSocket disconnected');
                    if (this.onDisconnect) this.onDisconnect('websocket');
                };
                
            } catch (error) {
                reject(error);
            }
        });
    }
    
    disconnectWebSocket() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
    
    // MQTT Connection (via WebSocket)
    connectMQTT() {
        return new Promise((resolve, reject) => {
            if (!window.mqtt) {
                reject('MQTT library not loaded');
                return;
            }
            
            const clientId = 'web_' + Math.random().toString(36).substring(7);
            const wsUrl = `ws://${this.mqttBroker}:8083`;
            
            this.mqtt = mqtt.connect(wsUrl, {
                clientId: clientId,
                keepalive: 60,
                reconnectPeriod: 5000
            });
            
            this.mqtt.on('connect', () => {
                console.log('MQTT connected');
                
                // Subscribe to image topic
                const topic = this.deviceId ? 
                    `esp32cam/${this.deviceId}/image` : 
                    'esp32cam/+/image';
                
                this.mqtt.subscribe(topic);
                
                // Subscribe to status topic
                const statusTopic = this.deviceId ? 
                    `esp32cam/${this.deviceId}/status` : 
                    'esp32cam/+/status';
                
                this.mqtt.subscribe(statusTopic);
                
                if (this.onConnect) this.onConnect('mqtt');
                resolve();
            });
            
            this.mqtt.on('message', (topic, message) => {
                if (topic.includes('/image')) {
                    const blob = new Blob([message], { type: 'image/jpeg' });
                    const url = URL.createObjectURL(blob);
                    
                    this.frameCount++;
                    const now = Date.now();
                    const diff = now - this.lastFrameTime;
                    if (diff >= 1000) {
                        this.fps = this.frameCount;
                        this.frameCount = 0;
                        this.lastFrameTime = now;
                    }
                    
                    if (this.onFrame) {
                        this.onFrame({
                            url: url,
                            fps: this.fps,
                            size: message.length
                        });
                    }
                } else if (topic.includes('/status')) {
                    try {
                        const status = JSON.parse(message.toString());
                        console.log('Device status:', status);
                    } catch (e) {}
                }
            });
            
            this.mqtt.on('error', (error) => {
                console.error('MQTT error:', error);
                reject(error);
            });
            
            this.mqtt.on('close', () => {
                if (this.onDisconnect) this.onDisconnect('mqtt');
            });
        });
    }
    
    disconnectMQTT() {
        if (this.mqtt) {
            this.mqtt.end();
            this.mqtt = null;
        }
    }
    
    // Device Discovery
    async discoverDevices() {
        try {
            // Try local network discovery
            const response = await fetch(`http://${this.ip}:${this.port}/discover`);
            const data = await response.json();
            return [data];
        } catch (error) {
            console.error('Discovery error:', error);
            return [];
        }
    }
}

// Auto-discover on page load
window.ESP32CAM = ESP32CAM;
)rawliteral";
    
    server.send(200, "application/javascript", js);
}

// ================= XỬ LÝ NOT FOUND =================
void handleNotFound() {
    sendCORSHeaders();
    String message = "404 Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    server.send(404, "text/plain", message);
}

// ================= TRANG CHỦ =================
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><title>" + String(deviceName) + "</title>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
    html += "<style>";
    html += "*{margin:0;padding:0;box-sizing:border-box}";
    html += "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#1a1a1a;color:#fff;min-height:100vh}";
    html += ".container{max-width:1200px;margin:0 auto;padding:15px}";
    html += ".header{background:#2d2d2d;border-radius:15px;padding:20px;margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:15px}";
    html += ".title{font-size:1.5rem;font-weight:600;color:#4a9eff}";
    html += ".status-badge{padding:8px 16px;border-radius:20px;font-size:0.9rem;font-weight:500}";
    html += ".status-online{background:#1a4d1a;color:#4caf50}";
    html += ".status-offline{background:#4d1a1a;color:#f44336}";
    html += ".video-container{background:#000;border-radius:15px;overflow:hidden;margin-bottom:20px;aspect-ratio:4/3;position:relative;box-shadow:0 4px 20px rgba(0,0,0,0.5)}";
    html += "#videoStream{width:100%;height:100%;object-fit:contain;display:none}";
    html += ".placeholder{position:absolute;top:0;left:0;width:100%;height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;background:#1a1a1a;color:#666}";
    html += ".placeholder i{font-size:4rem;margin-bottom:1rem;color:#333}";
    html += ".controls{background:#2d2d2d;border-radius:15px;padding:20px;margin-bottom:20px}";
    html += ".control-group{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:12px;margin-bottom:15px}";
    html += ".control-group:last-child{margin-bottom:0}";
    html += "button{display:flex;align-items:center;justify-content:center;gap:8px;padding:12px;border:none;border-radius:10px;font-size:0.95rem;font-weight:500;cursor:pointer;transition:all 0.2s;width:100%}";
    html += "button:active{transform:scale(0.98)}";
    html += ".btn-primary{background:#4a9eff;color:#fff}";
    html += ".btn-success{background:#4caf50;color:#fff}";
    html += ".btn-danger{background:#f44336;color:#fff}";
    html += ".btn-warning{background:#ff9800;color:#fff}";
    html += ".btn-secondary{background:#666;color:#fff}";
    html += ".info-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin-bottom:20px}";
    html += ".info-card{background:#2d2d2d;border-radius:12px;padding:15px}";
    html += ".info-label{color:#999;font-size:0.85rem;margin-bottom:5px}";
    html += ".info-value{font-size:1.4rem;font-weight:600;color:#4a9eff}";
    html += ".info-unit{font-size:0.9rem;color:#666;margin-left:4px}";
    html += ".tab-bar{display:flex;gap:2px;background:#1a1a1a;border-radius:10px;padding:4px;margin-bottom:20px}";
    html += ".tab{flex:1;padding:10px;text-align:center;border-radius:8px;cursor:pointer;font-weight:500;transition:all 0.2s}";
    html += ".tab.active{background:#4a9eff;color:#fff}";
    html += ".tab-content{display:none}";
    html += ".tab-content.active{display:block}";
    html += "select, input{width:100%;padding:12px;background:#1a1a1a;border:1px solid #444;border-radius:8px;color:#fff;font-size:1rem;margin-bottom:10px}";
    html += ".mqtt-section{background:#1a1a1a;border-radius:10px;padding:15px;margin-top:15px}";
    html += ".device-item{display:flex;justify-content:space-between;align-items:center;padding:12px;background:#1a1a1a;border-radius:8px;margin-bottom:8px;border-left:4px solid #4a9eff}";
    html += ".device-item.online{border-left-color:#4caf50}";
    html += ".device-item.offline{border-left-color:#f44336;opacity:0.6}";
    html += ".device-info{display:flex;align-items:center;gap:10px}";
    html += ".device-dot{width:10px;height:10px;border-radius:50%;background:#4caf50}";
    html += ".device-dot.offline{background:#f44336}";
    html += ".toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#333;color:#fff;padding:12px 24px;border-radius:30px;font-size:0.95rem;z-index:9999;box-shadow:0 4px 12px rgba(0,0,0,0.3);display:none}";
    html += ".toast.show{display:block;animation:slideUp 0.3s ease}";
    html += "@keyframes slideUp{from{opacity:0;transform:translate(-50%,20px)}to{opacity:1;transform:translate(-50%,0)}}";
    html += "@media(max-width:768px){.header{flex-direction:column;align-items:flex-start}}";
    html += "</style>";
    html += "</head><body>";
    
    html += "<div class='container'>";
    
    // Header
    html += "<div class='header'>";
    html += "<div><span class='title'>📷 " + String(deviceName) + "</span></div>";
    html += "<div><span class='status-badge status-online' id='statusBadge'>● ONLINE</span></div>";
    html += "</div>";
    
    // Video Container
    html += "<div class='video-container' id='videoContainer'>";
    html += "<img id='videoStream' style='display:none'>";
    html += "<div class='placeholder' id='videoPlaceholder'>";
    html += "<div style='font-size:4rem;margin-bottom:1rem;color:#333'>📹</div>";
    html += "<p>No stream active</p>";
    html += "<small style='color:#444'>Click Start Stream to begin</small>";
    html += "</div>";
    html += "</div>";
    
    // Tab Bar
    html += "<div class='tab-bar'>";
    html += "<div class='tab active' onclick='switchTab(\"stream\")'>📡 Stream</div>";
    html += "<div class='tab' onclick='switchTab(\"mqtt\")'>🌐 MQTT Remote</div>";
    html += "<div class='tab' onclick='switchTab(\"settings\")'>⚙️ Settings</div>";
    html += "</div>";
    
    // Stream Tab
    html += "<div id='tabStream' class='tab-content active'>";
    
    // Info Grid
    html += "<div class='info-grid'>";
    html += "<div class='info-card'><div class='info-label'>FPS</div><div class='info-value' id='fpsValue'>0</div></div>";
    html += "<div class='info-card'><div class='info-label'>Quality</div><div class='info-value' id='qualityValue'>" + String(quality) + "</div></div>";
    html += "<div class='info-card'><div class='info-label'>RSSI</div><div class='info-value' id='rssiValue'>" + String(WiFi.RSSI()) + "<span class='info-unit'>dBm</span></div></div>";
    html += "<div class='info-card'><div class='info-label'>Uptime</div><div class='info-value' id='uptimeValue'>0</div></div>";
    html += "</div>";
    
    // Stream Controls
    html += "<div class='controls'>";
    html += "<div class='control-group'>";
    html += "<button class='btn-primary' onclick='startMjpeg()'><span>▶</span> MJPEG Stream</button>";
    html += "<button class='btn-success' onclick='startWebSocket()'><span>🔌</span> WebSocket</button>";
    html += "<button class='btn-danger' onclick='stopStream()'><span>⏹️</span> Stop</button>";
    html += "</div>";
    html += "<div class='control-group'>";
    html += "<button class='btn-warning' onclick='toggleFlash()'><span>💡</span> Flash</button>";
    html += "<button class='btn-primary' onclick='captureImage()'><span>📸</span> Capture</button>";
    html += "<button class='btn-secondary' onclick='fullscreen()'><span>⛶</span> Fullscreen</button>";
    html += "</div>";
    html += "</div>";
    
    html += "</div>"; // Close stream tab
    
    // MQTT Tab
    html += "<div id='tabMqtt' class='tab-content'>";
    html += "<div class='controls'>";
    html += "<div class='mqtt-section'>";
    html += "<h3 style='margin-bottom:15px;color:#4a9eff'>🔗 MQTT Remote Connection</h3>";
    html += "<select id='mqttBrokerSelect' class='input-field'>";
    html += "<option value='broker.emqx.io:8083'>broker.emqx.io (Global)</option>";
    html += "<option value='test.mosquitto.org:8081'>test.mosquitto.org</option>";
    html += "<option value='broker.hivemq.com:8000'>broker.hivemq.com</option>";
    html += "<option value='custom'>Custom...</option>";
    html += "</select>";
    html += "<input type='text' id='customBroker' class='input-field' placeholder='Custom broker:port' style='display:none'>";
    html += "<input type='text' id='mqttUsername' class='input-field' placeholder='Username (optional)'>";
    html += "<input type='password' id='mqttPassword' class='input-field' placeholder='Password (optional)'>";
    html += "<input type='text' id='mqttTopic' class='input-field' placeholder='Topic (esp32cam/+/image)' value='esp32cam/+/image'>";
    html += "<div style='display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:15px 0'>";
    html += "<button class='btn-primary' onclick='connectMQTT()'>Connect MQTT</button>";
    html += "<button class='btn-danger' onclick='disconnectMQTT()'>Disconnect</button>";
    html += "</div>";
    html += "<div style='background:#1a1a1a;padding:10px;border-radius:8px;margin:10px 0' id='mqttStatus'>MQTT: Disconnected</div>";
    html += "<button class='btn-success' onclick='scanDevices()' style='margin-top:5px'>🔍 Scan for Devices</button>";
    html += "</div>";
    html += "<div id='deviceList' style='margin-top:20px;max-height:300px;overflow-y:auto'></div>";
    html += "</div></div>"; // Close MQTT tab
    
    // Settings Tab
    html += "<div id='tabSettings' class='tab-content'>";
    html += "<div class='controls'>";
    html += "<h3 style='margin-bottom:15px;color:#4a9eff'>⚙️ Camera Settings</h3>";
    html += "<div style='margin-bottom:20px'>";
    html += "<label style='display:block;margin-bottom:8px;color:#999'>Quality (1-63): <span id='sliderQuality'>" + String(quality) + "</span></label>";
    html += "<input type='range' id='qualitySlider' min='1' max='63' value='" + String(quality) + "' style='width:100%' oninput='document.getElementById(\"sliderQuality\").innerText=this.value'>";
    html += "</div>";
    html += "<div style='margin-bottom:20px'>";
    html += "<label style='display:block;margin-bottom:8px;color:#999'>Target FPS (1-30): <span id='sliderFPS'>" + String(targetFPS) + "</span></label>";
    html += "<input type='range' id='fpsSlider' min='1' max='30' value='" + String(targetFPS) + "' style='width:100%' oninput='document.getElementById(\"sliderFPS\").innerText=this.value'>";
    html += "</div>";
    html += "<button class='btn-primary' onclick='applySettings()' style='margin-bottom:15px'>Apply Settings</button>";
    html += "<hr style='border-color:#444;margin:20px 0'>";
    html += "<h3 style='margin-bottom:15px;color:#4a9eff'>ℹ️ Device Info</h3>";
    html += "<div style='background:#1a1a1a;padding:15px;border-radius:8px'>";
    html += "<p><strong>Device ID:</strong> " + deviceId + "</p>";
    html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>MAC Address:</strong> " + WiFi.macAddress() + "</p>";
    html += "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "<p><strong>SDK Version:</strong> " + String(ESP.getSdkVersion()) + "</p>";
    html += "<p><strong>Flash Size:</strong> " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB</p>";
    html += "</div>";
    html += "<button class='btn-danger' onclick='rebootDevice()' style='margin-top:20px;background:#f44336'>🔄 Reboot Device</button>";
    html += "<p style='margin-top:10px;text-align:center'><a href='/config' style='color:#4a9eff'>⚙️ Advanced Configuration</a></p>";
    html += "</div></div>"; // Close settings tab
    
    html += "</div>"; // Close container
    
    // Toast
    html += "<div id='toast' class='toast'></div>";
    
    // Script
    html += "<script src='/camera.js'></script>";
    html += "<script>";
    html += "const cam = new ESP32CAM({ ip: window.location.hostname, deviceId: '" + deviceId + "' });";
    html += "let currentStream = null;";
    html += "let flashState = false;";
    html += "let fpsInterval = null;";
    html += "let frameCount = 0;";
    html += "let lastFrameTime = Date.now();";
    html += "let mqttConnected = false;";
    
    html += R"rawliteral(
    // Tab switching
    function switchTab(tab) {
        document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
        
        if (tab === 'stream') {
            document.querySelector('.tab').classList.add('active');
            document.getElementById('tabStream').classList.add('active');
        } else if (tab === 'mqtt') {
            document.querySelectorAll('.tab')[1].classList.add('active');
            document.getElementById('tabMqtt').classList.add('active');
        } else if (tab === 'settings') {
            document.querySelectorAll('.tab')[2].classList.add('active');
            document.getElementById('tabSettings').classList.add('active');
        }
    }
    
    // Show toast
    function showToast(msg, type = 'info') {
        const toast = document.getElementById('toast');
        toast.textContent = msg;
        toast.style.background = type === 'error' ? '#f44336' : type === 'success' ? '#4caf50' : '#333';
        toast.classList.add('show');
        setTimeout(() => toast.classList.remove('show'), 3000);
    }
    
    // Start MJPEG stream
    function startMjpeg() {
        const img = document.getElementById('videoStream');
        const placeholder = document.getElementById('videoPlaceholder');
        
        img.style.display = 'block';
        placeholder.style.display = 'none';
        
        cam.startMjpegStream(img);
        currentStream = 'mjpeg';
        
        startFpsMonitor();
        showToast('MJPEG stream started', 'success');
    }
    
    // Start WebSocket stream
    function startWebSocket() {
        cam.onFrame = (data) => {
            const img = document.getElementById('videoStream');
            const placeholder = document.getElementById('videoPlaceholder');
            
            img.src = data.url;
            img.style.display = 'block';
            placeholder.style.display = 'none';
            
            frameCount++;
            
            // Update FPS
            const now = Date.now();
            if (now - lastFrameTime >= 1000) {
                document.getElementById('fpsValue').textContent = frameCount;
                frameCount = 0;
                lastFrameTime = now;
            }
        };
        
        cam.connectWebSocket().then(() => {
            currentStream = 'websocket';
            showToast('WebSocket connected', 'success');
            startFpsMonitor();
        }).catch(error => {
            showToast('WebSocket failed: ' + error, 'error');
        });
    }
    
    // Stop stream
    function stopStream() {
        const img = document.getElementById('videoStream');
        const placeholder = document.getElementById('videoPlaceholder');
        
        img.style.display = 'none';
        img.src = '';
        placeholder.style.display = 'flex';
        
        if (currentStream === 'websocket') {
            cam.disconnectWebSocket();
        }
        
        currentStream = null;
        if (fpsInterval) {
            clearInterval(fpsInterval);
            fpsInterval = null;
        }
        
        document.getElementById('fpsValue').textContent = '0';
        showToast('Stream stopped');
    }
    
    // Capture image
    async function captureImage() {
        const url = await cam.capture();
        if (url) {
            window.open(url, '_blank');
        } else {
            showToast('Capture failed', 'error');
        }
    }
    
    // Toggle flash
    async function toggleFlash() {
        flashState = !flashState;
        const result = await cam.control({ flash: flashState ? 1 : 0 });
        if (result) {
            showToast('Flash ' + (flashState ? 'ON' : 'OFF'));
        }
    }
    
    // Fullscreen
    function fullscreen() {
        const container = document.getElementById('videoContainer');
        if (container.requestFullscreen) {
            container.requestFullscreen();
        }
    }
    
    // Apply settings
    async function applySettings() {
        const quality = document.getElementById('qualitySlider').value;
        const fps = document.getElementById('fpsSlider').value;
        
        const result = await cam.control({ quality: quality, fps: fps });
        if (result) {
            document.getElementById('qualityValue').textContent = quality;
            showToast('Settings applied', 'success');
        }
    }
    
    // Reboot device
    function rebootDevice() {
        if (confirm('Reboot ESP32-CAM?')) {
            fetch('/control?reboot=1');
            showToast('Rebooting...');
            setTimeout(() => window.location.reload(), 5000);
        }
    }
    
    // FPS Monitor
    function startFpsMonitor() {
        if (fpsInterval) clearInterval(fpsInterval);
        
        frameCount = 0;
        lastFrameTime = Date.now();
        
        fpsInterval = setInterval(() => {
            const now = Date.now();
            const fps = frameCount;
            document.getElementById('fpsValue').textContent = fps;
            frameCount = 0;
            lastFrameTime = now;
        }, 1000);
    }
    
    // Update status periodically
    async function updateStatus() {
        const status = await cam.getStatus();
        if (status) {
            document.getElementById('rssiValue').textContent = status.rssi || '-';
            document.getElementById('uptimeValue').textContent = Math.floor(status.uptime / 60) + 'm';
            
            if (status.mqtt) {
                document.getElementById('mqttStatus').innerHTML = 'MQTT: Connected to ' + status.mqtt_server;
            }
        }
    }
    
    // MQTT Functions
    function connectMQTT() {
        const select = document.getElementById('mqttBrokerSelect');
        let broker = select.value;
        
        if (broker === 'custom') {
            broker = document.getElementById('customBroker').value;
            if (!broker) {
                showToast('Please enter custom broker', 'error');
                return;
            }
        }
        
        const [host, port] = broker.split(':');
        const username = document.getElementById('mqttUsername').value;
        const password = document.getElementById('mqttPassword').value;
        const topic = document.getElementById('mqttTopic').value;
        
        cam.mqttBroker = host;
        cam.mqttPort = parseInt(port) || 1883;
        
        cam.onFrame = (data) => {
            const img = document.getElementById('videoStream');
            const placeholder = document.getElementById('videoPlaceholder');
            
            img.src = data.url;
            img.style.display = 'block';
            placeholder.style.display = 'none';
            
            frameCount++;
        };
        
        cam.connectMQTT().then(() => {
            mqttConnected = true;
            document.getElementById('mqttStatus').innerHTML = 'MQTT: Connected to ' + broker;
            showToast('MQTT connected', 'success');
            startFpsMonitor();
        }).catch(error => {
            showToast('MQTT failed: ' + error, 'error');
        });
    }
    
    function disconnectMQTT() {
        cam.disconnectMQTT();
        mqttConnected = false;
        document.getElementById('mqttStatus').innerHTML = 'MQTT: Disconnected';
        stopStream();
        showToast('MQTT disconnected');
    }
    
    function scanDevices() {
        showToast('Scanning for devices...');
        
        // Simulate device discovery
        setTimeout(() => {
            const devices = [
                { id: 'cam-123456', name: 'ESP32-CAM Living Room', ip: '192.168.1.100', online: true },
                { id: 'cam-789012', name: 'ESP32-CAM Garden', ip: '192.168.1.101', online: true }
            ];
            
            const list = document.getElementById('deviceList');
            list.innerHTML = devices.map(dev => `
                <div class="device-item online">
                    <div class="device-info">
                        <span class="device-dot"></span>
                        <div>
                            <strong>${dev.name}</strong><br>
                            <small>${dev.id}</small>
                        </div>
                    </div>
                    <button onclick="selectDevice('${dev.id}')" style="background:#4a9eff;border:none;color:white;padding:5px 10px;border-radius:5px;cursor:pointer">Connect</button>
                </div>
            `).join('');
        }, 1000);
    }
    
    function selectDevice(deviceId) {
        document.getElementById('mqttTopic').value = `esp32cam/${deviceId}/image`;
        showToast('Selected device: ' + deviceId);
    }
    
    // Custom broker input
    document.getElementById('mqttBrokerSelect').addEventListener('change', function() {
        const custom = document.getElementById('customBroker');
        custom.style.display = this.value === 'custom' ? 'block' : 'none';
    });
    
    // Initial status
    setInterval(updateStatus, 5000);
    updateStatus();
    
    )rawliteral";
    
    html += "</script>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

// ================= IN THÔNG TIN =================
void printSystemInfo() {
    Serial.println("\n========================================");
    Serial.println("   ESP32-CAM MODEM STREAMING v5.0");
    Serial.println("========================================");
    Serial.print("WiFi: "); Serial.println(ssid);
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("Device ID: "); Serial.println(deviceId);
    Serial.print("RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    Serial.println("\nStream URLs:");
    Serial.print("  MJPEG: http://"); Serial.print(WiFi.localIP()); Serial.println("/stream");
    Serial.print("  Capture: http://"); Serial.print(WiFi.localIP()); Serial.println("/capture");
    Serial.print("  WebSocket: ws://"); Serial.print(WiFi.localIP()); Serial.println(":8080");
    Serial.println("\nMQTT:");
    Serial.print("  Broker: "); Serial.print(mqtt_server); Serial.print(":"); Serial.println(mqtt_port);
    Serial.print("  Image: "); Serial.println(topic_image);
    Serial.print("  Control: "); Serial.println(topic_control);
    Serial.println("\nWeb Config:");
    Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/config");
    Serial.println("========================================\n");
}

// ================= SETUP =================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n");
    Serial.println("ESP32-CAM MODEM STREAMING v5.0");
    Serial.println("========================================\n");
    
    deviceId = generateDeviceId();
    topic_image = "esp32cam/" + deviceId + "/image";
    topic_control = "esp32cam/" + deviceId + "/control";
    topic_status = "esp32cam/" + deviceId + "/status";
    topic_discover = "esp32cam/discover";
    
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    
    loadConfigFromEEPROM();
    
    Serial.println("[1/4] Initializing camera...");
    setupCamera();
    
    Serial.println("[2/4] Connecting to WiFi...");
    connectToWiFi();
    
    MDNS.begin("esp32-cam");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 8080);
    
    Serial.println("[3/4] Setting up MQTT...");
    if (WiFi.status() == WL_CONNECTED) {
        connectToMQTT();
    }
    
    Serial.println("[4/4] Starting servers...");
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    
    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/stream", HTTP_GET, handleMjpegStream);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/control", HTTP_GET, handleControl);
    server.on("/config", HTTP_GET, handleConfig);
    server.on("/config", HTTP_POST, handleConfig);
    server.on("/discover", HTTP_GET, handleDiscover);
    server.on("/camera.js", HTTP_GET, handleCameraJs);
    
    server.on("/", HTTP_OPTIONS, handleOptions);
    server.on("/capture", HTTP_OPTIONS, handleOptions);
    server.on("/stream", HTTP_OPTIONS, handleOptions);
    server.on("/status", HTTP_OPTIONS, handleOptions);
    server.on("/control", HTTP_OPTIONS, handleOptions);
    server.on("/discover", HTTP_OPTIONS, handleOptions);
    server.on("/camera.js", HTTP_OPTIONS, handleOptions);
    
    server.onNotFound(handleNotFound);
    
    server.begin();
    Serial.println("HTTP server ready");
    
    printSystemInfo();
    
    // Flash LED to indicate ready
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
    webSocket.loop();
    
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            unsigned long now = millis();
            if (now - lastMqttReconnectAttempt > 10000) {
                lastMqttReconnectAttempt = now;
                if (connectToMQTT()) {
                    lastMqttReconnectAttempt = 0;
                }
            }
        } else {
            mqttClient.loop();
        }
    }
    
    unsigned long now = millis();
    unsigned long frameInterval = 1000 / targetFPS;
    
    if (mqttStreaming && now - lastMqttFrameTime >= frameInterval) {
        sendFrameToMQTT();
        lastMqttFrameTime = now;
    }
    
    if (wsStreaming && now - lastWsFrameTime >= frameInterval) {
        sendFrameToWebSocket();
        lastWsFrameTime = now;
    }
    
    if (now - lastFpsCalc >= 1000) {
        currentFps = frameCount;
        frameCount = 0;
        lastFpsCalc = now;
    }
    
    delay(1);
}
