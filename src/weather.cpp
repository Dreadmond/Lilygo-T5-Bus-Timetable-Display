#include "weather.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

WeatherClient weatherClient;

WeatherClient::WeatherClient() {
    currentWeather.valid = false;
    currentWeather.temperature = 0;
    currentWeather.humidity = 0;
    currentWeather.condition = "";
    lastError = "";
}

bool WeatherClient::fetchWeather() {
    String apiKey = String(WEATHER_API_KEY);
    if (apiKey.length() < 10) {
        lastError = "No API key";
        return false;
    }
    
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    
    String url = "https://api.openweathermap.org/data/2.5/weather?lat=";
    url += String(WEATHER_LAT_STR);
    url += "&lon=";
    url += String(WEATHER_LON_STR);
    url += "&units=metric&appid=";
    url += String(WEATHER_API_KEY);
    
    DEBUG_PRINTF("Fetching weather from: %s\n", url.c_str());
    
    http.begin(client, url);
    http.setTimeout(10000);
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        http.end();
        return parseResponse(response);
    }
    
    lastError = "HTTP " + String(httpCode);
    DEBUG_PRINTF("Weather API error: %d\n", httpCode);
    http.end();
    return false;
}

bool WeatherClient::parseResponse(const String& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        lastError = "JSON error";
        DEBUG_PRINTF("Weather JSON error: %s\n", error.c_str());
        return false;
    }
    
    currentWeather.temperature = doc["main"]["temp"].as<float>();
    currentWeather.humidity = doc["main"]["humidity"].as<int>();
    currentWeather.condition = doc["weather"][0]["main"].as<String>();
    currentWeather.icon = doc["weather"][0]["icon"].as<String>();
    currentWeather.valid = true;
    
    DEBUG_PRINTF("Weather: %.1f°C, %s, %d%% humidity\n", 
                 currentWeather.temperature, 
                 currentWeather.condition.c_str(),
                 currentWeather.humidity);
    
    return true;
}

