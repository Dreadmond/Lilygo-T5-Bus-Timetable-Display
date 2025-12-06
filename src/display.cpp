#include "display.h"
#include "epd_driver.h"
#include "esp_heap_caps.h"
#include "firasans.h"
#include "busstop_font.h"
#include "busstop_small_font.h"
#include <time.h>
#include <vector>
#include <cmath>
#include "zlib/zlib.h"

// Use BusStop font as the main display font
#define DISPLAY_FONT BusStop

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

// Helper: Calculate fresh "leave in" minutes from departure time string
int DisplayManager::calculateLeaveIn(const BusDeparture& dep) const {
    int minutesUntil = dep.minutesUntilDeparture;  // fallback
    if (dep.departureTime.length() >= 5) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            int depHour = dep.departureTime.substring(0, 2).toInt();
            int depMin = dep.departureTime.substring(3, 5).toInt();
            int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
            int depMinutes = depHour * 60 + depMin;
            if (depMinutes < nowMinutes - 60) depMinutes += 24 * 60;  // overnight
            minutesUntil = depMinutes - nowMinutes;
        }
    }
    int leaveIn = minutesUntil - dep.walkingTimeMinutes;
    return (leaveIn < 0) ? 0 : leaveIn;
}

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
    colorsInverted = false;
    for (int i = 0; i < CARD_MAX_COUNT; i++) lastLeaveIn[i] = -999;
}

void DisplayManager::setInvertedColors(bool inverted) {
    colorsInverted = inverted;
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
    sleep();  // Ensure display sleeps after clear
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    partialRefreshCount = 0;
    lastFullRefresh = millis();
}

void DisplayManager::fullRefresh() {
    if (!initialized) return;
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display is fully powered off
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
    sleep();  // Ensure display sleeps after refresh
    partialRefreshCount++;
}

