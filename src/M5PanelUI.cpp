#include "M5PanelUI.h"
#include <ArduinoJson.h>

// Constants

#define MAX_ELEMENTS 6

#define ELEMENT_ROWS 2
#define ELEMENT_COLS 3

#define PANEL_WIDTH 960
#define PANEL_HEIGHT 540

#define MARGIN 10

// PANEL_HEIGHT / ELEMENT_ROWS - MARGIN
#define ELEMENT_AREA_SIZE 260
// PANEL_WIDTH - ELEMENT_COLS * ELEMENT_AREA_SIZE - 2 * MARGIN
#define NAV_WIDTH 150
#define NAV_MARGIN_TOP_BOTTOM 100

#define ELEMENT_CONTROL_HEIGHT 80
#define FONT_SIZE_ELEMENT_TITLE 32

#define LINE_THICKNESS 3

// M5PanelPage

M5PanelPage::M5PanelPage(JsonObject json) : M5PanelPage(json, 0) {}

M5PanelPage::M5PanelPage(JsonObject json, int pageIndex)
{
    String titleString = json["title"];
    // the root page has title, the subpages labels
    title = titleString != "null" ? titleString : json["label"];
    JsonArray widgets = json["widgets"];
    size_t pageOffset = pageIndex * 6;

    Serial.println("Initialized page: " + title + " (number " + (pageIndex + 1) + ")");

    numElements = min((size_t)MAX_ELEMENTS, widgets.size() - pageOffset);

    // initialize elements on page
    for (size_t i = 0; i < numElements; i++)
    {
        JsonObject elementJson = widgets[pageOffset + i];
        elements[i] = new M5PanelUIElement(elementJson);
        elements[i]->parent = this;
    }

    // initialize additional pages if necessary
    if (widgets.size() - pageOffset <= MAX_ELEMENTS)
    {
        // no additional pages needed
        return;
    }

    next = new M5PanelPage(json, pageIndex + 1);
    next->previous = this;
}

M5PanelPage::~M5PanelPage()
{
    Serial.print("delete page: " + title);
    for (size_t i = 0; i < MAX_ELEMENTS; i++)
    {
        delete elements[i];
    }
    delete previous;
    delete next;
}

void M5PanelPage::draw(M5EPD_Canvas *canvas)
{
    drawNavigation(canvas);
    for (size_t i = 0; i < numElements; i++)
    {
        int y = MARGIN + (i / ELEMENT_COLS) * ELEMENT_AREA_SIZE;
        int x = NAV_WIDTH + MARGIN + (i % ELEMENT_COLS) * ELEMENT_AREA_SIZE;
        elements[i]->draw(canvas, x, y, ELEMENT_AREA_SIZE);
    }
}

void M5PanelPage::drawNavigation(M5EPD_Canvas *canvas)
{
    int arrowAreaHeight = PANEL_HEIGHT - 2 * NAV_MARGIN_TOP_BOTTOM;

    canvas->createCanvas(NAV_WIDTH * 2, arrowAreaHeight);
    canvas->clear();

    void (M5EPD_Canvas::*next_triangle)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t);
    next_triangle = next != NULL ? &M5EPD_Canvas::fillTriangle : &M5EPD_Canvas::drawTriangle;

    void (M5EPD_Canvas::*previous_triangle)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t);
    previous_triangle = previous != NULL ? &M5EPD_Canvas::fillTriangle : &M5EPD_Canvas::drawTriangle;

    void (M5EPD_Canvas::*back_triangle)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t);
    back_triangle = parent != NULL ? &M5EPD_Canvas::fillTriangle : &M5EPD_Canvas::drawTriangle;

    int arrowMargin = NAV_WIDTH / 5;
    int arrowHeight = (arrowAreaHeight - arrowMargin) / 3;
    int arrowMiddle = NAV_WIDTH / 2;
    int arrowRight = NAV_WIDTH - arrowMargin;

    int secondArrowTop = arrowHeight + arrowMargin;
    int secondArrowBottom = 2 * arrowHeight;

    int backArrowLeft = arrowMargin + arrowMargin / 2;
    int backArrowTop = secondArrowBottom + arrowMargin;
    int backArrowLeftY = backArrowTop + arrowHeight / 2;
    int backArrowRight = NAV_WIDTH - backArrowLeft;
    int backArrowBottom = backArrowTop + arrowHeight;

    (canvas->*next_triangle)(arrowMargin, arrowHeight, arrowMiddle, arrowMargin, arrowRight, arrowHeight, 15);
    (canvas->*previous_triangle)(arrowMargin, secondArrowTop, arrowRight, secondArrowTop, arrowMiddle, secondArrowBottom, 15);
    (canvas->*back_triangle)(backArrowLeft, backArrowLeftY, backArrowRight, backArrowTop + 5, backArrowRight, backArrowBottom - 5, 15);

    canvas->pushCanvas(0, NAV_MARGIN_TOP_BOTTOM, UPDATE_MODE_DU);
}

