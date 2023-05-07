#include <Arduino.h>
#include <ArduinoJson.h>

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

class M5PanelUIElement
{
public:
    M5PanelElementType type;
    String title;
    String icon;
    String state;
    M5PanelPage *parent = NULL;
    M5PanelPage *detail = NULL;
    M5PanelPage *choices = NULL;

    M5PanelUIElement(JsonObject json);
    ~M5PanelUIElement();
};

class M5PanelPage
{
private:
    M5PanelPage(JsonObject json, int pageIndex);

public:
    String title;
    M5PanelUIElement *elements[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    M5PanelPage *previous = NULL;
    M5PanelPage *next = NULL;
    M5PanelUIElement *parent = NULL;

    M5PanelPage(JsonObject json);
    ~M5PanelPage();
};
