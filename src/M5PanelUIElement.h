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

    M5PanelUIElement(M5PanelPage *parent, JsonObject json);
    /** create choice element */
    M5PanelUIElement(M5PanelPage *parent, M5PanelUIElement *selection, JsonObject json, int i);
    ~M5PanelUIElement();

    boolean update(JsonObject json);

    void draw(M5EPD_Canvas *canvas, int x, int y, int size);

    M5PanelPage *forwardTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas);
    M5PanelPage *processTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas, int *highlightX, int *highlightY, boolean (**callback)(M5PanelUIElement *));
};