// M5PanelUIElement

M5PanelUIElement::M5PanelUIElement(JsonObject json)
{
    title = json["label"].as<String>(); // TODO if empty -> item label?
    icon = json["icon"].as<String>();

    // TODO store item with stateDescription, commandDescription

    String stateString = json["item"]["state"];
    state = stateString == "NULL" ? "" : stateString; // TODO use mappings and format from sitemap

    String typeString = json["type"];
    if (typeString == "Frame")
    {
        type = M5PanelElementType::Frame;
    }
    else if (typeString == "Selection")
    {
        type = M5PanelElementType::Selection;
        // TODO create "choices" page
    }
    else if (typeString == "Setpoint")
    {
        type = M5PanelElementType::Setpoint;
    }
    else if (typeString == "Slider")
    {
        type = M5PanelElementType::Slider;
    }
    else if (typeString == "Switch")
    {
        type = M5PanelElementType::Switch;
    }
    else if (typeString == "Text")
    {
        type = M5PanelElementType::Text;
    }
    else
    {
        type = M5PanelElementType::Text; // sitemap contains bad element type, make it only text
    }

    Serial.println("Initialized element: " + title + " with icon: " + icon + " state: " + state + " type: " + typeString);

    // frames can have direct widgets, other items always seem to have a "linked page"
    JsonArray widgets = json["widgets"];
    JsonObject linkedPageJson = json["linkedPage"];
    if (widgets.size() == 0 && linkedPageJson.isNull())
    {
        // we do not need a child element here
        return;
    }

    detail = new M5PanelPage(widgets.size() != 0 ? json : linkedPageJson);
    M5PanelPage *detailPage = detail;
    while (detailPage != NULL)
    {
        detailPage->parent = this;
        detailPage = detailPage->next;
    }
}

M5PanelUIElement::~M5PanelUIElement()
{
    Serial.println("delete element: " + title);
    delete detail;
    delete choices;
}

void M5PanelUIElement::draw(M5EPD_Canvas *canvas, int x, int y, int size)
{
    canvas->createCanvas(size - 2 * MARGIN, size - 2 * MARGIN);
    canvas->clear();

    int elementSize = size - 2 * MARGIN;

    drawFrame(canvas, elementSize);

    drawIcon(canvas, elementSize);

    drawTitle(canvas, elementSize);

    drawStatusAndControlArea(canvas, elementSize);

    canvas->pushCanvas(x + MARGIN, y + MARGIN, UPDATE_MODE_DU);
}

void M5PanelUIElement::drawFrame(M5EPD_Canvas *canvas, int elementSize)
{
    int radius = 10;

    int innerRectStart = LINE_THICKNESS;
    int innerRectSize = elementSize - 2 * LINE_THICKNESS;
    int innerRadius = radius - LINE_THICKNESS;
    canvas->fillRoundRect(0, 0, elementSize, elementSize, radius, 15);
    canvas->fillRoundRect(innerRectStart, innerRectStart, innerRectSize, innerRectSize, innerRadius, 0);
}

void M5PanelUIElement::drawTitle(M5EPD_Canvas *canvas, int elementSize)
{
    int elementCenter = elementSize / 2;
    canvas->setTextSize(FONT_SIZE_LABEL);
    canvas->setTextDatum(TC_DATUM);
    canvas->drawString(title, elementCenter, MARGIN);
}

void M5PanelUIElement::drawIcon(M5EPD_Canvas *canvas, int size)
{
    // TODO
}

void M5PanelUIElement::drawStatusAndControlArea(M5EPD_Canvas *canvas, int elementSize)
{
    int elementCenter = elementSize / 2;
    int controlY = elementSize - ELEMENT_CONTROL_HEIGHT;
    int controlYCenter = controlY + ELEMENT_CONTROL_HEIGHT / 2;

    // TODO draw only if controllable directly (setpoint / slider)
    canvas->drawLine(0, controlY, elementSize - LINE_THICKNESS, controlY, LINE_THICKNESS, 15);
    canvas->setTextSize(FONT_SIZE_STATUS_CENTER);
    canvas->setTextDatum(ML_DATUM);
    canvas->drawString("-", MARGIN, controlYCenter);
    canvas->setTextDatum(MR_DATUM);
    canvas->drawString("+", elementSize - MARGIN, controlYCenter);

    // TODO draw only if status exists (not frame)
    canvas->setTextSize(FONT_SIZE_LABEL);
    canvas->setTextDatum(MC_DATUM);
    canvas->drawString(state, elementCenter, controlYCenter);
}