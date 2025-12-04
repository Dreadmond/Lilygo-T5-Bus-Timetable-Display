#include "display.h"
#include "epd_driver.h"
#include "esp_heap_caps.h"
#include "firasans.h"
#include <time.h>
#include <vector>
#include <cmath>
#include "zlib/zlib.h"

DisplayManager display;

static const int SCREEN_MARGIN = 10;
static const int HERO_HEIGHT = 60;
static const int HERO_CONTENT_PADDING = 10;
static const int HERO_COLUMN_GAP = 12;
static const int HERO_WIDTH = EPD_WIDTH - (SCREEN_MARGIN * 2);
static const int HERO_INNER_WIDTH = HERO_WIDTH - (HERO_CONTENT_PADDING * 2);
static const int HERO_TIME_WIDTH = 140;
static const int HERO_BATTERY_WIDTH = 140;
static const int HERO_DIRECTION_WIDTH = HERO_INNER_WIDTH - HERO_TIME_WIDTH - HERO_BATTERY_WIDTH - (HERO_COLUMN_GAP * 2);
static const float HERO_FONT_SCALE = 0.8f;
static const float RIGHT_COLUMN_SCALE = 0.78f;
static const int CARD_MAX_COUNT = 3;
static const int MIN_CARD_COUNT = 3;
static const int CARD_SPACING = 12;
static const int CARD_STACK_TOP = SCREEN_MARGIN + HERO_HEIGHT + SCREEN_MARGIN;
static const int CARD_STACK_HEIGHT = EPD_HEIGHT - SCREEN_MARGIN - CARD_STACK_TOP;
static const int CARD_HEIGHT = (CARD_STACK_HEIGHT - ((MIN_CARD_COUNT - 1) * CARD_SPACING)) / MIN_CARD_COUNT;
static const int BATTERY_ICON_WIDTH = 48;
static const int BATTERY_ICON_HEIGHT = 27;
static const int STOP_COLUMN_REDUCTION = 150;
static const int ARRIVAL_COLUMN_WIDTH = 110;
static const int LEAVE_COLUMN_WIDTH = 60;
static const int LEAVE_COLUMN_OFFSET = 200;
static const int NAME_BLOCK_EXTRA_WIDTH = 260;
static const int ARRIVAL_COLUMN_OFFSET = 100;

static_assert(HERO_DIRECTION_WIDTH > 0, "Hero direction width must remain positive");
static_assert(CARD_STACK_HEIGHT > 0, "Card stack must have positive height");
static_assert(CARD_HEIGHT > 0, "Card height must remain positive");
static_assert((CARD_HEIGHT * MIN_CARD_COUNT) + (CARD_SPACING * (MIN_CARD_COUNT - 1)) == CARD_STACK_HEIGHT,
              "Card stack math must exactly fill the allotted area");

enum LayoutRegion {
    LAYOUT_HERO,
    LAYOUT_HERO_TIME,
    LAYOUT_HERO_DIRECTION,
    LAYOUT_HERO_BATTERY,
    LAYOUT_CARD_STACK,
    LAYOUT_COUNT
};

struct LayoutSlot {
    LayoutRegion id;
    const char* label;
    int x;
    int y;
    int width;
    int height;
};

static const LayoutSlot kLayoutTable[LAYOUT_COUNT] = {
    {LAYOUT_HERO, "Hero background", SCREEN_MARGIN, SCREEN_MARGIN, HERO_WIDTH, HERO_HEIGHT},
    {LAYOUT_HERO_TIME, "Hero time", SCREEN_MARGIN + HERO_CONTENT_PADDING, SCREEN_MARGIN,
        HERO_TIME_WIDTH, HERO_HEIGHT},
    {LAYOUT_HERO_DIRECTION, "Hero direction",
        SCREEN_MARGIN + HERO_CONTENT_PADDING + HERO_TIME_WIDTH + HERO_COLUMN_GAP,
        SCREEN_MARGIN,
        HERO_DIRECTION_WIDTH, HERO_HEIGHT},
    {LAYOUT_HERO_BATTERY, "Hero battery",
        SCREEN_MARGIN + HERO_CONTENT_PADDING + HERO_TIME_WIDTH + HERO_COLUMN_GAP + HERO_DIRECTION_WIDTH + HERO_COLUMN_GAP,
        SCREEN_MARGIN,
        HERO_BATTERY_WIDTH, HERO_HEIGHT},
    {LAYOUT_CARD_STACK, "Card stack", SCREEN_MARGIN, CARD_STACK_TOP, HERO_WIDTH, CARD_STACK_HEIGHT}
};

