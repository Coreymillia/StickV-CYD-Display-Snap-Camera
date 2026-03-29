/*
 * StickVCam — CYD Display Receiver  v2.0
 * ----------------------------------------
 * Creates WiFi SoftAP "StickVCam"
 * Discovers C3 relay on the AP, polls /latest.jpg
 * Saves up to MAX_PHOTOS to SPIFFS (ring buffer, persists across reboots)
 * Touch: tap left edge = older photo, right edge = newer photo
 *        tap bottom bar = cycle filter (None → Gray → Invert → Sepia)
 * Filters applied at draw time — no StickV changes needed
 *
 * Hardware: ESP32-2432S028R (CYD)
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <SPIFFS.h>
#include <XPT2046_Touchscreen.h>

// ── Display pins ──────────────────────────────────────────────────
#define TFT_DC   2
#define TFT_CS   15
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_BL   21

// ── Touch pins (XPT2046 on separate VSPI bus) ─────────────────────
#define XPT2046_CS   33
#define XPT2046_IRQ  36
#define XPT2046_CLK  25
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define TOUCH_DEBOUNCE_MS 300

// ── Network ───────────────────────────────────────────────────────
#define AP_SSID  "StickVCam"
#define AP_PASS  ""
#define AP_IP    "192.168.5.1"

// ── Colours ───────────────────────────────────────────────────────
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_GRAY   0x7BEF
#define C_DKGRAY 0x2104
#define C_CYAN   0x07FF
#define C_YELLOW 0xFFE0

// ── Screen / layout ───────────────────────────────────────────────
#define SCREEN_W  320
#define SCREEN_H  240
#define STATUS_H   20   // status bar height at bottom
#define NAV_W      64   // left/right touch zone width

// ── Gallery ───────────────────────────────────────────────────────
#define MAX_PHOTOS  24
#define MAX_JPEG    (50 * 1024)

// ── Filters ───────────────────────────────────────────────────────
enum FilterMode { FILTER_NONE = 0, FILTER_GRAY, FILTER_INVERT, FILTER_SEPIA, FILTER_COUNT };
static const char* filterNames[] = { "None", "Gray", "Invert", "Sepia" };

// ── Globals ───────────────────────────────────────────────────────
static Arduino_DataBus* bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
static Arduino_GFX*     gfx = new Arduino_ILI9341(bus, -1, 1, false);
static JPEGDEC          jpeg;
static SPIClass         touchSPI(VSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static uint8_t  jpegBuf[MAX_JPEG];
static char     c3IP[20]        = "";
static bool     c3Found         = false;
static uint32_t lastC3Count     = 0;
static int      totalSaved      = 0;  // total photos ever written (ring wraps at MAX_PHOTOS)
static int      viewIndex       = 0;  // 0 = newest, increases toward older
static FilterMode curFilter     = FILTER_NONE;
static unsigned long lastTouchMs = 0;

// ── Gallery helpers ───────────────────────────────────────────────
static int galleryCount() { return totalSaved < MAX_PHOTOS ? totalSaved : MAX_PHOTOS; }

static void photoPath(int absIdx, char* buf, size_t len) {
    snprintf(buf, len, "/p%02d.jpg", absIdx % MAX_PHOTOS);
}

// ── Persistent count ──────────────────────────────────────────────
static void loadCount() {
    File f = SPIFFS.open("/count.dat", "r");
    if (!f) { totalSaved = 0; return; }
    if (f.size() >= 4) f.read((uint8_t*)&totalSaved, 4);
    f.close();
    // sanity: verify the newest slot file actually exists
    if (totalSaved > 0) {
        char path[16];
        photoPath(totalSaved - 1, path, sizeof(path));
        if (!SPIFFS.exists(path)) totalSaved = 0;
    }
}

static void saveCount() {
    File f = SPIFFS.open("/count.dat", "w");
    if (!f) return;
    f.write((uint8_t*)&totalSaved, 4);
    f.close();
}

// ── Filter application ────────────────────────────────────────────
static void applyFilter(uint16_t* pixels, int count) {
    if (curFilter == FILTER_NONE) return;
    for (int i = 0; i < count; i++) {
        // Pixels are RGB565 big-endian — swap bytes to get little-endian R5G6B5
        uint16_t le = (pixels[i] >> 8) | (pixels[i] << 8);
        uint8_t r5 = (le >> 11) & 0x1F;
        uint8_t g6 = (le >>  5) & 0x3F;
        uint8_t b5 =  le        & 0x1F;
        // Expand to 8-bit
        int r = (r5 << 3) | (r5 >> 2);
        int g = (g6 << 2) | (g6 >> 4);
        int b = (b5 << 3) | (b5 >> 2);

        switch (curFilter) {
            case FILTER_GRAY: {
                int lum = (r * 77 + g * 150 + b * 29) >> 8;
                r = g = b = lum;
                break;
            }
            case FILTER_INVERT:
                r = 255 - r;  g = 255 - g;  b = 255 - b;
                break;
            case FILTER_SEPIA: {
                int nr = min(255, (r * 101 + g * 197 + b * 48) >> 8);
                int ng = min(255, (r *  89 + g * 176 + b * 43) >> 8);
                int nb = min(255, (r *  70 + g * 137 + b * 34) >> 8);
                r = nr;  g = ng;  b = nb;
                break;
            }
            default: break;
        }

        // Pack back to RGB565 big-endian
        uint16_t result = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        pixels[i] = (result >> 8) | (result << 8);
    }
}

// ── JPEG draw callback ────────────────────────────────────────────
int jpegDrawCB(JPEGDRAW* pDraw) {
    applyFilter(pDraw->pPixels, pDraw->iWidth * pDraw->iHeight);
    gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    return 1;
}

// ── Status bar ────────────────────────────────────────────────────
static void drawStatusBar() {
    gfx->fillRect(0, SCREEN_H - STATUS_H, SCREEN_W, STATUS_H, C_DKGRAY);
    gfx->setTextSize(1);

    // Left: photo index (e.g. "3/8")
    char left[16];
    int gc = galleryCount();
    snprintf(left, sizeof(left), gc > 0 ? "%d/%d" : "0/0", viewIndex + 1, gc);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(4, SCREEN_H - STATUS_H + 6);
    gfx->print(left);

    // Centre: filter name
    const char* fn = filterNames[curFilter];
    gfx->setTextColor(C_CYAN);
    gfx->setCursor((SCREEN_W - (int)strlen(fn) * 6) / 2, SCREEN_H - STATUS_H + 6);
    gfx->print(fn);

    // Right: C3 IP
    gfx->setTextColor(c3Found ? C_GREEN : C_GRAY);
    const char* right = c3Found ? c3IP : "no relay";
    gfx->setCursor(SCREEN_W - (int)strlen(right) * 6 - 4, SCREEN_H - STATUS_H + 6);
    gfx->print(right);
}

// ── Nav arrows (drawn over image edges) ──────────────────────────
static void drawNavArrows() {
    int gc = galleryCount();
    if (gc <= 1) return;
    int midY = (SCREEN_H - STATUS_H) / 2;
    if (viewIndex < gc - 1)              // older exists → left arrow
        gfx->fillTriangle(8, midY, 22, midY - 14, 22, midY + 14, C_GRAY);
    if (viewIndex > 0)                   // newer exists → right arrow
        gfx->fillTriangle(SCREEN_W - 8, midY, SCREEN_W - 22, midY - 14, SCREEN_W - 22, midY + 14, C_GRAY);
}

// ── Decode jpegBuf and draw ───────────────────────────────────────
static bool displayFromBuf(size_t sz) {
    gfx->fillScreen(C_BLACK);
    if (!jpeg.openRAM(jpegBuf, sz, jpegDrawCB)) return false;
    jpeg.setPixelType(RGB565_BIG_ENDIAN);
    jpeg.decode(0, 0, 0);
    jpeg.close();
    drawNavArrows();
    drawStatusBar();
    return true;
}

// ── Load photo from SPIFFS and redisplay ──────────────────────────
static bool redisplay() {
    int gc = galleryCount();
    if (gc == 0) return false;
    int absIdx = totalSaved - 1 - viewIndex;
    char path[16];
    photoPath(absIdx, path, sizeof(path));
    File f = SPIFFS.open(path, "r");
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > MAX_JPEG) { f.close(); return false; }
    f.read(jpegBuf, sz);
    f.close();
    return displayFromBuf(sz);
}

// ── Save incoming JPEG to SPIFFS ring buffer ──────────────────────
static bool savePhoto(uint8_t* data, size_t len) {
    char path[16];
    photoPath(totalSaved, path, sizeof(path));
    File f = SPIFFS.open(path, "w");
    if (!f) return false;
    bool ok = (f.write(data, len) == len);
    f.close();
    if (ok) { totalSaved++;  saveCount();  viewIndex = 0; }
    return ok;
}

// ── Waiting / splash screen ───────────────────────────────────────
static void drawWaiting() {
    gfx->fillScreen(C_BLACK);
    gfx->setTextColor(C_GREEN);
    gfx->setTextSize(2);
    gfx->setCursor(70, 60);
    gfx->println("StickVCam");
    gfx->setTextSize(1);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(70, 98);
    char apLine[32];
    snprintf(apLine, sizeof(apLine), "AP: %s", AP_SSID);
    gfx->println(apLine);
    gfx->setCursor(70, 116);
    gfx->println("Waiting for C3 relay...");
    gfx->setTextColor(C_CYAN);
    gfx->setCursor(70, 134);
    char staBuf[32];
    snprintf(staBuf, sizeof(staBuf), "Stations: %d", WiFi.softAPgetStationNum());
    gfx->print(staBuf);
    gfx->setTextColor(C_YELLOW);
    gfx->setCursor(70, 152);
    char savedBuf[32];
    snprintf(savedBuf, sizeof(savedBuf), "Saved: %d/%d", galleryCount(), MAX_PHOTOS);
    gfx->print(savedBuf);
    drawStatusBar();
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
    if (code != 200) { http.end(); return lastC3Count; }
    String body = http.getString();
    http.end();
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, body)) return lastC3Count;
    return doc["count"] | lastC3Count;
}

// ── Fetch latest.jpg from C3, save, and display ──────────────────
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
    savePhoto(jpegBuf, got);   // save to SPIFFS ring buffer
    displayFromBuf(got);       // display from RAM (already in jpegBuf)
    Serial.printf("[CYD] Photo saved and displayed (%zu bytes, slot %d/%d)\n",
                  got, galleryCount(), MAX_PHOTOS);
    return true;
}

// ── Touch handling ────────────────────────────────────────────────
static void handleTouch() {
    if (!ts.tirqTouched() || !ts.touched()) return;
    unsigned long now = millis();
    if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
    lastTouchMs = now;

    TS_Point p = ts.getPoint();
    int tx = map(p.x, 200, 3700, 0, SCREEN_W);
    int ty = map(p.y, 200, 3700, 0, SCREEN_H);
    int gc = galleryCount();

    // Bottom bar → cycle filter
    if (ty >= SCREEN_H - STATUS_H - 4) {
        curFilter = (FilterMode)((curFilter + 1) % FILTER_COUNT);
        Serial.printf("[CYD] Filter: %s\n", filterNames[curFilter]);
        if (gc > 0) redisplay();
        else        drawStatusBar();
        return;
    }

    if (gc == 0) return;

    // Left zone → older photo
    if (tx < NAV_W && viewIndex < gc - 1) {
        viewIndex++;
        redisplay();
        return;
    }

    // Right zone → newer photo
    if (tx > SCREEN_W - NAV_W && viewIndex > 0) {
        viewIndex--;
        redisplay();
        return;
    }
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    gfx->begin();
    gfx->fillScreen(C_BLACK);

    if (!SPIFFS.begin(true)) {
        Serial.println("[CYD] SPIFFS mount failed — no gallery persistence");
    } else {
        loadCount();
        Serial.printf("[CYD] SPIFFS ok — %d photos in gallery\n", galleryCount());
    }

    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);

    IPAddress apIP(192, 168, 5, 1);
    IPAddress netmask(255, 255, 255, 0);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netmask);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[CYD] AP up: %s\n", AP_SSID);

    drawWaiting();
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
    static unsigned long lastProbe = 0;
    static unsigned long lastPoll  = 0;
    unsigned long now = millis();

    handleTouch();

    // Phase 1: find C3
    if (!c3Found) {
        if (now - lastProbe >= 3000) {
            lastProbe = now;
            drawWaiting();
            if (findC3()) {
                c3Found = true;
                if (galleryCount() > 0) {
                    redisplay();   // show last saved photo while waiting for new ones
                } else {
                    gfx->fillScreen(C_BLACK);
                    gfx->setTextColor(C_GREEN);
                    gfx->setTextSize(1);
                    gfx->setCursor(4, SCREEN_H / 2 - 8);
                    gfx->print("C3 relay online. Press Button A on StickV.");
                    drawStatusBar();
                }
            }
        }
        return;
    }

    // Phase 2: C3 dropped off?
    if (WiFi.softAPgetStationNum() == 0) {
        c3Found = false;
        strlcpy(c3IP, "", sizeof(c3IP));
        drawWaiting();
        return;
    }

    // Phase 3: poll for new photo
    if (now - lastPoll >= 500) {
        lastPoll = now;
        uint32_t count = getC3Count();
        if (count != lastC3Count) {
            lastC3Count = count;
            Serial.printf("[CYD] New photo from C3 — fetching...\n");
            fetchAndDisplay();
        }
    }
}
