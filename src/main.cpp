#include <M5EPD.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ezTime.h>
#include <regex>
#include "M5PanelUI.h"
#include "defs.h"

#define ERR_WIFI_NOT_CONNECTED "ERROR: Wifi not connected"
#define ERR_HTTP_ERROR "ERROR: HTTP code "
#define ERR_GETITEMSTATE "ERROR in getItemState"

#define DEBUG true

#define FONT_CACHE_SIZE 256

// Global vars
M5EPD_Canvas canvas(&M5.EPD);

HTTPClient httpClient;
WiFiClient wifiClient;
WiFiClient subscribeClient;

String restUrl = "http://" + String(OPENHAB_HOST) + String(":") + String(OPENHAB_PORT) + String("/rest");
String iconURL = "http://" + String(OPENHAB_HOST) + String(":") + String(OPENHAB_PORT) + String("/icon");
String subscriptionURL = "";

DynamicJsonDocument jsonDoc(60000); // size to be checked

M5PanelPage *rootPage = NULL;
String currentPage = ""; // TODO store the current widget ID in here

int loopStartMillis = 0;
int interactionStartMillis = 0;

uint16_t _last_pos_x = 0xFFFF, _last_pos_y = 0xFFFF;

Timezone openhabTZ;

#ifndef OPENHAB_SITEMAP
#define OPENHAB_SITEMAP "m5panel"
#endif

#ifndef OPENHAB_SITEMAP
#define DISPLAY_SYSINFO false
#endif

/* Reminders
    EPD canvas library https://docs.m5stack.com/#/en/api/m5paper/epd_canvas
    Text aligment https://github.com/m5stack/M5Stack/blob/master/examples/Advanced/Display/TFT_Float_Test/TFT_Float_Test.ino
*/

// HTTP and REST

void debug(String function, String message)
{
    if (DEBUG)
    {
        Serial.print(F("DEBUG (function "));
        Serial.print(function);
        Serial.print(F("): "));
        Serial.println(message);
    }
}

bool httpRequest(String &url, String &response)
{
    if (SAMPLE_SITEMAP)
    {
        return false;
    }

    HTTPClient http;
    debug(F("httpRequest"), "HTTP request to " + String(url));
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(ERR_WIFI_NOT_CONNECTED);
        response = String(ERR_WIFI_NOT_CONNECTED);
        return false;
    }
    http.useHTTP10(true);
    http.setReuse(false);
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.println(String(ERR_HTTP_ERROR) + String(httpCode));
        response = String(ERR_HTTP_ERROR) + String(httpCode);
        http.end();
        return false;
    }
    response = http.getString();
    http.end();
    debug(F("httpRequest"), F("HTTP request done"));
    return true;
}

bool subscribe()
{
    String subscribeResponse;
    httpClient.useHTTP10(true);
    httpClient.begin(wifiClient, restUrl + "/sitemaps/events/subscribe");
    int httpCode = httpClient.POST("");
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.println(String(ERR_HTTP_ERROR) + String(httpCode));
        httpClient.end();
        return false;
    }
    subscribeResponse = httpClient.getString();
    httpClient.end();

    DynamicJsonDocument subscribeResponseJson(3000);
    // Serial.println("HTTP SUBSCRIBE: " + subscribeResponse);
    deserializeJson(subscribeResponseJson, subscribeResponse);

    // String subscriptionURL = subscribeResponseJson["Location"].as<String>();
    String subscriptionURL = subscribeResponseJson["context"]["headers"]["Location"][0];
    debug(F("subscribe"), "Full subscriptionURL: " + subscriptionURL);
    subscriptionURL = subscriptionURL.substring(subscriptionURL.indexOf("/rest/sitemaps")) + "?sitemap=" + OPENHAB_SITEMAP + "&pageid=" + OPENHAB_SITEMAP; // Fix : pageId
    debug(F("subscribe"), "subscriptionURL: " + subscriptionURL);
    subscribeClient.connect(OPENHAB_HOST, OPENHAB_PORT);
    subscribeClient.println("GET " + subscriptionURL + " HTTP/1.1");
    subscribeClient.println("Host: " + String(OPENHAB_HOST) + ":" + String(OPENHAB_PORT));
    subscribeClient.println(F("Accept: text/event-stream"));
    subscribeClient.println(F("Connection: keep-alive"));
    subscribeClient.println();
    return true;
}