static const LayoutSlot& layoutSlot(LayoutRegion id) {
    return kLayoutTable[id];
}

// No placeholder data - only show real bus times

DisplayManager::DisplayManager() {
    frameBuffer = nullptr;
    initialized = false;
    lastFullRefresh = 0;
    partialRefreshCount = 0;
    placeholderBannerVisible = false;
    placeholderYOffset = 0;
    lastTimeStr = "";
    lastBatteryPercent = -1;
    loadingLogActive = false;
    loadingLogCursorY = SCREEN_MARGIN + 40;
    for (int i = 0; i < CARD_MAX_COUNT; i++) lastLeaveIn[i] = -999;
}

void DisplayManager::init() {
    if (initialized) return;
    epd_init();
    frameBuffer = (uint8_t*)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
    if (!frameBuffer) frameBuffer = (uint8_t*)malloc(EPD_WIDTH * EPD_HEIGHT / 2);
    if (!frameBuffer) { DEBUG_PRINTLN("FATAL: No buffer"); return; }
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    initialized = true;
    lastFullRefresh = millis();
    DEBUG_PRINTLN("Display OK");
}

void DisplayManager::clear() {
    if (!initialized) return;
    epd_poweron(); epd_clear(); epd_poweroff_all();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    partialRefreshCount = 0;
    lastFullRefresh = millis();
}

void DisplayManager::fullRefresh() {
    if (!initialized) return;
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    partialRefreshCount = 0;
    lastFullRefresh = millis();
}

void DisplayManager::partialRefresh(ScreenRegion r) {
    if (!initialized) return;
    if (needsFullRefresh()) { fullRefresh(); return; }
    Rect_t a = {r.x, r.y, r.width, r.height};
    epd_poweron();
    epd_draw_grayscale_image(a, frameBuffer);
    epd_poweroff_all();
    partialRefreshCount++;
}

void DisplayManager::fastRefresh() {
    if (!initialized) return;
    epd_poweron();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    partialRefreshCount++;
}

bool DisplayManager::needsFullRefresh() const {
    return (millis() - lastFullRefresh > 1800000) || (partialRefreshCount >= 12);
}

void DisplayManager::resetFullRefreshTimer() {
    lastFullRefresh = millis();
    partialRefreshCount = 0;
}

void DisplayManager::resetLoadingLog() {
    loadingLogActive = false;
    loadingLogCursorY = SCREEN_MARGIN + 40;
}