void DisplayManager::fastRefresh() {
    if (!initialized) return;
    epd_poweron();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
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
                                       bool placeholderMode,
                                       bool forceFullRefresh) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    
    // Background color depends on inversion state
    uint8_t bgColor = colorsInverted ? 0xFF : 0x33;  // Light or dark
    uint8_t headerBg = colorsInverted ? 240 : 50;
    memset(frameBuffer, bgColor, EPD_WIDTH * EPD_HEIGHT / 2);
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
    const LayoutSlot& heroBatteryRect = layoutSlot(LAYOUT_HERO_BATTERY);
    
    // Header background
    epd_fill_rect(heroRect.x, heroRect.y, heroRect.width, heroRect.height, headerBg, frameBuffer);
    
    // Text properties depend on inversion
    FontProperties textProps = {
        .fg_color = colorsInverted ? (uint8_t)0 : (uint8_t)15,  // Black or white
        .bg_color = colorsInverted ? (uint8_t)15 : (uint8_t)3,
        .fallback_glyph = 0,
        .flags = 0
    };
    
    // Time on left
    int32_t hx = heroRect.x + 20;
    int32_t hy = heroRect.y + 42;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStop, timeLabel.c_str(), &hx, &hy, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStop, timeLabel.c_str(), &hx, &hy, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
    
    // Direction centered
    String dirLine = direction.length() ? direction : "Departures";
    int dirWidth = getTextWidth(dirLine, &BusStop);
    int32_t dx = heroRect.x + (heroRect.width - dirWidth) / 2;
    int32_t dy = heroRect.y + 42;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStop, dirLine.c_str(), &dx, &dy, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStop, dirLine.c_str(), &dx, &dy, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
    
    // Battery as ASCII only: [|||||] or [|||  ]
    int bars = (batteryPercent + 10) / 20;  // 0-5 bars
    if (bars > 5) bars = 5;
    if (bars < 0) bars = 0;
    String batStr = "[";
    for (int i = 0; i < 5; i++) {
        batStr += (i < bars) ? "|" : " ";
    }
    batStr += "]";
    
    int batWidth = getTextWidth(batStr, &BusStop);
    int32_t batX = heroBatteryRect.x + heroBatteryRect.width - batWidth - 10;
    int32_t batY = heroRect.y + 42;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStop, batStr.c_str(), &batX, &batY, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStop, batStr.c_str(), &batX, &batY, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
    
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
        // Calculate fresh leaveIn from departure time
        int leaveIn = calculateLeaveIn(departures[i]);
        lastLeaveIn[i] = leaveIn;
    }
    
    // Show message if no buses available
    if (actualCount == 0) {
        int32_t x = cardLeft + 20;
        int32_t y = cardAreaTop + 80;
        if (colorsInverted) {
            writeln((GFXfont*)&BusStop, "Unable to obtain live bus information", &x, &y, frameBuffer);
        } else {
            FontProperties whiteText = {
                .fg_color = 15,
                .bg_color = 3,
                .fallback_glyph = 0,
                .flags = 0
            };
            write_mode((GFXfont*)&BusStop, "Unable to obtain live bus information", &x, &y, frameBuffer, BLACK_ON_WHITE, &whiteText);
        }
    }
    
    // Always do full refresh
    epd_poweron();
    Rect_t fullScreen = {0, 0, EPD_WIDTH, EPD_HEIGHT};
    epd_clear_area_cycles(fullScreen, 2, 40);
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
    DEBUG_PRINTLN("Display: Full refresh");
    
    lastTimeStr = timeLabel;
    lastBatteryPercent = batteryPercent;
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
    
    // Card styling depends on inversion
    uint8_t cardBg = colorsInverted ? 255 : 50;
    uint8_t borderColor = colorsInverted ? 180 : 30;
    uint8_t lineColor = colorsInverted ? 180 : 100;
    
    // Draw main card
    epd_fill_rect(cardLeft, cardTop, cardWidth, cardHeight, cardBg, frameBuffer);
    epd_draw_rect(cardLeft, cardTop, cardWidth, cardHeight, borderColor, frameBuffer);
    
    // Text properties depend on inversion
    FontProperties textProps = {
        .fg_color = colorsInverted ? (uint8_t)0 : (uint8_t)15,
        .bg_color = colorsInverted ? (uint8_t)15 : (uint8_t)3,
        .fallback_glyph = 0,
        .flags = 0
    };
    
    int paddingTop = 20;
    int paddingBottom = 16;
    int innerTop = cardTop + paddingTop;
    int innerHeight = cardHeight - paddingTop - paddingBottom;
    if (innerHeight < 20) innerHeight = cardHeight - 10;
    
    const int colSpacing = 12;
    const int cardRight = cardLeft + cardWidth;
    const int leftAreaLeft = cardLeft + colSpacing + 15;
    
    // Bus number
    int busNumWidth = 90;
    int32_t busNumX = leftAreaLeft + 10;
    int32_t busNumY = innerTop + innerHeight / 2 + 15;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStop, departure.busNumber.c_str(), &busNumX, &busNumY, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStop, departure.busNumber.c_str(), &busNumX, &busNumY, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
    
    // Info section
    int infoColLeft = leftAreaLeft + busNumWidth + colSpacing;
    int rightSectionLeft = cardRight - 320;
    
    // Clean up stop name - remove "Cheltenham" prefix
    String stopName = departure.stopName;
    stopName.replace("Cheltenham ", "");
    stopName.replace("Cheltenham, ", "");
    
    // Stop name
    int32_t stopX = infoColLeft;
    int32_t stopY = innerTop + innerHeight / 2 + 15;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStop, stopName.c_str(), &stopX, &stopY, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStop, stopName.c_str(), &stopX, &stopY, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
    
    // Vertical separator line
    epd_draw_line(rightSectionLeft - 20, cardTop + 15, rightSectionLeft - 20, cardTop + cardHeight - 15, lineColor, frameBuffer);
    
    // Calculate fresh leaveIn from actual departure time
    int leaveIn = calculateLeaveIn(departure);
    
    String leaveLine = (leaveIn <= 0)
        ? "Leave now!"
        : "Leave in " + String(leaveIn) + " min";
    
    // Departure time (smaller font)
    int32_t timeX = rightSectionLeft;
    int32_t timeY = innerTop + 22;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStopSmall, departure.departureTime.c_str(), &timeX, &timeY, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStopSmall, departure.departureTime.c_str(), &timeX, &timeY, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
    
    // Leave in text below (smaller font)
    int32_t leaveX = rightSectionLeft;
    int32_t leaveY = innerTop + innerHeight / 2 + 22;
    if (colorsInverted) {
        writeln((GFXfont*)&BusStopSmall, leaveLine.c_str(), &leaveX, &leaveY, frameBuffer);
    } else {
        write_mode((GFXfont*)&BusStopSmall, leaveLine.c_str(), &leaveX, &leaveY, frameBuffer, BLACK_ON_WHITE, &textProps);
    }
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
    const GFXfont* font = (GFXfont*)&BusStop;
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
    const GFXfont* font = (GFXfont*)&BusStop;
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
    const GFXfont* font = (GFXfont*)&BusStop;
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
    writeln((GFXfont*)&BusStop, "Error", &x, &y, frameBuffer);
    x = 200; y = 320;
    writeln((GFXfont*)&BusStop, msg.c_str(), &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::showLoading(const String& msg) {
    if (!initialized || !frameBuffer) return;
    const GFXfont* font = (GFXfont*)&BusStop;
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
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::showNoData(const String& msg) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    int32_t x = 350, y = 270;
    writeln((GFXfont*)&BusStop, "No Data", &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::showWiFiSetup(const String& ssid, const String& ip) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    
    // White background
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    
    const GFXfont* font = (GFXfont*)&BusStop;
    int centerX = EPD_WIDTH / 2;
    
    // Title
    String title = "WiFi Setup";
    int titleWidth = getTextWidth(title, font);
    int32_t x = centerX - titleWidth / 2;
    int32_t y = 80;
    writeln(font, title.c_str(), &x, &y, frameBuffer);
    
    // Decorative line
    epd_draw_line(centerX - 150, y + 20, centerX + 150, y + 20, 180, frameBuffer);
    
    // Instructions
    y = 180;
    
    String line1 = "1. Connect to WiFi network:";
    int w1 = getTextWidth(line1, font);
    x = centerX - w1 / 2;
    writeln(font, line1.c_str(), &x, &y, frameBuffer);
    
    y += 50;
    String networkName = "\"" + ssid + "\"";
    int wn = getTextWidth(networkName, font);
    x = centerX - wn / 2;
    writeln(font, networkName.c_str(), &x, &y, frameBuffer);
    
    y += 70;
    String line2 = "2. Open browser and go to:";
    int w2 = getTextWidth(line2, font);
    x = centerX - w2 / 2;
    writeln(font, line2.c_str(), &x, &y, frameBuffer);
    
    y += 50;
    String url = "http://" + ip;
    int wu = getTextWidth(url, font);
    x = centerX - wu / 2;
    writeln(font, url.c_str(), &x, &y, frameBuffer);
    
    y += 70;
    String line3 = "3. Enter your WiFi details";
    int w3 = getTextWidth(line3, font);
    x = centerX - w3 / 2;
    writeln(font, line3.c_str(), &x, &y, frameBuffer);
    
    // Refresh display
    epd_poweron();
    Rect_t fullScreen = {0, 0, EPD_WIDTH, EPD_HEIGHT};
    epd_clear_area_cycles(fullScreen, 2, 40);
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::showClock(const String& timeStr) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    
    // Clear to white
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    
    // Get current date
    char dateBuf[64] = "";
    char dayBuf[32] = "";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        strftime(dayBuf, sizeof(dayBuf), "%A", &timeinfo);  // "Friday"
        strftime(dateBuf, sizeof(dateBuf), "%d %B %Y", &timeinfo);  // "05 December 2024"
    }
    
    // Draw a subtle decorative line above the clock
    int lineY = EPD_HEIGHT / 2 - 100;
    int lineWidth = 200;
    int lineX = (EPD_WIDTH - lineWidth) / 2;
    epd_draw_line(lineX, lineY, lineX + lineWidth, lineY, 180, frameBuffer);
    
    // Draw the time HUGE and centered using scaled text
    // Scale factor 3.0 makes it approximately 150px tall
    float clockScale = 3.5f;
    String displayTime = timeStr.length() ? timeStr : "--:--";
    
    // Measure text width at scale
    int baseWidth = measureTextAdvance(displayTime);
    int scaledWidth = (int)(baseWidth * clockScale);
    int clockX = (EPD_WIDTH - scaledWidth) / 2;
    int clockY = EPD_HEIGHT / 2 + 30;  // Centered vertically, slightly down
    
    // Draw scaled clock digits
    drawScaledGlyphRun(displayTime, clockX, clockY, clockScale);
    
    // Draw decorative line below the clock
    int line2Y = EPD_HEIGHT / 2 + 80;
    epd_draw_line(lineX, line2Y, lineX + lineWidth, line2Y, 180, frameBuffer);
    
    // Draw day of week above clock
    if (strlen(dayBuf) > 0) {
        int dayWidth = getTextWidth(String(dayBuf), &BusStop);
        int32_t dayX = (EPD_WIDTH - dayWidth) / 2;
        int32_t dayY = EPD_HEIGHT / 2 - 120;
        writeln((GFXfont*)&BusStop, dayBuf, &dayX, &dayY, frameBuffer);
    }
    
    // Draw date below clock
    if (strlen(dateBuf) > 0) {
        int dateWidth = getTextWidth(String(dateBuf), &BusStop);
        int32_t dateX = (EPD_WIDTH - dateWidth) / 2;
        int32_t dateY = EPD_HEIGHT / 2 + 130;
        writeln((GFXfont*)&BusStop, dateBuf, &dateX, &dateY, frameBuffer);
    }
    
    // Small "Sleep mode" text at bottom
    const char* sleepText = "Display sleeping until 06:00";
    int sleepWidth = getTextWidth(String(sleepText), &BusStop);
    int32_t sleepX = (EPD_WIDTH - sleepWidth) / 2;
    int32_t sleepY = EPD_HEIGHT - 40;
    FontProperties grayText = {
        .fg_color = 8,  // Medium gray
        .bg_color = 15,
        .fallback_glyph = 0,
        .flags = 0
    };
    write_mode((GFXfont*)&BusStop, sleepText, &sleepX, &sleepY, frameBuffer, BLACK_ON_WHITE, &grayText);
    
    // Full refresh for clean display
    epd_poweron();
    epd_clear();
    delay(50);
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::showLowBattery(int pct) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "Low Battery: %d%%", pct);
    int32_t x = 300, y = 270;
    writeln((GFXfont*)&BusStop, buf, &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::showConnectionStatus(bool wifi, bool mqtt) {
    if (!initialized || !frameBuffer) return;
    resetLoadingLog();
    memset(frameBuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    int32_t x = 350, y = 250;
    writeln((GFXfont*)&BusStop, "Status", &x, &y, frameBuffer);
    x = 300; y = 300;
    writeln((GFXfont*)&BusStop, wifi ? "WiFi: OK" : "WiFi: FAIL", &x, &y, frameBuffer);
    x = 300; y = 340;
    writeln((GFXfont*)&BusStop, mqtt ? "MQTT: OK" : "MQTT: FAIL", &x, &y, frameBuffer);
    epd_poweron(); epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), frameBuffer);
    epd_poweroff_all();
    sleep();  // Ensure display sleeps after refresh
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
    const GFXfont* font = (GFXfont*)&BusStop;
    int textWidth = getTextWidth(text, font);
    int textHeight = getTextHeight(font);
    int32_t x = left + (width - textWidth) / 2;
    if (x < left) x = left;
    int32_t y = top + (height - textHeight) / 2 + textHeight;
    writeln((GFXfont*)font, text.c_str(), &x, &y, frameBuffer);
}

int DisplayManager::drawWrappedTextBlock(int left, int top, int width, int maxHeight, const String& text, bool center) {
    if (!frameBuffer || width <= 0 || maxHeight <= 0) return top;
    const GFXfont* font = (GFXfont*)&BusStop;
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
    sleep();  // Ensure display sleeps after refresh
}

void DisplayManager::sleep() {
    if (!initialized) return;
    // Ensure display is fully powered off - safe to call multiple times
    epd_poweroff_all();
    // Small delay to ensure power-off completes
    delay(10);
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
