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
#include "ImageResource.h"

#define SAVED_STATE_FILE "/savedState"
#define TIME_UNTIL_SLEEP 120
#define UPTIME_AUTOMATIC_BOOT 20

#define ERR_WIFI_NOT_CONNECTED "ERROR: Wifi not connected"
#define ERR_HTTP_ERROR "ERROR: HTTP code "
#define ERR_GETITEMSTATE "ERROR in getItemState"

#define DEBUG true

#define FONT_CACHE_SIZE 256

// Global vars
M5EPD_Canvas canvas(&M5.EPD);

WiFiClient subscribeClient;

String restUrl = "http://" + String(OPENHAB_HOST) + String(":") + String(OPENHAB_PORT) + String("/rest");
String iconURL = "http://" + String(OPENHAB_HOST) + String(":") + String(OPENHAB_PORT) + String("/icon");
String subscriptionId = "";

DynamicJsonDocument jsonDoc(60000); // size to be checked

M5PanelPage *rootPage = NULL;
String currentPage = "" + String(OPENHAB_SITEMAP) + "_0";

unsigned long loopStartMillis = 0;
long interactionStartMillis = 0;

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

    debug(F("httpRequest"), "HTTP request to " + String(url));
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(ERR_WIFI_NOT_CONNECTED);
        response = String(ERR_WIFI_NOT_CONNECTED);
        return false;
    }

    WiFiClient wifiClient;
    HTTPClient httpClient;

    httpClient.useHTTP10(true);
    httpClient.begin(wifiClient, url);
    int httpCode = httpClient.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.println(String(ERR_HTTP_ERROR) + String(httpCode));
        response = String(ERR_HTTP_ERROR) + String(httpCode);
        httpClient.end();
        return false;
    }
    response = httpClient.getString();
    httpClient.end();
    debug(F("httpRequest"), F("HTTP request done"));
    return true;
}

String getCurrentPageId()
{
    int cutoffIdx = currentPage.length() - 1;
    int choicesIdx = currentPage.lastIndexOf("_choices_");
    if (choicesIdx > 0)
    {
        cutoffIdx = choicesIdx;
    }
    int pageSeparatorIdx = currentPage.lastIndexOf("_");
    if (pageSeparatorIdx > 0)
    {
        cutoffIdx = pageSeparatorIdx;
    }
    return currentPage.substring(0, cutoffIdx);
}

void updateAndSubscribeCurrentPage()
{
    String page = getCurrentPageId();

    String pageUpdate;
    httpRequest(restUrl + "/sitemaps/" + OPENHAB_SITEMAP + "/" + page + "?subscriptionid=" + subscriptionId, pageUpdate);
    DynamicJsonDocument jsonData(30000);
    deserializeJson(jsonData, pageUpdate);
    debug(F("updateAndSubscribeCurrentPage"), pageUpdate);
    JsonArray widgets = jsonData["widgets"];
    for (size_t i = 0; i < widgets.size(); i++)
    {
        String widgetId = widgets[i]["widgetId"].as<String>();
        debug(F("updateAndSubscribeCurrentPage"), "update widget with id " + widgetId);
        rootPage->updateWidget(widgets[i], widgetId, currentPage, &canvas);
    }

    jsonData.clear();
}

bool subscribe()
{
    String subscribeResponse;

    WiFiClient wifiClient;
    HTTPClient httpClient;

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
    String baseUrl = subscribeResponseJson["context"]["headers"]["Location"][0];
    debug(F("subscribe"), "Full subscriptionURL: " + baseUrl);

    subscribeResponseJson.clear();

    subscriptionId = baseUrl.substring(baseUrl.lastIndexOf("/") + 1);

    String subscriptionURL = baseUrl.substring(baseUrl.indexOf("/rest/sitemaps"));
    String parametrizedUrl = subscriptionURL + "?sitemap=" + OPENHAB_SITEMAP + "&pageid=" + getCurrentPageId();
    debug(F("subscribe"), "subscriptionURL: " + parametrizedUrl);
    subscribeClient.connect(OPENHAB_HOST, OPENHAB_PORT);
    subscribeClient.println("GET " + parametrizedUrl + " HTTP/1.1");
    subscribeClient.println("Host: " + String(OPENHAB_HOST) + ":" + String(OPENHAB_PORT));
    subscribeClient.println(F("Accept: text/event-stream"));
    subscribeClient.println(F("Connection: keep-alive"));
    subscribeClient.println();

    updateAndSubscribeCurrentPage();

    return true;
}