void DisplayManager::showBusTimetable(BusDeparture departures[], int count,
                                       String currentTime, String direction,
                                       int batteryPercent, bool wifiConnected,
                                       bool placeholderMode) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    logLayoutTable();
    
    char dateBuf[32] = "--";
    struct tm timeinfo;
    bool haveLocalTime = getLocalTime(&timeinfo);
    if (haveLocalTime) {
        strftime(dateBuf, sizeof(dateBuf), "%A %d %b", &timeinfo);
    }
    String timeLabel = currentTime.length() ? currentTime : "--:--";
    if ((currentTime.length() == 0 || currentTime == "--:--") && haveLocalTime) {
        char timeBuf[6];
        if (strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo) > 0) {
            timeLabel = String(timeBuf);
        }
    }
    
    // ===== HERO HEADER =====
    const LayoutSlot& heroRect = layoutSlot(LAYOUT_HERO);
    const LayoutSlot& heroTimeRect = layoutSlot(LAYOUT_HERO_TIME);
    const LayoutSlot& heroDirectionRect = layoutSlot(LAYOUT_HERO_DIRECTION);
    const LayoutSlot& heroBatteryRect = layoutSlot(LAYOUT_HERO_BATTERY);
    epd_fill_rect(heroRect.x, heroRect.y, heroRect.width, heroRect.height, 230, frameBuffer);
    epd_draw_rect(heroRect.x, heroRect.y, heroRect.width, heroRect.height, 200, frameBuffer);
    
    String dirLine = direction.length() ? direction : "Departures";
    drawScaledTextInRect(heroTimeRect.x, heroTimeRect.y, heroTimeRect.width, heroTimeRect.height,
                         timeLabel, HERO_FONT_SCALE, TextAlignment::LEFT);
    drawScaledTextInRect(heroDirectionRect.x, heroDirectionRect.y, heroDirectionRect.width, heroDirectionRect.height,
                         dirLine, HERO_FONT_SCALE, TextAlignment::CENTER);
    
    int iconX = heroBatteryRect.x + heroBatteryRect.width - BATTERY_ICON_WIDTH;
    int iconY = heroBatteryRect.y + (heroBatteryRect.height - BATTERY_ICON_HEIGHT) / 2;
    drawBatteryIcon(iconX, iconY, batteryPercent);
    
    // ===== BUS CARDS =====
    const LayoutSlot& cardsArea = layoutSlot(LAYOUT_CARD_STACK);
    const int cardLeft = cardsArea.x;
    const int cardWidth = cardsArea.width;
    const int cardAreaTop = cardsArea.y;
    
    // Only show actual departures - no placeholders
    int actualCount = min(count, CARD_MAX_COUNT);
    
    for (int i = 0; i < actualCount; i++) {
        int cardTop = cardAreaTop + i * (CARD_HEIGHT + CARD_SPACING);
        drawBusCard(i, departures[i], i == 0, false,
                    cardTop, CARD_HEIGHT, cardLeft, cardWidth);
        int leaveIn = departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes;
        if (leaveIn < 0) leaveIn = 0;
        lastLeaveIn[i] = leaveIn;
    }
    
    // Show message if no buses available
    if (actualCount == 0) {
        int32_t x = cardLeft + 20;
        int32_t y = cardAreaTop + 80;
        writeln((GFXfont*)&FiraSans, "No buses available", &x, &y, frameBuffer);
    }
    
    // Refresh display
    epd_poweron();
    epd_clear();
    delay(100);
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    
    lastTimeStr = timeLabel;
    lastBatteryPercent = batteryPercent;
    partialRefreshCount = 0;
    lastFullRefresh = millis();
}

void DisplayManager::logLayoutTable() const {
    DEBUG_PRINTLN("Layout table (landscape, 10px margin):");
    for (int i = 0; i < LAYOUT_COUNT; i++) {
        const LayoutSlot& slot = kLayoutTable[i];
        DEBUG_PRINTF("  %-16s x=%3d y=%3d w=%3d h=%3d\n",
                     slot.label, slot.x, slot.y, slot.width, slot.height);
    }
    const LayoutSlot& cardsArea = layoutSlot(LAYOUT_CARD_STACK);
    for (int i = 0; i < CARD_MAX_COUNT; i++) {
        int cardTop = cardsArea.y + i * (CARD_HEIGHT + CARD_SPACING);
        DEBUG_PRINTF("  Card %d           x=%3d y=%3d w=%3d h=%3d\n",
                     i + 1, cardsArea.x, cardTop, cardsArea.width, CARD_HEIGHT);
    }
    DEBUG_PRINTF("  Cards: %d slots @ %dpx tall, spacing %dpx\n",
                 CARD_MAX_COUNT, CARD_HEIGHT, CARD_SPACING);
}