void updateSiteMap()
{
    jsonDoc.clear(); // jsonDoc needed to stay because elements refer to it
    debug(F("updateSiteMap"), "1:" + String(ESP.getFreeHeap()));
    String sitemapStr;

#if SAMPLE_SITEMAP
    debug(F("updateSiteMap"), "Load sample sitemap");
    File f = LittleFS.open("/sample_sitemap.json");
    sitemapStr = f.readString();
#else
    httpRequest(restUrl + "/sitemaps/" + OPENHAB_SITEMAP, sitemapStr);
#endif

    debug(F("updateSiteMap"), "2:" + String(ESP.getFreeHeap()));
    deserializeJson(jsonDoc, sitemapStr, DeserializationOption::NestingLimit(50));
    debug(F("updateSiteMap"), "3:" + String(ESP.getFreeHeap()));

    delete rootPage;

    JsonObject rootPageJson = jsonDoc.as<JsonObject>()["homepage"];
    rootPage = new M5PanelPage(rootPageJson);
    currentPage = rootPage->identifier;
    debug(F("updateSiteMap"), "5:" + String(ESP.getFreeHeap()));

    rootPage->draw(&canvas);
}

void parseSubscriptionData(String jsonDataStr)
{
    DynamicJsonDocument jsonData(30000);
    deserializeJson(jsonData, jsonDataStr);
    debug(F("parseSubscriptionData"), jsonDataStr);
    if (!jsonData["widgetId"].isNull()) // Data Widget (subscription)
    {
        String widgetId = jsonData["widgetId"];
        debug(F("parseSubscriptionData"), "Widget changed: " + widgetId);
        // update widget and redraw if widget on currently shown page
        rootPage->updateWidget(jsonData.as<JsonObject>(), widgetId, currentPage, &canvas);
    }
    else if (!jsonData["TYPE"].isNull())
    {
        String jsonDataType = jsonData["TYPE"];
        if (jsonDataType.equals("ALIVE"))
        {
            debug(F("parseSubscriptionData"), F("Subscription Alive"));
        }
        else if (jsonDataType.equals("SITEMAP_CHANGED"))
        {
            debug(F("parseSubscriptionData"), F("Sitemap changed, reloading"));
            updateSiteMap();
        }
    }
    jsonData.clear();
}

void setTimeZone() // Gets timezone from OpenHAB
{
    String response;
    if (httpRequest(restUrl + "/services/org.eclipse.smarthome.i18n/config", response))
    {
        DynamicJsonDocument doc(2000);
        deserializeJson(doc, response);
        String timezone = doc["timezone"];
        debug("setTimeZone", "OpenHAB timezone= " + timezone);
        doc.clear();
        openhabTZ.setLocation(timezone);
    }
    else
    {
        debug("setTimeZone", "Could not get OpenHAB timezone");
    }
}

void syncRTC()
{
    /*
    RTCtime.hour = now.hour
    RTCtime.min  = timeClient.getMinutes();
    RTCtime.sec  = timeClient.getSeconds();
    M5.RTC.setTime(&RTCtime);

    RTCDate.year = timeClient.getDay();
    RTCDate.mon  = timeClient.getHours();
    RTCDate.day  = timeClient.getDay();
    M5.RTC.setDate(&RTCDate);
*/
    /*
    String dateTime = getItemState(OPENHAB_DATETIME_ITEM);
    RTCtime.hour = dateTime.substring(11,13).toInt();
    RTCtime.min  = dateTime.substring(14,16).toInt();
    RTCtime.sec  = dateTime.substring(17,19).toInt();
    debug("syncRTC","Time="+String(RTCtime.hour)+":"+String(RTCtime.min)+":"+String(RTCtime.sec));
    M5.RTC.setTime(&RTCtime);

    RTCDate.year = dateTime.substring(0,4).toInt();
    RTCDate.mon  = dateTime.substring(5,7).toInt();
    RTCDate.day  = dateTime.substring(8,10).toInt();
    debug("syncRTC","Date="+String(RTCDate.year)+"-"+String(RTCDate.mon)+"-"+String(RTCDate.day));
    M5.RTC.setDate(&RTCDate);
    */
}

