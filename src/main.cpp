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

#define FONT_CACHE_SIZE 256

// Global vars
M5EPD_Canvas canvas(&M5.EPD);
M5EPD_Canvas touchCanvas(&M5.EPD);

WiFiClient subscribeClient;

String restUrl = "http://" + String(OPENHAB_HOST) + String(":") + String(OPENHAB_PORT) + String("/rest");
String subscriptionId = "";

DynamicJsonDocument jsonDoc(60000); // size to be checked

M5PanelPage *rootPage = NULL;
String currentPage = "" + String(OPENHAB_SITEMAP) + "_0";

#define PAGE_CHANGE_WAIT 10000
SemaphoreHandle_t pageChangeSemaphore = xSemaphoreCreateBinary();

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

bool httpRequest(String &url, String &response)
{
    if (SAMPLE_SITEMAP)
    {
        return false;
    }

    log_d("httpRequest: HTTP request to %s", String(url).c_str());
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.reconnect();
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        //attempted to reconnect but it did not work - give up.
        log_d(ERR_WIFI_NOT_CONNECTED);
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
        log_d("ERROR: HTTP code %d", httpCode);
        response = String(ERR_HTTP_ERROR) + String(httpCode);
        httpClient.end();
        return false;
    }
    response = httpClient.getString();
    httpClient.end();
    log_d("httpRequest: HTTP request done");
    return true;
}

String getSitemapPageId(String page)
{
    int cutoffIdx = page.length() - 1;
    int choicesIdx = page.lastIndexOf("_choices_");
    if (choicesIdx > 0)
    {
        return page.substring(0, choicesIdx);
    }

    int pageSeparatorIdx = page.lastIndexOf("_");
    if (pageSeparatorIdx > 0)
    {
        return page.substring(0, pageSeparatorIdx);
    }

    return OPENHAB_SITEMAP;
}

String getCurrentSitemapPageId()
{
    return getSitemapPageId(currentPage);
}

DynamicJsonDocument subscribePage(String pageId)
{
    String sitemapPageId = getSitemapPageId(pageId);
    String pageUpdate;
    DynamicJsonDocument jsonData(30000);
    if (httpRequest(restUrl + "/sitemaps/" + OPENHAB_SITEMAP + "/" + sitemapPageId + "?subscriptionid=" + subscriptionId, pageUpdate))
    {
        deserializeJson(jsonData, pageUpdate);
    }
    return jsonData;
}

void updateAndSubscribePage(M5PanelPage *page)
{
    DynamicJsonDocument jsonData = subscribePage(page->identifier);

    if (jsonData.isNull())
    {
        return;
    }

    page->updateAllWidgets(jsonData);
    jsonData.clear();
}

void updateAndSubscribeCurrentPage()
{
    DynamicJsonDocument jsonData = subscribePage(currentPage);

    if (jsonData.isNull())
    {
        return;
    }

    JsonArray widgets = jsonData["widgets"];
    for (size_t i = 0; i < widgets.size(); i++)
    {
        String widgetId = widgets[i]["widgetId"].as<String>();
        rootPage->updateWidget(widgets[i], widgetId, currentPage, &canvas);
    }

    jsonData.clear();
}

bool subscribe()
{

    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.reconnect();
    }

    String subscribeResponse;

    WiFiClient wifiClient;
    HTTPClient httpClient;

    httpClient.useHTTP10(true);
    httpClient.begin(wifiClient, restUrl + "/sitemaps/events/subscribe");
    int httpCode = httpClient.POST("");
    if (httpCode != HTTP_CODE_OK)
    {
        log_d("ERROR: HTTP code %d", httpCode);
        httpClient.end();
        return false;
    }
    subscribeResponse = httpClient.getString();
    httpClient.end();

    DynamicJsonDocument subscribeResponseJson(3000);
    deserializeJson(subscribeResponseJson, subscribeResponse);

    // String subscriptionURL = subscribeResponseJson["Location"].as<String>();
    String baseUrl = subscribeResponseJson["context"]["headers"]["Location"][0];
    log_d("subscribe: Full subscriptionURL: %s", baseUrl.c_str());

    subscribeResponseJson.clear();

    subscriptionId = baseUrl.substring(baseUrl.lastIndexOf("/") + 1);

    String subscriptionURL = baseUrl.substring(baseUrl.indexOf("/rest/sitemaps"));
    String parametrizedUrl = subscriptionURL + "?sitemap=" + OPENHAB_SITEMAP + "&pageid=" + getCurrentSitemapPageId();
    log_d("subscribe: subscriptionURL: %s", parametrizedUrl.c_str());
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
    log_d("updateSiteMap: Load sample sitemap");
    File f = LittleFS.open("/sample_sitemap.json");
    sitemapStr = f.readString();
#else
    httpRequest(restUrl + "/sitemaps/" + OPENHAB_SITEMAP, sitemapStr);