void DisplayManager::drawBusCard(int cardIndex, const BusDeparture& departure, bool highlight, bool placeholderMode,
                                 int cardTop, int cardHeight, int cardLeft, int cardWidth) {
    if (!frameBuffer) return;
    
    uint8_t bg = highlight ? 215 : 245;
    
    epd_fill_rect(cardLeft, cardTop, cardWidth, cardHeight, bg, frameBuffer);
    epd_draw_rect(cardLeft, cardTop, cardWidth, cardHeight, 160, frameBuffer);
    if (highlight) {
        epd_fill_rect(cardLeft + 6, cardTop + 12, 6, cardHeight - 24, 0, frameBuffer);
    }
    
    int paddingTop = 24;
    int paddingBottom = 20;
    int innerTop = cardTop + paddingTop;
    int innerHeight = cardHeight - paddingTop - paddingBottom;
    if (innerHeight < 20) innerHeight = cardHeight - 10;
    
    const int colSpacing = 10;
    const int cardRight = cardLeft + cardWidth;
    const int leftAreaLeft = cardLeft + colSpacing;
    const int leaveAreaWidth = LEAVE_COLUMN_WIDTH;
    int leaveAreaLeft = cardRight - colSpacing - leaveAreaWidth - LEAVE_COLUMN_OFFSET;
    int minLeaveAreaLeft = leftAreaLeft + (colSpacing * 2) + ARRIVAL_COLUMN_WIDTH;
    if (leaveAreaLeft < minLeaveAreaLeft) leaveAreaLeft = minLeaveAreaLeft;
    const int arrivalAreaWidth = ARRIVAL_COLUMN_WIDTH;
    int desiredArrivalLeft = cardLeft + ((cardWidth * 2) / 3) - STOP_COLUMN_REDUCTION + NAME_BLOCK_EXTRA_WIDTH - ARRIVAL_COLUMN_OFFSET;
    int maxArrivalLeft = leaveAreaLeft - colSpacing - arrivalAreaWidth;
    int minArrivalLeft = leftAreaLeft + colSpacing + 100;
    int arrivalAreaLeft = constrain(desiredArrivalLeft, minArrivalLeft, maxArrivalLeft);
    
    int leftAreaWidth = max(120, arrivalAreaLeft - leftAreaLeft - colSpacing);
    
    int busColWidth = min(120, leftAreaWidth / 3);
    if (busColWidth < 80) busColWidth = 80;
    int busColLeft = leftAreaLeft;
    int infoColLeft = busColLeft + busColWidth + colSpacing;
    int infoColWidth = max(40, (leftAreaLeft + leftAreaWidth) - infoColLeft);
    
    drawTextCenteredInRect(busColLeft, innerTop, busColWidth, innerHeight, departure.busNumber);
    
    String destination = departure.destination.length() ? departure.destination : departure.statusText;
    char walk[32];
    snprintf(walk, sizeof(walk), "%d min walk", departure.walkingTimeMinutes);
    String statusLine = departure.statusText.length() ? departure.statusText : (departure.isLive ? "Live data" : "Scheduled");
    if (placeholderMode && cardIndex == 0) {
        statusLine = "Live data unavailable";
    }
    String infoBlock = departure.stopName + "\n" + destination + "\n" + String(walk) + "\n" + statusLine;
    drawWrappedTextBlock(infoColLeft, innerTop, infoColWidth, innerHeight, infoBlock, false);
    
    int leaveIn = departure.minutesUntilDeparture - departure.walkingTimeMinutes;
    if (leaveIn < 0) leaveIn = 0;
    String timeLine = departure.departureTime;
    String leaveLine = (leaveIn <= 0)
        ? "Leave now"
        : "Leave in " + String(leaveIn) + (leaveIn == 1 ? " min" : " mins");
    
    drawScaledTextInRect(arrivalAreaLeft, innerTop, arrivalAreaWidth, innerHeight, timeLine, RIGHT_COLUMN_SCALE, TextAlignment::LEFT);
    
    drawScaledTextInRect(leaveAreaLeft, innerTop, leaveAreaWidth, innerHeight, leaveLine, RIGHT_COLUMN_SCALE, TextAlignment::LEFT);
}

// Placeholder function removed - only showing real data