void setup()
{
    Serial.println(F("Setup start..."));

    M5.begin(true, false, true, true, false); // bool touchEnable = true, bool SDEnable = false, bool SerialEnable = true, bool BatteryADCEnable = true, bool I2CEnable = false
    M5.disableEXTPower();
    /* Uncomment for static IP
        IPAddress ip(192,168,0,xxx);    // Node Static IP
        IPAddress gateway(192,168,0,xxx); // Network Gateway (usually Router IP)
        IPAddress subnet(255,255,255,0);  // Subnet Mask
        IPAddress dns1(xxx,xxx,xxx,xxx);    // DNS1 IP
        IPAddress dns2(xxx,xxx,xxx,xxx);    // DNS2 IP
        WiFi.config(ip, gateway, subnet, dns1, dns2);
    */
    // M5.EPD.SetRotation(180);
    M5.EPD.Clear(true);
    M5.RTC.begin();

    // FS Setup
    Serial.println(F("Inizializing FS..."));
    if (SPIFFS.begin())
    {
        Serial.println(F("SPIFFS mounted correctly."));
    }
    else
    {
        Serial.println(F("!An error occurred during SPIFFS mounting"));
    }

    Serial.println(F("Inizializing LittleFS FS..."));
    if (LittleFS.begin())
    {
        Serial.println(F("LittleFS mounted correctly."));
    }
    else
    {
        Serial.println(F("!An error occurred during LittleFS mounting"));
    }

    // Get all information of LittleFS
    unsigned int totalBytes = LittleFS.totalBytes();
    unsigned int usedBytes = LittleFS.usedBytes();

    // TODO : Should fail and stop if SPIFFS error

    Serial.println("===== File system info =====");

    Serial.print("Total space:      ");
    Serial.print(totalBytes);
    Serial.println("byte");

    Serial.print("Total space used: ");
    Serial.print(usedBytes);
    Serial.println("byte");

    Serial.println();

    esp_err_t errorCode = canvas.loadFont("/FreeSansBold.ttf", LittleFS);
    // TODO : Should fail and stop if font not found
    Serial.print("Font load exit code:");
    Serial.println(errorCode);

    canvas.createRender(FONT_SIZE_LABEL, FONT_CACHE_SIZE);
    canvas.createRender(FONT_SIZE_LABEL_SMALL, FONT_CACHE_SIZE);
    canvas.createRender(FONT_SIZE_STATUS_CENTER, FONT_CACHE_SIZE);
    canvas.createRender(FONT_SIZE_STATUS_BOTTOM, FONT_CACHE_SIZE);
    canvas.createRender(FONT_SIZE_SYSINFO, FONT_CACHE_SIZE);

    canvas.setTextSize(FONT_SIZE_LABEL);

    // Setup Wifi
    if (!SAMPLE_SITEMAP)
    {
        Serial.println(F("Starting Wifi"));
        WiFi.begin(WIFI_SSID, WIFI_PSK);
        while (WiFi.status() != WL_CONNECTED)
        {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        Serial.println(F("WiFi connected"));
        Serial.println(F("IP address: "));
        Serial.println(WiFi.localIP());

        // NTP stuff
        setInterval(3600);
        waitForSync();
        setTimeZone();

        subscribe();
    }

    updateSiteMap();
}

void checkSubscription()
{
    // Subscribe or re-subscribe to sitemap
    if (!subscribeClient.connected())
    {
        Serial.println(F("subscribeClient not connected, connecting..."));
        if (!subscribe())
        {
            delay(300);
        }
    }

    // Check and get subscription data
    while (subscribeClient.available())
    {
        String subscriptionReceivedData = subscribeClient.readStringUntil('\n');
        debug(F("loop"), "received subscription " + subscriptionReceivedData);
        int dataStart = subscriptionReceivedData.indexOf("data: ");
        if (dataStart > -1) // received data contains "data: "
        {
            String subscriptionData = subscriptionReceivedData.substring(dataStart + 6); // Remove chars before "data: "
            int dataEnd = subscriptionData.indexOf("\n\n");
            subscriptionData = subscriptionData.substring(0, dataEnd);
            parseSubscriptionData(subscriptionData);
        }
    }
}

void checkTouch()
{
    if (M5.TP.avaliable())
    {
        interactionStartMillis = loopStartMillis;

        M5.TP.update();
        bool is_finger_up = M5.TP.isFingerUp();
        if (is_finger_up)
        {
            if (_last_pos_x != 0xFFFF && _last_pos_y != 0xFFFF)
            {
                // process touch on finger lifting
                currentPage = rootPage->processTouch(currentPage, _last_pos_x, _last_pos_y, &canvas);
                // TODO create subscription for new current page! Remove last _x (where x is the page number) and use as pageId
                // TODO request initial update for new current page, since not all pages have a subscription running
                debug(F("loop"), "new current page after touch: " + currentPage);
                _last_pos_x = _last_pos_y = 0xFFFF;
            }
        }
        else
        {
            _last_pos_x = M5.TP.readFingerX(0);
            _last_pos_y = M5.TP.readFingerY(0);
        }
        M5.TP.flush();
    }
}

// Loop
void loop()
{
    loopStartMillis = millis();
    if (!SAMPLE_SITEMAP)
    {
        checkSubscription();
    }

    checkTouch();

    events(); // for ezTime

    int loopEndMillis = millis();
    int loopDuration = loopEndMillis - loopStartMillis;
    int durationSinceInteraction = interactionStartMillis - loopEndMillis;
    if (durationSinceInteraction < 45000)
    {
        int sleepMillisShort = 50 - loopDuration;
        // responsive within the first 45s after last touch
        if (sleepMillisShort > 0)
        {
            sleep(sleepMillisShort);
        }
    }
    else
    {
        // shut down M5 to save energy
        M5.shutdown(120);
    }
}