/*
 * StickVCam — CYD Display Receiver
 * ----------------------------------
 * Creates WiFi SoftAP "StickVCam"
 * Discovers C3 relay on the AP, polls /latest.jpg
 * Decodes JPEG and displays full-screen on ILI9341 320×240
 *
 * Hardware: ESP32-2432S028R (CYD) — same as HashCYDCluster
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>

// ── Display pins (identical to HashCYDCluster) ────────────────────
#define TFT_DC   2
#define TFT_CS   15
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_BL   21

// ── Network ───────────────────────────────────────────────────────
#define AP_SSID  "StickVCam"
#define AP_PASS  ""          // open AP — no password
#define AP_IP    "192.168.5.1"

// ── Colours ───────────────────────────────────────────────────────
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_GRAY   0x7BEF
#define C_DKGRAY 0x2104
#define C_CYAN   0x07FF

// ── Screen (landscape) ───────────────────────────────────────────
#define SCREEN_W  320
#define SCREEN_H  240
#define STATUS_H   16   // status bar height at bottom

// ── JPEG buffer ───────────────────────────────────────────────────
#define MAX_JPEG  (50 * 1024)
static uint8_t jpegBuf[MAX_JPEG];

// ── Globals ───────────────────────────────────────────────────────
static Arduino_DataBus* bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
static Arduino_GFX*     gfx = new Arduino_ILI9341(bus, -1, 1, false);  // rotation 1 = landscape
static JPEGDEC          jpeg;

static char     c3IP[20]    = "";
static bool     c3Found     = false;
static uint32_t lastCount   = 0;
static uint32_t displayCount = 0;

// ── JPEG draw callback ────────────────────────────────────────────
int jpegDrawCB(JPEGDRAW* pDraw) {
    gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    return 1;
}

// ── Status bar (overlaid at bottom of screen) ─────────────────────
void drawStatus(const char* left, const char* right, uint16_t colour) {
    gfx->fillRect(0, SCREEN_H - STATUS_H, SCREEN_W, STATUS_H, C_DKGRAY);
    gfx->setTextSize(1);
    gfx->setTextColor(colour);
    gfx->setCursor(4, SCREEN_H - STATUS_H + 4);
    gfx->print(left);
    if (right && right[0]) {
        gfx->setCursor(SCREEN_W - (strlen(right) * 6) - 4, SCREEN_H - STATUS_H + 4);
        gfx->print(right);
    }
}

// ── Waiting / splash screen ───────────────────────────────────────
void drawWaiting() {
    gfx->fillScreen(C_BLACK);
    gfx->setTextColor(C_GREEN);
    gfx->setTextSize(2);
    gfx->setCursor(70, 70);
    gfx->println("StickVCam");
    gfx->setTextSize(1);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(70, 102);
    char apLine[32], pwLine[32];
    snprintf(apLine, sizeof(apLine), "AP: %-16s", AP_SSID);
    snprintf(pwLine, sizeof(pwLine), "PW: %s", AP_PASS);
    gfx->print(apLine);
    gfx->setCursor(70, 116);
    gfx->print(pwLine);
    gfx->setCursor(70, 138);
    gfx->println("Waiting for C3 relay...");
    gfx->setTextColor(C_CYAN);
    gfx->setCursor(70, 154);
    char staBuf[32];
    snprintf(staBuf, sizeof(staBuf), "Stations: %d", WiFi.softAPgetStationNum());
    gfx->print(staBuf);
    drawStatus(AP_IP, nullptr, C_GRAY);
}

// ── Probe one IP for a live C3 relay ─────────────────────────────
static bool probeIP(const char* ip) {
    char url[48];
    snprintf(url, sizeof(url), "http://%s/status", ip);
    HTTPClient http;
    http.begin(url);
    http.setTimeout(2000);
    int code = http.GET();
    http.end();
    return (code == 200);
}

static bool findC3() {
    for (int i = 2; i <= 10; i++) {
        char ip[20];
        snprintf(ip, sizeof(ip), "192.168.5.%d", i);
        if (probeIP(ip)) {
            strlcpy(c3IP, ip, sizeof(c3IP));
            Serial.printf("[CYD] C3 found at %s\n", c3IP);
            return true;
        }
    }
    return false;
}

// ── Poll C3 photo count ───────────────────────────────────────────
static uint32_t getC3Count() {
    char url[48];
    snprintf(url, sizeof(url), "http://%s/status", c3IP);
    HTTPClient http;
    http.begin(url);
    http.setTimeout(1000);
    int code = http.GET();
    if (code != 200) { http.end(); return lastCount; }
    String body = http.getString();
    http.end();
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, body)) return lastCount;
    return doc["count"] | lastCount;
}

// ── Fetch and display latest photo ────────────────────────────────
static bool fetchAndDisplay() {
    char url[48];
    snprintf(url, sizeof(url), "http://%s/latest.jpg", c3IP);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(6000);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    int len = http.getSize();
    if (len <= 0 || len > MAX_JPEG) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    size_t got = 0;
    unsigned long t = millis();
    while (http.connected() && got < (size_t)len && millis() - t < 6000) {
        size_t avail = stream->available();
        if (avail) got += stream->readBytes(jpegBuf + got, min(avail, (size_t)(len - got)));
        delay(1);
    }
    http.end();

    if (got < 4) return false;

    // Decode to screen (image starts at y=0, status bar overlaid after)
    if (jpeg.openRAM(jpegBuf, got, jpegDrawCB)) {
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.decode(0, 0, 0);
        jpeg.close();
        displayCount++;
        return true;
    }
    return false;
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    gfx->begin();
    gfx->fillScreen(C_BLACK);

    IPAddress apIP(192, 168, 5, 1);
    IPAddress netmask(255, 255, 255, 0);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netmask);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[CYD] AP up: %s  %s\n", AP_SSID, AP_IP);

    drawWaiting();
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
    static unsigned long lastProbe = 0;
    static unsigned long lastPoll  = 0;
    unsigned long now = millis();

    // ── Phase 1: find C3 ────────────────────────────────────────
    if (!c3Found) {
        if (now - lastProbe >= 3000) {
            lastProbe = now;
            drawWaiting();   // refreshes station count on screen
            if (findC3()) {
                c3Found = true;
                gfx->fillScreen(C_BLACK);
                gfx->setTextColor(C_GREEN);
                gfx->setTextSize(1);
                gfx->setCursor(4, SCREEN_H / 2 - 8);
                gfx->print("C3 relay online. Press Button A on StickV.");
                char rstat[32];
                snprintf(rstat, sizeof(rstat), "C3 @ %s", c3IP);
                drawStatus(rstat, "ready", C_GREEN);
            }
        }
        return;
    }

    // ── Phase 2: check if C3 dropped off ────────────────────────
    if (WiFi.softAPgetStationNum() == 0) {
        c3Found = false;
        lastCount = 0;
        strlcpy(c3IP, "", sizeof(c3IP));
        drawWaiting();
        return;
    }

    // ── Phase 3: poll for new photo ──────────────────────────────
    if (now - lastPoll >= 500) {
        lastPoll = now;
        uint32_t count = getC3Count();
        if (count != lastCount) {
            lastCount = count;
            Serial.printf("[CYD] New photo #%u — fetching...\n", count);
            if (fetchAndDisplay()) {
                char left[32], right[16];
                snprintf(left,  sizeof(left),  "C3 @ %s", c3IP);
                snprintf(right, sizeof(right), "#%u", displayCount);
                drawStatus(left, right, C_GREEN);
                Serial.printf("[CYD] Displayed photo #%u\n", displayCount);
            }
        }
    }
}