String DisplayManager::formatTimeOffset(int minutesAhead) const {
    if (minutesAhead < 0) minutesAhead = 0;
    time_t now = time(nullptr);
    if (now <= 0) return "--:--";
    now += minutesAhead * 60;
    struct tm future;
    if (!localtime_r(&now, &future)) return "--:--";
    char buf[6];
    if (strftime(buf, sizeof(buf), "%H:%M", &future) == 0) {
        return "--:--";
    }
    return String(buf);
}

int DisplayManager::measureTextAdvance(const String& text) const {
    const GFXfont* font = (GFXfont*)&FiraSans;
    int advance = 0;
    const char* raw = text.c_str();
    while (*raw) {
        uint8_t c = (uint8_t)*raw++;
        GFXglyph* glyph;
        get_glyph((GFXfont*)font, c, &glyph);
        if (!glyph) continue;
        advance += glyph->advance_x;
    }
    return advance;
}

void DisplayManager::drawScaledTextInRect(int left, int top, int width, int height, const String& text, float scale, TextAlignment alignment) {
    if (!frameBuffer || text.length() == 0) return;
    const float clampedScale = constrain(scale, 0.1f, 1.0f);
    const GFXfont* font = (GFXfont*)&FiraSans;
    int rawHeight = getTextHeight(font);
    int scaledHeight = max(1, (int)ceil(rawHeight * clampedScale));
    int baseline = top + ((height - scaledHeight) / 2) + scaledHeight;
    int advance = measureTextAdvance(text);
    int scaledAdvance = max(1, (int)ceil(advance * clampedScale));
    int cursorX = left;
    if (alignment == TextAlignment::CENTER) {
        cursorX += max(0, (width - scaledAdvance) / 2);
    } else if (alignment == TextAlignment::RIGHT) {
        cursorX += max(0, width - scaledAdvance);
    }
    drawScaledGlyphRun(text, cursorX, baseline, clampedScale);
}

void DisplayManager::drawScaledGlyphRun(const String& text, int startX, int baselineY, float scale) {
    if (!frameBuffer) return;
    const GFXfont* font = (GFXfont*)&FiraSans;
    std::vector<uint8_t> scratch;
    int cursorX = startX;
    const char* raw = text.c_str();
    while (*raw) {
        uint8_t c = (uint8_t)*raw++;
        GFXglyph* glyph;
        get_glyph((GFXfont*)font, c, &glyph);
        if (!glyph) continue;
        int byteWidth = (glyph->width / 2) + (glyph->width & 1);
        size_t bitmapSize = byteWidth * glyph->height;
        const uint8_t* bitmap = font->bitmap + glyph->data_offset;
        scratch.clear();
        if (font->compressed && bitmapSize > 0) {
            scratch.resize(bitmapSize);
            uLongf destLen = bitmapSize;
            if (uncompress(scratch.data(), &destLen, font->bitmap + glyph->data_offset, glyph->compressed_size) != Z_OK) {
                cursorX += max(1, (int)ceil(glyph->advance_x * scale));
                continue;
            }
            bitmap = scratch.data();
        }
        drawScaledGlyph(glyph, bitmap, byteWidth, cursorX, baselineY, scale);
        cursorX += max(1, (int)ceil(glyph->advance_x * scale));
    }
}

void DisplayManager::drawScaledGlyph(const GFXglyph* glyph, const uint8_t* bitmap, int byteWidth, int startX, int baselineY, float scale) {
    if (!glyph || !bitmap || !frameBuffer) return;
    if (glyph->width == 0 || glyph->height == 0) return;
    int scaledLeft = (int)floor(glyph->left * scale);
    int scaledTop = (int)ceil(glyph->top * scale);
    int scaledWidth = max(1, (int)ceil(glyph->width * scale));
    int scaledHeight = max(1, (int)ceil(glyph->height * scale));
    
    for (int dy = 0; dy < scaledHeight; dy++) {
        int sourceY = min(glyph->height - 1, (int)floor(dy / scale));
        for (int dx = 0; dx < scaledWidth; dx++) {
            int sourceX = min(glyph->width - 1, (int)floor(dx / scale));
            uint8_t raw = bitmap[sourceY * byteWidth + sourceX / 2];
            uint8_t nibble = (sourceX & 1) == 0 ? (raw & 0x0F) : (raw >> 4);
            if (nibble == 0) continue;
            int destX = startX + scaledLeft + dx;
            int destY = baselineY - scaledTop + dy;
            writePixelToBuffer(destX, destY, 15 - nibble);
        }
    }
}

