#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5EPD.h>

#define FONT_SIZE_LABEL 32
#define FONT_SIZE_STATUS_CENTER 48
#define FONT_SIZE_STATUS_BOTTOM 36
#define FONT_SIZE_SYSINFO 15

class M5PanelUIElement;

class M5PanelPage;

enum class M5PanelElementType
{
    Frame,
    Selection,
    Choice,
    Setpoint,
    Slider,
    Switch,
    Text
};

class M5PanelPage
{
private:
    size_t numElements;
    M5PanelPage(JsonObject json, int pageIndex);
    void drawNavigation(M5EPD_Canvas *canvas);

public:
    String title;
    M5PanelUIElement *elements[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    M5PanelPage *previous = NULL;
    M5PanelPage *next = NULL;
    M5PanelUIElement *parent = NULL;

    M5PanelPage(JsonObject json);
    ~M5PanelPage();

    void draw(M5EPD_Canvas *canvas);
};

class M5PanelUIElement
{
private:
    void drawFrame(M5EPD_Canvas *canvas, int size);
    void drawTitle(M5EPD_Canvas *canvas, int size);
    void drawIcon(M5EPD_Canvas *canvas, int size);
    void drawStatusAndControlArea(M5EPD_Canvas *canvas, int size);

public:
    M5PanelElementType type;
    String title; // TODO replace with fixed-length char[]
    String icon;  // TODO replace with fixed-length char[]
    String state; // TODO replace with fixed-length char[]
    M5PanelPage *parent = NULL;
    M5PanelPage *detail = NULL;
    M5PanelPage *choices = NULL;

    M5PanelUIElement(JsonObject json);
    ~M5PanelUIElement();

    void draw(M5EPD_Canvas *canvas, int x, int y, int size);
};
