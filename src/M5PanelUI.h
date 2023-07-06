#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5EPD.h>
#include <map>

#define FONT_SIZE_LABEL 32
#define FONT_SIZE_LABEL_SMALL 20
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
    M5PanelPage(M5PanelUIElement *selection, JsonObject json, int pageIndex);

    void drawElement(M5EPD_Canvas *canvas, int elementIndex, boolean updateImmediately);
    void drawNavigation(M5EPD_Canvas *canvas);
    String processNavigationTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas);
    String processElementTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas);

public:
    String title;
    M5PanelUIElement *elements[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    M5PanelPage *previous = NULL;
    M5PanelPage *next = NULL;
    M5PanelUIElement *parent = NULL;

    String identifier = "";

    M5PanelPage(JsonObject json);
    /** create choices page */
    M5PanelPage(M5PanelUIElement *selection, JsonObject json);
    ~M5PanelPage();

    boolean draw(String pageIdentifier, M5EPD_Canvas *canvas);
    void draw(M5EPD_Canvas *canvas);

    /**
     * react to touch in a certain place and return the new currentElement
     */
    String processTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas);

    /**
     * update widget and report the page where this was found
     */
    String updateWidget(JsonObject json, String widgetId, String currentPage, M5EPD_Canvas *canvas);
};

class M5PanelUIElement
{
private:
    void drawFrame(M5EPD_Canvas *canvas, int size);
    void drawTitle(M5EPD_Canvas *canvas, int size);
    void drawIcon(M5EPD_Canvas *canvas, int size);
    void drawStatusAndControlArea(M5EPD_Canvas *canvas, int size);
    boolean updateFromCurrentJson();

public:
    JsonObject json;
    M5PanelElementType type;
    String title;
    String icon;
    String state;
    M5PanelPage *parent = NULL;
    M5PanelPage *detail = NULL;
    M5PanelPage *choices = NULL;

    String identifier = "";

    M5PanelUIElement(JsonObject json);
    /** create choice element */
    M5PanelUIElement(M5PanelUIElement *selection, JsonObject json, int i);
    ~M5PanelUIElement();

    boolean update(JsonObject json);

    void draw(M5EPD_Canvas *canvas, int x, int y, int size);

    String forwardTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas);
    String processTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas, int *highlightX, int *highlightY, void (**callback)(M5PanelUIElement *));
};