void DisplayManager::writePixelToBuffer(int x, int y, uint8_t color) {
    if (!frameBuffer) return;
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    uint8_t value = color & 0x0F;
    int byteIndex = (y * (EPD_WIDTH / 2)) + (x / 2);
    uint8_t existing = frameBuffer[byteIndex];
    if ((x & 1) == 0) {
        frameBuffer[byteIndex] = (existing & 0xF0) | value;
    } else {
        frameBuffer[byteIndex] = (existing & 0x0F) | (value << 4);
    }
}

void DisplayManager::updateCountdownsOnly(BusDeparture[], int) {}


void DisplayManager::updateFooter(int, bool) {}

void DisplayManager::showError(const String& msg) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    int32_t x = 300, y = 270;
    writeln((GFXfont*)&FiraSans, "Error", &x, &y, frameBuffer);
    x = 200; y = 320;
    writeln((GFXfont*)&FiraSans, msg.c_str(), &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
}

void DisplayManager::showLoading(const String& msg) {
    if (!initialized || !frameBuffer) return;
    const GFXfont* font = (GFXfont*)&FiraSans;
    int lineHeight = getTextHeight(font) + 8;
    if (!loadingLogActive || (loadingLogCursorY + lineHeight > EPD_HEIGHT - SCREEN_MARGIN)) {
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
        int32_t titleX = SCREEN_MARGIN + 10;
        int32_t titleY = SCREEN_MARGIN + 40;
        writeln((GFXfont*)font, "Loading...", &titleX, &titleY, frameBuffer);
        loadingLogCursorY = titleY + lineHeight;
        loadingLogActive = true;
    }
    
    String line = msg.length() ? msg : String("...");
    int32_t x = SCREEN_MARGIN + 10;
    int32_t y = loadingLogCursorY;
    writeln((GFXfont*)font, line.c_str(), &x, &y, frameBuffer);
    loadingLogCursorY = y + lineHeight;
    
    epd_poweron();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
}

void DisplayManager::showNoData(const String& msg) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    int32_t x = 350, y = 270;
    writeln((GFXfont*)&FiraSans, "No Data", &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
}

void DisplayManager::showLowBattery(int pct) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "Low Battery: %d%%", pct);
    int32_t x = 300, y = 270;
    writeln((GFXfont*)&FiraSans, buf, &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
}

void DisplayManager::showConnectionStatus(bool wifi, bool mqtt) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    int32_t x = 350, y = 250;
    writeln((GFXfont*)&FiraSans, "Status", &x, &y, frameBuffer);
    x = 300; y = 300;
    writeln((GFXfont*)&FiraSans, wifi ? "WiFi: OK" : "WiFi: FAIL", &x, &y, frameBuffer);
    x = 300; y = 340;
    writeln((GFXfont*)&FiraSans, mqtt ? "MQTT: OK" : "MQTT: FAIL", &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
}

void DisplayManager::updateTimeOnly(const String&) {}
void DisplayManager::updateStatusBarOnly(int, bool) {}
ScreenRegion DisplayManager::getHeaderRegion() const { return {0,0,EPD_WIDTH,60}; }
ScreenRegion DisplayManager::getCardRegion(int) const { return {0,0,0,0}; }
ScreenRegion DisplayManager::getStatusBarRegion() const { return {0,EPD_HEIGHT-30,EPD_WIDTH,30}; }
ScreenRegion DisplayManager::getTimeRegion() const { return {40,10,150,50}; }

