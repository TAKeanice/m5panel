#include <ArduinoJson.h>
#include <M5EPD.h>

class M5PanelUIElement;

class M5PanelPage
{
private:
    size_t numElements;

    M5PanelPage(M5PanelUIElement *parent, JsonObject json, int pageIndex);
    M5PanelPage(JsonObject json, M5PanelUIElement *selection, int pageIndex);

    void drawElement(M5EPD_Canvas *canvas, int elementIndex, boolean updateImmediately);
    void drawNavigation(M5EPD_Canvas *canvas);
    M5PanelPage *processNavigationTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas);
    M5PanelPage *processElementTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas);

public:
    String title;
    int pageIndex;
    M5PanelUIElement *elements[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    M5PanelPage *previous = NULL;
    M5PanelPage *next = NULL;
    M5PanelUIElement *parent = NULL;

    String identifier = "";

    M5PanelPage(M5PanelUIElement *parent, JsonObject json);
    /** create choices page */
    M5PanelPage(JsonObject json, M5PanelUIElement *selection);
    ~M5PanelPage();

    boolean draw(String pageIdentifier, M5EPD_Canvas *canvas);
    void draw(M5EPD_Canvas *canvas);

    /**
     * react to touch in a certain place and return the new currentElement
     */
    M5PanelPage *processTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas);

    /**
     * update widget and report the page where this was found
     */
    M5PanelPage *updateWidget(JsonObject json, String widgetId, String currentPage, M5EPD_Canvas *canvas);

    void updateAllWidgets(DynamicJsonDocument json);
};