#endif

    deserializeJson(jsonDoc, sitemapStr, DeserializationOption::NestingLimit(50));

    delete rootPage;

    JsonObject rootPageJson = jsonDoc.as<JsonObject>()["homepage"];
    rootPage = new M5PanelPage(rootPageJson);
    log_d("updateSiteMap: current page: %s", currentPage.c_str());
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
    log_d("parseSubscriptionData: %s", jsonDataStr.c_str());
    if (!jsonData["widgetId"].isNull()) // Data Widget (subscription)
    {
        String widgetId = jsonData["widgetId"];
        log_d("parseSubscriptionData: Widget changed: %s", widgetId.c_str());

        xSemaphoreTake(pageChangeSemaphore, PAGE_CHANGE_WAIT / portTICK_PERIOD_MS);
        // CRITICAL SECTION PAGE UPDATE

        // update widget and redraw if widget on currently shown page
        rootPage->updateWidget(jsonData.as<JsonObject>(), widgetId, currentPage, &canvas);

        // CRITICAL SECTION PAGE UPDATE END
        xSemaphoreGive(pageChangeSemaphore);
    }
    else if (!jsonData["TYPE"].isNull())
    {
        String jsonDataType = jsonData["TYPE"];
        if (jsonDataType.equals("ALIVE"))
        {
            log_d("parseSubscriptionData: Subscription Alive");
        }
        else if (jsonDataType.equals("SITEMAP_CHANGED"))
        {
            xSemaphoreTake(pageChangeSemaphore, PAGE_CHANGE_WAIT / portTICK_PERIOD_MS);
            // CRITICAL SECTION PAGE UPDATE

            log_d("parseSubscriptionData: Sitemap changed, reloading");
            updateSiteMap();
            updateAndSubscribeCurrentPage();

            // CRITICAL SECTION PAGE UPDATE END
            xSemaphoreGive(pageChangeSemaphore);
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
        log_d("setTimeZone: OpenHAB timezone = %s", timezone.c_str());
        doc.clear();
        openhabTZ.setLocation(timezone);
    }
    else
    {
        log_d("setTimeZone: Could not get OpenHAB timezone");
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
    log_d("syncRTC: Time="+String(RTCtime.hour)+":"+String(RTCtime.min)+":"+String(RTCtime.sec));
    M5.RTC.setTime(&RTCtime);

    RTCDate.year = dateTime.substring(0,4).toInt();
    RTCDate.mon  = dateTime.substring(5,7).toInt();
    RTCDate.day  = dateTime.substring(8,10).toInt();
    log_d("syncRTC: Date="+String(RTCDate.year)+"-"+String(RTCDate.mon)+"-"+String(RTCDate.day));
    M5.RTC.setDate(&RTCDate);
    */
}

boolean readSavedState()
{
    if (LittleFS.exists(SAVED_STATE_FILE))
    {
        File savedState = LittleFS.open(SAVED_STATE_FILE);
        currentPage = savedState.readString();
        log_d("readSavedState: read current page from saved file: %s", currentPage.c_str());
        savedState.close();
        LittleFS.remove(SAVED_STATE_FILE);
        return true;
    }
    else
    {
        return false;
    }
}

void checkSubscription()
{
    // Subscribe or re-subscribe to sitemap
    if (!subscribeClient.connected())
    {
        log_d("subscribeClient not connected, connecting...");
        if (!subscribe())
        {
            delay(300);
        }
    }

    // Check and get subscription data
    while (subscribeClient.available())
    {
        String subscriptionReceivedData = subscribeClient.readStringUntil('\n');
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
                log_d("checkTouch: resetting interactionStartMillis");
                interactionStartMillis = loopStartMillis;

                // process touch on finger lifting
                M5PanelPage *newPage = rootPage->processTouch(currentPage, _last_pos_x, _last_pos_y, &touchCanvas);
                if (currentPage != newPage->identifier)
                {
                    xSemaphoreTake(pageChangeSemaphore, PAGE_CHANGE_WAIT / portTICK_PERIOD_MS);
                    // CRITICAL SECTION OF PAGE CHANGE

                    int oldPageChoicesIdx = currentPage.lastIndexOf("_choices_");
                    int newPageChoicesIdx = newPage->identifier.lastIndexOf("_choices_");

                    currentPage = newPage->identifier;
                    log_d("checkTouch: new current page after touch: %s", currentPage.c_str());
                    if (oldPageChoicesIdx < 0 && newPageChoicesIdx < 0) // no subscription update if navigating from / to choices
                    {
                        updateAndSubscribePage(newPage);
                    }
                    newPage->draw(&touchCanvas);

                    // CRITICAL SECTION OF PAGE CHANGE END
                    xSemaphoreGive(pageChangeSemaphore);
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

    touchCanvas.createCanvas(150, 40);

    int img_y = 5;
    int img_x = 40;
    int img_height = 32;
    int img_width = 32;

    touchCanvas.pushImage(img_x, img_y, 32, 32, ImageResource_status_bar_battery_32x32);
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
    char buf[5];
    sprintf(buf, "%d%%", (int)(battery * 100));
    touchCanvas.fillRect(img_x + 3, img_y + 10, px, 13, 15);

    touchCanvas.setTextDatum(ML_DATUM);
    touchCanvas.setTextSize(FONT_SIZE_LABEL_SMALL);
    touchCanvas.drawString(buf, img_x + img_width + 5, img_y + img_height / 2);
    touchCanvas.pushCanvas(0, 500, UPDATE_MODE_GLD16);

    touchCanvas.deleteCanvas();
}

void showWakeUpIndicator()
{
    touchCanvas.createCanvas(400, 15);

    touchCanvas.fillCircle(100, -23, 40, 15);

    touchCanvas.setTextSize(FONT_SIZE_LABEL);
    touchCanvas.setTextDatum(TC_DATUM);
    touchCanvas.setTextColor(0);
    touchCanvas.drawString("^", 100, 0);

    touchCanvas.setTextSize(FONT_SIZE_LABEL_SMALL);
    touchCanvas.setTextColor(15);
    touchCanvas.drawString("3s drücken", 200, 0);

    touchCanvas.pushCanvas(402, 0, UPDATE_MODE_GLD16);
    touchCanvas.deleteCanvas();
}

void showSleepText()
{
    touchCanvas.createCanvas(150, 30);
    touchCanvas.setTextSize(FONT_SIZE_LABEL);
    touchCanvas.setTextDatum(TL_DATUM);
    touchCanvas.drawString("ZzzZzz", 40, 0);
    touchCanvas.pushCanvas(0, 70, UPDATE_MODE_DU);
    touchCanvas.deleteCanvas();
}

void shutdown()
{
    // TODO draw hint for wakeup by button press
    // TODO configure to wake up from side button if possible

    showBatteryIndicator();

    showWakeUpIndicator();

    // showSleepText();

    File savedState = LittleFS.open(SAVED_STATE_FILE, "w", true);
    savedState.print(currentPage.c_str());
    savedState.close();

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

void loop() {}

// Loop
void updateLoop(void *pvParameters)
{
    while (true)
    {
        if (!SAMPLE_SITEMAP)
        {
            checkSubscription();
        }

        events(); // for ezTime

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void interactionLoop(void *pvParameters)
{
    while (true)
    {
        loopStartMillis = millis();

        checkTouch();

        unsigned long durationSinceInteraction = loopStartMillis - interactionStartMillis;

        if (durationSinceInteraction > (TIME_UNTIL_SLEEP * 1000))
        {
            log_d("interactionLoop: Shutting down after %d ms since interaction", durationSinceInteraction);
            shutdown();
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    log_d("Setup start...");

    xSemaphoreGive(pageChangeSemaphore); // binary semaphore must first be given to be free

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
    /*log_d("Inizializing FS...");
    if (SPIFFS.begin())
    {
        log_d("SPIFFS mounted correctly.");
    }
    else
    {
        log_d("!An error occurred during SPIFFS mounting");
    }*/

    log_d("Inizializing LittleFS FS...");
    if (LittleFS.begin())
    {
        log_d("LittleFS mounted correctly.");
    }
    else
    {
        log_d("!An error occurred during LittleFS mounting");
    }

    // Get all information of LittleFS
    unsigned int totalBytes = LittleFS.totalBytes();
    unsigned int usedBytes = LittleFS.usedBytes();

    // TODO : Should fail and stop if littlefs error

    log_d("===== File system info =====");

    log_d("Total space: %d byte", totalBytes);

    log_d("Total space used: %d byte", usedBytes);

    esp_err_t errorCode = canvas.loadFont("/FreeSansBold.ttf", LittleFS);
    touchCanvas.loadFont("/FreeSansBold.ttf", LittleFS);
    // TODO : Should fail and stop if font not found
    log_d("Font load exit code: %d", errorCode);

    canvas.createRender(FONT_SIZE_LABEL, FONT_CACHE_SIZE);
    canvas.createRender(FONT_SIZE_LABEL_SMALL, FONT_CACHE_SIZE);
    canvas.createRender(FONT_SIZE_CONTROL, FONT_CACHE_SIZE);

    touchCanvas.createRender(FONT_SIZE_LABEL, FONT_CACHE_SIZE);
    touchCanvas.createRender(FONT_SIZE_LABEL_SMALL, FONT_CACHE_SIZE);

    canvas.setTextSize(FONT_SIZE_LABEL);

    // read and remove saved state
    readSavedState();

    // Setup Wifi
    if (!SAMPLE_SITEMAP)
    {
        log_d("Starting Wifi");
        WiFi.begin(WIFI_SSID, WIFI_PSK);
        while (WiFi.status() != WL_CONNECTED)
        {
            delay(500);
            log_d(".");
        }
        log_d("WiFi connected");
        log_d("IP address: %s", String(WiFi.localIP()).c_str());

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

    xTaskCreatePinnedToCore(interactionLoop, "interactionLoop", 4096, NULL, 2,
                            NULL, 1);

    xTaskCreatePinnedToCore(updateLoop, "updateLoop", 4096, NULL, 1,
                            NULL, 0);

    vApplicationIdleHook();
}