void DisplayManager::drawText(int x, int y, const String& t, const GFXfont* f, uint8_t c) {
    if (!frameBuffer) return;
    int32_t cx=x, cy=y;
    writeln((GFXfont*)f, t.c_str(), &cx, &cy, frameBuffer);
}
void DisplayManager::drawCenteredText(int y, const String& t, const GFXfont* f, uint8_t c) {
    drawText((EPD_WIDTH - getTextWidth(t,f))/2, y, t, f, c);
}
void DisplayManager::drawRightAlignedText(int x, int y, const String& t, const GFXfont* f, uint8_t c) {
    drawText(x - getTextWidth(t,f), y, t, f, c);
}
void DisplayManager::drawRect(int x, int y, int w, int h, uint8_t c) {
    if (frameBuffer) epd_draw_rect(x,y,w,h,c,frameBuffer);
}
void DisplayManager::drawFilledRect(int x, int y, int w, int h, uint8_t c) {
    if (frameBuffer) epd_fill_rect(x,y,w,h,c,frameBuffer);
}
void DisplayManager::drawCircle(int x, int y, int r, uint8_t c) {
    if (frameBuffer) epd_draw_circle(x,y,r,c,frameBuffer);
}
void DisplayManager::drawFilledCircle(int x, int y, int r, uint8_t c) {
    if (frameBuffer) epd_fill_circle(x,y,r,c,frameBuffer);
}
void DisplayManager::drawRoundedRect(int x, int y, int w, int h, int r, uint8_t c) { drawRect(x,y,w,h,c); }
void DisplayManager::drawFilledRoundedRect(int x, int y, int w, int h, int r, uint8_t c) { drawFilledRect(x,y,w,h,c); }
void DisplayManager::drawLine(int x1, int y1, int x2, int y2, uint8_t c) {
    if (frameBuffer) epd_draw_line(x1,y1,x2,y2,c,frameBuffer);
}
void DisplayManager::drawHorizontalDivider(int y) { drawLine(40,y,EPD_WIDTH-40,y,128); }
void DisplayManager::drawGradientRect(int x, int y, int w, int h, uint8_t s, uint8_t e) { drawFilledRect(x,y,w,h,s); }
void DisplayManager::drawShadow(int x, int y, int w, int h, int sz) {}

int DisplayManager::getTextWidth(const String& t, const GFXfont* f) {
    int32_t x1 = 0, y1 = 0, minX, minY, maxX, maxY;
    get_text_bounds((GFXfont*)f, t.c_str(), &x1, &y1, &minX, &minY, &maxX, &maxY, NULL);
    return maxX - minX;
}
int DisplayManager::getTextHeight(const GFXfont* f) {
    int32_t x1 = 0, y1 = 0, minX, minY, maxX, maxY;
    get_text_bounds((GFXfont*)f, "Ay", &x1, &y1, &minX, &minY, &maxX, &maxY, NULL);
    return maxY - minY;
}

void DisplayManager::drawTextCenteredInRect(int left, int top, int width, int height, const String& text) {
    if (!frameBuffer || width <= 0 || height <= 0) return;
    const GFXfont* font = (GFXfont*)&FiraSans;
    int textWidth = getTextWidth(text, font);
    int textHeight = getTextHeight(font);
    int32_t x = left + (width - textWidth) / 2;
    if (x < left) x = left;
    int32_t y = top + (height - textHeight) / 2 + textHeight;
    writeln((GFXfont*)font, text.c_str(), &x, &y, frameBuffer);
}

