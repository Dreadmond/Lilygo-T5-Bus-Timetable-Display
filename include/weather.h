#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>
#include "config.h"

struct WeatherData {
    float temperature;      // Celsius
    int humidity;           // Percentage
    String condition;       // "Clear", "Clouds", "Rain", etc.
    String icon;            // Icon code
    bool valid;
};

class WeatherClient {
public:
    WeatherClient();
    
    bool fetchWeather();
    WeatherData getWeather() const { return currentWeather; }
    String getLastError() const { return lastError; }
    
private:
    WeatherData currentWeather;
    String lastError;
    
    bool parseResponse(const String& json);
};

extern WeatherClient weatherClient;

#endif

