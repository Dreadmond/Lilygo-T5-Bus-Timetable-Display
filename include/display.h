#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "epd_driver.h"
#include "config.h"

// ============================================================================
// DISPLAY MANAGER FOR LILYGO T5 4.7" E-INK
// High contrast, large fonts for elderly readability
// Utilizes partial refresh, grayscale, and touch features
// ============================================================================

// Font sizes for different elements
// The EPD47 library includes fonts - use the ones from the library
#include "firasans.h"

// Bus departure structure
struct BusDeparture {
    String busNumber;
    String stopName;
    String destination;
    String departureTime;
    int minutesUntilDeparture;
    int walkingTimeMinutes;
    bool isLive;
    String statusText;
};

// Screen regions for partial updates
struct ScreenRegion {
    int x;
    int y;
    int width;
    int height;
};

enum class TextAlignment {
    LEFT,
    CENTER,
    RIGHT
};


// Display update modes
enum UpdateMode {
    UPDATE_MODE_FULL,           // Full refresh - clears ghosting, slow (~1.5s)
    UPDATE_MODE_PARTIAL,        // Partial refresh - faster (~0.3s), may ghost
    UPDATE_MODE_FAST,           // Fast mode - very quick, more ghosting
    UPDATE_MODE_GRAYSCALE       // High quality grayscale
};

// Display manager class
class DisplayManager {
public:
    DisplayManager();
    
    void init();
    void clear();
    
    // Update modes
    void fullRefresh();                     // Complete screen refresh (clears ghosting)
    void partialRefresh(ScreenRegion region); // Update specific region only
    void fastRefresh();                     // Quick update (for countdown timers)
    
    // Main display functions
    void showBusTimetable(BusDeparture departures[], int count, 
                          String currentTime, String direction,
                          int batteryPercent, bool wifiConnected,
                          bool placeholderMode = false,
                          bool forceFullRefresh = false);
    void showError(const String& message);
    void showLoading(const String& message);
    void showNoData(const String& message);
    void showClock(const String& timeStr);  // Full-screen clock for sleep mode
    void showWiFiSetup(const String& ssid, const String& ip);  // WiFi setup instructions
    void setInvertedColors(bool inverted);  // Toggle color inversion
    
    // Partial update functions (for real-time countdown)
    void updateTimeOnly(const String& currentTime);
    void updateCountdownsOnly(BusDeparture departures[], int count);
    void updateStatusBarOnly(int batteryPercent, bool wifiConnected);
    void updateFooter(int secondsAgo, bool cached);  // Update footer with elapsed time
    
    // Low battery warning
    void showLowBattery(int percent);
    
    // Connection status
    void showConnectionStatus(bool wifi, bool mqtt);
    
    
    // Getters for regions (for selective updates)
    ScreenRegion getHeaderRegion() const;
    ScreenRegion getCardRegion(int index) const;
    ScreenRegion getStatusBarRegion() const;
    ScreenRegion getTimeRegion() const;
    
    // Should we do a full refresh?
    bool needsFullRefresh() const;
    void resetFullRefreshTimer();

private:
    uint8_t* frameBuffer;
    bool initialized;
    unsigned long lastFullRefresh;
    int partialRefreshCount;
    bool placeholderBannerVisible;
    int placeholderYOffset;
    bool loadingLogActive;
    int loadingLogCursorY;
    bool colorsInverted;
    
    // Cached values for partial updates
    String lastTimeStr;
    int lastBatteryPercent;
    static const int MAX_TRACKED_DEPARTURES = 3;
    int lastLeaveIn[MAX_TRACKED_DEPARTURES];
    
    // Drawing helpers
    void drawText(int x, int y, const String& text, const GFXfont* font, uint8_t color = 0);
    void drawCenteredText(int y, const String& text, const GFXfont* font, uint8_t color = 0);
    void drawRightAlignedText(int x, int y, const String& text, const GFXfont* font, uint8_t color = 0);
    void drawRect(int x, int y, int w, int h, uint8_t color);
    void drawFilledRect(int x, int y, int w, int h, uint8_t color);
    void drawRoundedRect(int x, int y, int w, int h, int radius, uint8_t color);
    void drawFilledRoundedRect(int x, int y, int w, int h, int radius, uint8_t color);
    void drawLine(int x1, int y1, int x2, int y2, uint8_t color);
    void drawHorizontalDivider(int y);
    void drawCircle(int x, int y, int radius, uint8_t color);
    void drawFilledCircle(int x, int y, int radius, uint8_t color);
    void drawTextCenteredInRect(int left, int top, int width, int height, const String& text);
    int drawWrappedTextBlock(int left, int top, int width, int maxHeight, const String& text, bool center = true);
    void logLayoutTable() const;
    BusDeparture buildFallbackDeparture(int slotIndex, const String& directionLabel, bool placeholderMode) const;
    String formatTimeOffset(int minutesAhead) const;
    int calculateLeaveIn(const BusDeparture& dep) const;
    void drawScaledTextInRect(int left, int top, int width, int height, const String& text, float scale, TextAlignment alignment);
    void drawScaledGlyphRun(const String& text, int startX, int baselineY, float scale);
    void drawScaledGlyph(const GFXglyph* glyph, const uint8_t* bitmap, int byteWidth, int startX, int baselineY, float scale);
    void writePixelToBuffer(int x, int y, uint8_t color);
    int measureTextAdvance(const String& text) const;
    void resetLoadingLog();
    
    // Grayscale helpers
    void drawGradientRect(int x, int y, int w, int h, uint8_t startColor, uint8_t endColor);
    void drawShadow(int x, int y, int w, int h, int shadowSize);
    
    // Bus card rendering
    void drawBusCard(int cardIndex, const BusDeparture& departure, bool highlight, bool placeholderMode,
                     int cardTop, int cardHeight, int cardLeft, int cardWidth);
    
    // Header/footer rendering
    void drawHeader(const String& currentTime, const String& direction,
                    int batteryPercent, bool wifiConnected);
    void drawStatusBar(int batteryPercent, bool wifiConnected);
    void drawBatteryIcon(int x, int y, int percent);
    void drawWifiIcon(int x, int y, bool connected);
    
    
    // Calculate text dimensions
    int getTextWidth(const String& text, const GFXfont* font);
    int getTextHeight(const GFXfont* font);
    
    // Update region to EPD
    void pushRegionToDisplay(ScreenRegion region, UpdateMode mode);
    
    Rect_t clampToScreen(Rect_t rect) const;
};

extern DisplayManager display;

#endif // DISPLAY_H