int DisplayManager::drawWrappedTextBlock(int left, int top, int width, int maxHeight, const String& text, bool center) {
    if (!frameBuffer || width <= 0 || maxHeight <= 0) return top;
    const GFXfont* font = (GFXfont*)&FiraSans;
    int textHeight = getTextHeight(font);
    if (textHeight <= 0) textHeight = 24;
    int lineHeight = textHeight + 4;
    int maxLines = maxHeight / lineHeight;
    if (maxLines < 1) maxLines = 1;
    static const int MAX_BUFFERED_LINES = 16;
    if (maxLines > MAX_BUFFERED_LINES) {
        maxLines = MAX_BUFFERED_LINES;
    }
    
    String bufferedLines[MAX_BUFFERED_LINES];
    int lineCount = 0;
    String currentLine = "";
    
    for (int i = 0; i <= text.length() && lineCount < maxLines; i++) {
        char c = (i < text.length()) ? text[i] : '\n';
        if (c == '\n') {
            bufferedLines[lineCount++] = currentLine;
            currentLine = "";
        } else {
            currentLine += c;
            if (getTextWidth(currentLine, font) > width && currentLine.length() > 1) {
                char last = currentLine.charAt(currentLine.length() - 1);
                currentLine.remove(currentLine.length() - 1);
                bufferedLines[lineCount++] = currentLine;
                currentLine = String(last);
            }
        }
        
        if (lineCount >= maxLines) break;
    }
    
    if (currentLine.length() > 0 && lineCount < maxLines) {
        bufferedLines[lineCount++] = currentLine;
    }
    if (lineCount == 0) {
        bufferedLines[lineCount++] = "";
    }
    
    int contentHeight = lineCount * lineHeight;
    if (contentHeight > maxHeight) {
        contentHeight = maxHeight;
    }
    int verticalOffset = (maxHeight - contentHeight) / 2;
    
    for (int i = 0; i < lineCount; i++) {
        const String& line = bufferedLines[i];
        if (!line.length()) continue;
        int lineWidth = getTextWidth(line, font);
        int32_t drawX = center ? left + (width - lineWidth) / 2 : left;
        if (drawX < left) drawX = left;
        int32_t drawY = top + verticalOffset + i * lineHeight + textHeight;
        if (drawY > top + maxHeight) break;
        writeln((GFXfont*)font, line.c_str(), &drawX, &drawY, frameBuffer);
    }
    
    return top + maxHeight;
}
void DisplayManager::pushRegionToDisplay(ScreenRegion r, UpdateMode m) {
    Rect_t a = {r.x, r.y, r.width, r.height};
    epd_poweron();
    if (m == UPDATE_MODE_FULL) epd_clear_area(a);
    epd_draw_grayscale_image(a, frameBuffer);
    epd_poweroff_all();
}

Rect_t DisplayManager::clampToScreen(Rect_t rect) const {
    Rect_t r = rect;
    if (r.x < 0) {
        r.width += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.height += r.y;
        r.y = 0;
    }
    int16_t maxWidth = EPD_WIDTH - r.x;
    int16_t maxHeight = EPD_HEIGHT - r.y;
    if (maxWidth < 0) maxWidth = 0;
    if (maxHeight < 0) maxHeight = 0;
    if (r.width > maxWidth) {
        r.width = maxWidth;
    }
    if (r.height > maxHeight) {
        r.height = maxHeight;
    }
    if (r.width < 0) r.width = 0;
    if (r.height < 0) r.height = 0;
    return r;
}

void DisplayManager::drawHeader(const String&, const String&, int, bool) {}
void DisplayManager::drawStatusBar(int, bool) {}
void DisplayManager::drawBatteryIcon(int x, int y, int percent) {
    if (!frameBuffer) return;
    int clamped = constrain(percent, 0, 100);
    int width = BATTERY_ICON_WIDTH;
    int height = BATTERY_ICON_HEIGHT;
    int capWidth = 4;
    int capHeight = height / 2;
    
    epd_draw_rect(x, y, width, height, 0, frameBuffer);
    epd_fill_rect(x + width, y + (height - capHeight) / 2, capWidth, capHeight, 0, frameBuffer);
    
    int innerX = x + 2;
    int innerY = y + 2;
    int innerWidth = width - 4;
    int innerHeight = height - 4;
    epd_fill_rect(innerX, innerY, innerWidth, innerHeight, 255, frameBuffer);
    
    int fillWidth = (innerWidth - 4) * clamped / 100;
    if (fillWidth < 0) fillWidth = 0;
    if (fillWidth > innerWidth - 4) fillWidth = innerWidth - 4;
    if (fillWidth > 0) {
        epd_fill_rect(innerX + 2, innerY + 2, fillWidth, innerHeight - 4, 0, frameBuffer);
    }
}
void DisplayManager::drawWifiIcon(int, int, bool) {}
