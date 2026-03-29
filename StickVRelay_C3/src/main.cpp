#include <Arduino.h>

/*
 * StickV Camera Relay — ESP32-C3 Super Mini
 * -----------------------------------------
 * Receives JPEG from M5StickV over UART (Grove port)
 * Serves latest photo at http://[device-ip]/latest.jpg
 * CYD polls that URL and displays it.
 *
 * Wiring (Grove cable):
 *   Grove Yellow (StickV CONNEXT_A, pin 35 = TX) → C3 GPIO20 (RX)
 *   Grove White  (StickV CONNEXT_B, pin 34 = RX) → C3 GPIO21 (TX)
 *   Grove Black  (GND)                           → C3 GND
 *   Grove Red    (3.3V)                          → leave unconnected
 */

#include <WiFi.h>
#include <WebServer.h>

// ── Config ────────────────────────────────────────────────────────
// Connects to the CYD SoftAP — no manual WiFi entry needed
const char* WIFI_SSID = "StickVCam";
const char* WIFI_PASS = "";   // open AP

#define UART_RX_PIN  20
#define UART_TX_PIN  21
#define UART_BAUD    115200

#define MAX_IMG_SIZE (50 * 1024)   // 50KB max — StickV QVGA ~15-25KB

// ── Globals ───────────────────────────────────────────────────────
HardwareSerial StickV(1);
WebServer server(80);

uint8_t imgBuf[MAX_IMG_SIZE];
size_t  imgSize   = 0;
bool    hasPhoto  = false;
uint32_t photoCount = 0;

// ── HTTP handlers ─────────────────────────────────────────────────
void handleStatus() {
    char json[48];
    snprintf(json, sizeof(json), "{\"count\":%u,\"size\":%u}", photoCount, (unsigned)imgSize);
    server.send(200, "application/json", json);
}
void handleLatestJpeg() {
    if (!hasPhoto) {
        server.send(404, "text/plain", "No photo yet");
        return;
    }
    server.sendHeader("Content-Type",   "image/jpeg");
    server.sendHeader("Content-Length", String(imgSize));
    server.sendHeader("Cache-Control",  "no-cache");
    server.send_P(200, "image/jpeg", (const char*)imgBuf, imgSize);
    Serial.printf("Served %u bytes to CYD\n", imgSize);
}

void handleRoot() {
    String html = "<h2>StickV Camera Relay</h2>";
    html += hasPhoto
        ? "<p>Latest: <a href='/latest.jpg'>latest.jpg</a> ("
          + String(imgSize) + " bytes)</p>"
          "<img src='/latest.jpg' style='max-width:320px'>"
        : "<p>Waiting for first photo from StickV...</p>";
    server.send(200, "text/html", html);
}

// ── UART receive ──────────────────────────────────────────────────
void checkUart() {
    if (!StickV.available()) return;

    String header = StickV.readStringUntil('\n');
    header.trim();
    if (!header.startsWith("STICKV_IMG:")) return;

    int size = header.substring(11).toInt();
    if (size <= 0 || size > MAX_IMG_SIZE) {
        Serial.printf("Bad size: %d — flushing\n", size);
        return;
    }

    Serial.printf("Receiving %d bytes...\n", size);
    size_t got = 0;
    unsigned long start = millis();
    while (got < (size_t)size && millis() - start < 10000) {
        if (StickV.available())
            imgBuf[got++] = StickV.read();
    }
    StickV.readStringUntil('\n');  // consume STICKV_END

    if (got == (size_t)size) {
        imgSize  = got;
        hasPhoto = true;
        photoCount++;
        Serial.printf("Stored %u bytes — photo #%u ready\n", imgSize, photoCount);
    } else {
        Serial.printf("Timeout: got %u of %d\n", got, size);
    }
}

void setup() {
    Serial.begin(115200);
    StickV.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Scan for networks so we can confirm CYD AP is visible
    Serial.println("\nScanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        Serial.println("No networks found!");
    } else {
        Serial.printf("%d networks found:\n", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %d: %s (RSSI %d)\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
    }
    WiFi.scanDelete();

    WiFi.begin(WIFI_SSID);   // open AP — no password
    Serial.print("Connecting to CYD AP");
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.printf(".[%d]", WiFi.status());
        if (millis() - t > 15000) {
            Serial.println("\nRetrying...");
            WiFi.disconnect();
            delay(1000);
            WiFi.begin(WIFI_SSID);   // open AP — no password
            t = millis();
        }
    }
    Serial.println("\nIP: " + WiFi.localIP().toString());

    server.on("/",           handleRoot);
    server.on("/status",     handleStatus);
    server.on("/latest.jpg", handleLatestJpeg);
    server.begin();
}

void loop() {
    server.handleClient();
    checkUart();
}