void updateSiteMap()
{
    jsonDoc.clear(); // jsonDoc needed to stay because elements refer to it
    String sitemapStr;

#if SAMPLE_SITEMAP
    debug(F("updateSiteMap"), "Load sample sitemap");
    File f = LittleFS.open("/sample_sitemap.json");
    sitemapStr = f.readString();
#else
    httpRequest(restUrl + "/sitemaps/" + OPENHAB_SITEMAP, sitemapStr);
#endif

    deserializeJson(jsonDoc, sitemapStr, DeserializationOption::NestingLimit(50));

    delete rootPage;

    JsonObject rootPageJson = jsonDoc.as<JsonObject>()["homepage"];
    rootPage = new M5PanelPage(rootPageJson);
    debug(F("updateSiteMap"), "current page: " + currentPage);
    if (currentPage == "")
    {
        currentPage = rootPage->identifier;
    }

    if (!rootPage->draw(currentPage, &canvas))
    {
        // reset page because the formerly displayed page disappeared
        currentPage = rootPage->identifier;
        rootPage->draw(&canvas);
    }
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

boolean readSavedState()
{
    if (LittleFS.exists(SAVED_STATE_FILE))
    {
        File savedState = LittleFS.open(SAVED_STATE_FILE);
        currentPage = savedState.readString();
        debug(F("readSavedState"), "read current page from saved file: " + currentPage);
        savedState.close();
        LittleFS.remove(SAVED_STATE_FILE);
        return true;
    }
    else
    {
        return false;
    }
}

void setup()
{
    Serial.println(F("Setup start..."));

    if (M5.BtnP.read() == 0)
    {
        // was booted up on automatic request, sleep again quickly
        interactionStartMillis = (-TIME_UNTIL_SLEEP + UPTIME_AUTOMATIC_BOOT) * 1000;
    }

    M5.begin(true, false, true, false, false); // bool touchEnable = true, bool SDEnable = false, bool SerialEnable = true, bool BatteryADCEnable = false, bool I2CEnable = false
    gpio_deep_sleep_hold_dis();
    M5.disableEXTPower();

    // M5.EPD.SetRotation(180);
    M5.EPD.Clear(false);
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

    // TODO : Should fail and stop if littlefs error

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

    // read and remove saved state
    readSavedState();

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
    }

    updateSiteMap();

    if (!SAMPLE_SITEMAP)
    {
        subscribe();
    }
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
        debug(F("checkSubscription"), "received subscription " + subscriptionReceivedData);
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
        M5.TP.update();
        bool is_finger_up = M5.TP.isFingerUp();
        if (is_finger_up)
        {
            if (_last_pos_x != 0xFFFF && _last_pos_y != 0xFFFF)
            {
                // user interaction detected
                debug(F("checkTouch"), "resetting interactionStartMillis");
                interactionStartMillis = loopStartMillis;

                // process touch on finger lifting
                String newPage = rootPage->processTouch(currentPage, _last_pos_x, _last_pos_y, &canvas);
                if (currentPage != newPage)
                {
                    currentPage = newPage;
                    debug(F("checkTouch"), "new current page after touch: " + currentPage);
                    updateAndSubscribeCurrentPage();
                }
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

void showBatteryIndicator()
{
    M5.BatteryADCBegin();

    canvas.createCanvas(150, 40);

    int img_y = 5;
    int img_x = 40;
    int img_height = 32;
    int img_width = 32;

    canvas.pushImage(img_x, img_y, 32, 32, ImageResource_status_bar_battery_32x32);
    uint32_t vol = M5.getBatteryVoltage();

    if (vol < 3300)
    {
        vol = 3300;
    }
    else if (vol > 4350)
    {
        vol = 4350;
    }
    float battery = (float)(vol - 3300) / (float)(4350 - 3300);
    if (battery <= 0.01)
    {
        battery = 0.01;
    }
    if (battery > 1)
    {
        battery = 1;
    }
    uint8_t px = battery * 25;
    char buf[4];
    sprintf(buf, "%d%%", (int)(battery * 100));
    canvas.fillRect(img_x + 3, img_y + 10, px, 13, 15);

    canvas.setTextDatum(ML_DATUM);
    canvas.setTextSize(FONT_SIZE_LABEL_SMALL);
    canvas.drawString(buf, img_x + img_width + 5, img_y + img_height / 2);
    canvas.pushCanvas(0, 500, UPDATE_MODE_GLD16);

    canvas.deleteCanvas();
}

void shutdown()
{
    // TODO draw hint for wakeup by button press
    // TODO configure to wake up from side button if possible

    showBatteryIndicator();

    File savedState = LittleFS.open(SAVED_STATE_FILE, "w", true);
    savedState.print(currentPage.c_str());
    savedState.close();

    canvas.createCanvas(150, 30);
    canvas.setTextSize(FONT_SIZE_LABEL);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("ZzzZzz", 40, 0);
    canvas.pushCanvas(0, 70, UPDATE_MODE_DU);

    canvas.deleteCanvas();

    delay(1000);

    // shut down M5 to save energy
    // M5.shutdown(20);

    M5.disableEPDPower();
    M5.disableEXTPower();
    M5.disableMainPower();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW); // TOUCH_INT
    esp_sleep_enable_timer_wakeup(REFRESH_INTERVAL * 1000000);
    esp_deep_sleep_start();
    while (1)
        ;
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

    unsigned long durationSinceInteraction = loopStartMillis - interactionStartMillis;

    if (durationSinceInteraction > (TIME_UNTIL_SLEEP * 1000))
    {
        debug(F("loop"), "Shutting down after " + String(durationSinceInteraction) + "ms since interaction");
        shutdown();
    }

    delay(50);
}