#include "M5PanelUI.h"
#include <ArduinoJson.h>

#include <HTTPClient.h>

// Constants

#define ELEMENT_ROWS 2
#define ELEMENT_COLS 3

// ELEMENT_ROWS * ELEMENT_COLS
#define MAX_ELEMENTS 6

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

WiFiClient commandWifiClient;

// Utility functions

String parseWidgetLabel(String label)
{
    int openingBracket = label.lastIndexOf('[');
    int closingBracket = label.lastIndexOf(']');
    String parsedLabel;
    if (openingBracket == -1 || closingBracket == -1) // Value not found
    {
        parsedLabel = label;
    }
    else
    {
        parsedLabel = label.substring(0, openingBracket);
    }
    parsedLabel.trim();
    return parsedLabel;
}

// M5PanelPage

M5PanelPage::M5PanelPage(JsonObject json) : M5PanelPage(json, 0) {}

M5PanelPage::M5PanelPage(JsonObject json, int pageIndex)
{
    String widgetId = json["widgetId"].isNull() ? "" : json["widgetId"].as<String>();
    String id = json["id"].isNull() ? "" : json["id"].as<String>();
    identifier = id + widgetId + "_" + pageIndex;

    // the root page has title, the subpages labels
    String label = json["label"].isNull() ? "" : json["label"].as<String>();
    String titleString = json["title"].isNull() ? "" : json["title"].as<String>();
    title = parseWidgetLabel(label + titleString);

    Serial.println("Initialized page: " + title + " (number " + (pageIndex + 1) + ")");

    size_t pageOffset = pageIndex * MAX_ELEMENTS;
    JsonArray widgets = json["widgets"];
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

M5PanelPage::M5PanelPage(M5PanelUIElement *selection, JsonObject json) : M5PanelPage(selection, json, 0) {}

M5PanelPage::M5PanelPage(M5PanelUIElement *selection, JsonObject json, int pageIndex)
{
    JsonArray choices = json["item"]["stateDescription"]["options"];

    identifier = selection->identifier + "_choices_" + pageIndex;
    title = selection->title;
    size_t pageOffset = pageIndex * MAX_ELEMENTS;
    numElements = min((size_t)MAX_ELEMENTS, choices.size() - pageOffset);
    // initialize choice elements
    for (size_t i = 0; i < numElements; i++)
    {
        elements[i] = new M5PanelUIElement(selection, json, pageOffset + i);
        elements[i]->parent = this;
    }
    if (choices.size() - pageOffset <= MAX_ELEMENTS)
    {
        // no additional pages needed
        return;
    }

    next = new M5PanelPage(selection, json, pageIndex + 1);
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

boolean M5PanelPage::draw(String pageIdentifier, M5EPD_Canvas *canvas)
{
    if (identifier == pageIdentifier)
    {
        draw(canvas);
        return true;
    }

    for (size_t i = 0; i < numElements; i++)
    {
        M5PanelPage *detailPage = elements[i]->detail;
        if (detailPage != NULL && detailPage->draw(pageIdentifier, canvas))
        {
            return true;
        }
    }

    return next->draw(pageIdentifier, canvas);
}

void M5PanelPage::draw(M5EPD_Canvas *canvas)
{
    // clear
    canvas->createCanvas(PANEL_WIDTH, PANEL_HEIGHT);
    canvas->clear();
    canvas->pushCanvas(NAV_WIDTH, 0, UPDATE_MODE_DU);
    canvas->deleteCanvas();

    drawNavigation(canvas);

    // draw elements
    for (size_t i = 0; i < numElements; i++)
    {
        drawElement(canvas, i);
    }
}

void M5PanelPage::drawElement(M5EPD_Canvas *canvas, int elementIndex)
{
    int y = MARGIN + (elementIndex / ELEMENT_COLS) * ELEMENT_AREA_SIZE;
    int x = NAV_WIDTH + MARGIN + (elementIndex % ELEMENT_COLS) * ELEMENT_AREA_SIZE;
    elements[elementIndex]->draw(canvas, x, y, ELEMENT_AREA_SIZE);
}

void M5PanelPage::drawNavigation(M5EPD_Canvas *canvas)
{
    // page title
    canvas->createCanvas(NAV_WIDTH, NAV_MARGIN_TOP_BOTTOM);
    canvas->setTextSize(FONT_SIZE_LABEL);
    canvas->setTextArea(MARGIN, MARGIN, NAV_WIDTH, NAV_MARGIN_TOP_BOTTOM);
    canvas->setTextWrap(true);
    canvas->setTextColor(15);
    canvas->println(title);
    canvas->pushCanvas(MARGIN, MARGIN, UPDATE_MODE_GLR16);
    canvas->deleteCanvas();

    // navigation arrows

    int arrowAreaHeight = PANEL_HEIGHT - 2 * NAV_MARGIN_TOP_BOTTOM;

    canvas->createCanvas(NAV_WIDTH, arrowAreaHeight);
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
    canvas->deleteCanvas();
}

String navigate(M5PanelPage *navigationTarget, M5EPD_Canvas *canvas)
{
    navigationTarget->draw(canvas);
    return navigationTarget->identifier;
}

String M5PanelPage::processNavigationTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    Serial.println("Touched navigation area");
    // touch within navigation area
    int arrowAreaHeight = PANEL_HEIGHT - 2 * NAV_MARGIN_TOP_BOTTOM;

    if (y > arrowAreaHeight)
    {
        return identifier; // not touched on arrows
    }
    int arrow = y / (arrowAreaHeight / 3);
    M5PanelPage *toDraw;
    switch (arrow)
    {
    case 0:
        toDraw = next;
        break;
    case 1:
        toDraw = previous;
        break;
    case 2:
        toDraw = parent == NULL ? NULL : parent->parent;
        break;
    default:
        toDraw = NULL;
    }

    if (toDraw == NULL)
    {
        return identifier;
    }
    else
    {
        return navigate(toDraw, canvas);
    }
}

String M5PanelPage::processElementTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    int elementColumn = x / ELEMENT_AREA_SIZE;
    int elementRow = y / ELEMENT_AREA_SIZE;
    int elementIndex = elementColumn + (elementRow * ELEMENT_COLS);
    int originX = elementColumn * ELEMENT_AREA_SIZE;
    int originY = elementRow * ELEMENT_AREA_SIZE;
    if (elementIndex < numElements)
    {
        int highlightX, highlightY;
        void (*callback)(M5PanelUIElement *);
        M5PanelUIElement *element = elements[elementIndex];
        String newCurrentElement = element->processTouch(x - originX, y - originY, canvas, &highlightX, &highlightY, &callback);
        if (newCurrentElement != "")
        {
            return newCurrentElement;
        }
        else
        {
            // react to touch graphically to give immediate feedback, since no navigation occurred
            canvas->pushCanvas(highlightX + originX + NAV_WIDTH + MARGIN, highlightY + originY + MARGIN, UPDATE_MODE_GC16);
            canvas->deleteCanvas();
            if (callback != NULL)
            {
                callback(element);
            }
        }
    }
    return identifier;
}

String M5PanelPage::processTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    if (currentElement == identifier)
    {
        if (x <= NAV_WIDTH)
        {
            return processNavigationTouch(x, y - NAV_MARGIN_TOP_BOTTOM, canvas);
        }
        else
        {
            return processElementTouch(x - NAV_WIDTH - MARGIN, y - MARGIN, canvas);
        }
    }
    else
    {
        // iterate through next pages and let them process the touch
        String newCurrentElement = "";
        if (next != NULL)
        {
            newCurrentElement = next->processTouch(currentElement, x, y, canvas);
        }
        if (newCurrentElement != "")
        {
            return newCurrentElement;
        }
        // since none of the following pages could process the touch either, let child pages process it
        for (size_t i = 0; i < numElements; i++)
        {
            newCurrentElement = elements[i]->forwardTouch(currentElement, x, y, canvas);
            if (newCurrentElement != "")
            {
                return newCurrentElement;
            }
        }
        return "";
    }
}

String M5PanelPage::updateWidget(JsonObject json, String widgetId, String currentPage, M5EPD_Canvas *canvas)
{
    Serial.printf("Updating on page %s\n", identifier.c_str());
    for (size_t i = 0; i < numElements; i++)
    {
        M5PanelUIElement *element = elements[i];
        if (element->identifier != widgetId)
        {
            continue;
        }
        Serial.printf("found widget %s\n", widgetId.c_str());
        // replace widget
        elements[i] = new M5PanelUIElement(json, element);
        if (currentPage == identifier)
        {
            Serial.printf("redraw element %s\n", element->identifier.c_str());
            // redraw widget
            drawElement(canvas, i);
        }
        // widget to update was found on this page
        return identifier;
    }

    // search subpages for element to be updated
    for (size_t i = 0; i < numElements; i++)
    {
        M5PanelUIElement *element = elements[i];
        // forward update command
        if (element->detail != NULL)
        {
            Serial.printf("Updating element %s detail\n", element->identifier.c_str());
            String foundOnPageId = element->detail->updateWidget(json, widgetId, currentPage, canvas);
            if (foundOnPageId != "")
            {
                return foundOnPageId;
            }
        }
    }

    // not found on any of the subpages, search sibling page
    if (next != NULL)
    {
        Serial.printf("Updating next page %s\n", next->identifier.c_str());
        return next->updateWidget(json, widgetId, currentPage, canvas);
    }

    // not found at all in this branch
    return "";
}

// M5PanelUIElement

void setParentForPageAndSuccessors(M5PanelPage *firstChild, M5PanelUIElement *parent)
{
    M5PanelPage *page = firstChild;
    while (page != NULL)
    {
        page->parent = parent;
        page = page->next;
    }
}

String getStateString(JsonObject json)
{
    // get state from label
    String label = json["label"];
    int openingBracket = label.lastIndexOf('[');
    int closingBracket = label.lastIndexOf(']');
    if (openingBracket != -1 && closingBracket != -1) // Value not found
    {
        String value = label.substring(openingBracket + 1, closingBracket);
        value.trim();
        return value;
    }

    // get state from item
    JsonObject item = json["item"];
    if (json["state"].isNull() && item["state"].isNull())
    {
        return "";
    }
    String stateString = json["state"].isNull() ? item["state"].as<String>() : json["state"].as<String>();
    JsonObject stateDescription = item["stateDescription"];
    JsonArray mappings = json["mappings"];
    JsonArray options = mappings.isNull() || mappings.size() == 0 ? stateDescription["options"] : mappings;
    if (!options.isNull())
    {
        for (size_t i = 0; i < options.size(); i++)
        {
            JsonObject option = options[i];
            String optionName = option["value"].isNull() ? option["command"].as<String>() : option["value"].as<String>();
            if (optionName == stateString)
            {
                // replace state value with label
                stateString = option["label"].as<String>();
                break;
            }
        }
    }

    return stateString;
}

M5PanelUIElement::M5PanelUIElement(JsonObject newJson, M5PanelUIElement *oldElement)
{
    json = oldElement != NULL ? oldElement->json : newJson;
    if (oldElement != NULL)
    {
        // update contents of old json (the elements to update are derived from the BasicUI update function)
        String state = newJson["state"].isNull() ? newJson["item"]["state"].as<String>() : newJson["state"].as<String>();
        if (!json["state"].isNull())
        {
            json["state"].set(state);
        }
        json["item"]["state"].set(state);

        json["label"].set(newJson["label"]);

        json["visibility"].set(newJson["visibility"]);
    }

    title = parseWidgetLabel(json["label"].as<String>()); // TODO if empty -> item label?
    icon = json["icon"].as<String>();

    identifier = json["widgetId"].as<String>();

    // TODO store commandDescription, mappings, pattern

    state = getStateString(json);

    String typeString = json["type"];
    if (typeString == "Frame")
    {
        type = M5PanelElementType::Frame;
    }
    else if (typeString == "Selection")
    {
        type = M5PanelElementType::Selection;
        choices = new M5PanelPage(this, json);
        setParentForPageAndSuccessors(choices, this);
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
    setParentForPageAndSuccessors(detail, this);

    if (oldElement != NULL)
    {
        parent = oldElement->parent;
        oldElement->parent = NULL;

        detail = oldElement->detail;
        oldElement->detail = NULL;

        delete oldElement;
    }
}

M5PanelUIElement::M5PanelUIElement(JsonObject json) : M5PanelUIElement(json, NULL) {}

M5PanelUIElement::M5PanelUIElement(M5PanelUIElement *selection, JsonObject json, int i)
{
    JsonArray choices = json["item"]["stateDescription"]["options"];

    String value = choices[i]["value"].as<String>();
    String label = choices[i]["label"].as<String>();

    title = label;
    // TODO icon?
    identifier = selection->identifier + "_choice_" + i;
    type = M5PanelElementType::Choice;
    this->json = json;
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
    canvas->deleteCanvas();
}

void M5PanelUIElement::drawFrame(M5EPD_Canvas *canvas, int elementSize)
{
    int radius = 10;

    int innerRectStart = LINE_THICKNESS;
    int innerRectSize = elementSize - 2 * LINE_THICKNESS;
    int innerRadius = radius - LINE_THICKNESS;
    canvas->fillRoundRect(0, 0, elementSize, elementSize, radius, 15);
    canvas->fillRoundRect(innerRectStart, innerRectStart, innerRectSize, innerRectSize, innerRadius, 0);

    // detail page indicator
    if (detail != NULL)
    {
        int cornerStart = innerRectStart + innerRectSize - 30;
        canvas->fillRoundRect(cornerStart, innerRectStart, 30, 30, innerRadius, 15);
        canvas->fillTriangle(cornerStart, innerRectStart, cornerStart + 30, innerRectStart + 30, cornerStart, innerRectStart + 30, 0);
    }
}

void M5PanelUIElement::drawTitle(M5EPD_Canvas *canvas, int elementSize)
{
    int elementCenter = elementSize / 2;
    int titleY;
    uint8_t alignment;
    switch (type)
    {
    case M5PanelElementType::Choice:
    case M5PanelElementType::Frame:
        // TODO allow multiple lines in Frames
        titleY = elementCenter;
        alignment = MC_DATUM;
        break;
    default:
        titleY = MARGIN;
        alignment = TC_DATUM;
        break;
    }

    canvas->setTextSize(FONT_SIZE_LABEL);
    canvas->setTextDatum(alignment);
    canvas->drawString(title, elementCenter, titleY);
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

    // clear previous status content
    canvas->fillRect(MARGIN, controlY + MARGIN, elementSize - 2 * MARGIN, ELEMENT_CONTROL_HEIGHT - 2 * MARGIN, 0);

    switch (type)
    {
    case M5PanelElementType::Slider:
    case M5PanelElementType::Setpoint:
        // draw +/- symbols
        canvas->setTextSize(FONT_SIZE_STATUS_CENTER);
        canvas->setTextDatum(ML_DATUM);
        canvas->drawString("-", MARGIN, controlYCenter);
        canvas->setTextDatum(MR_DATUM);
        canvas->drawString("+", elementSize - MARGIN, controlYCenter);
        // also draw control area --> no break
    case M5PanelElementType::Selection:
    case M5PanelElementType::Switch:
        // draw divider
        canvas->drawLine(0, controlY, elementSize - LINE_THICKNESS, controlY, LINE_THICKNESS, 15);
        // also draw text --> no break
    case M5PanelElementType::Text:
        // draw status
        canvas->setTextSize(FONT_SIZE_LABEL);
        canvas->setTextDatum(MC_DATUM);
        canvas->drawString(state, elementCenter, controlYCenter);
        break;
    default:
        break;
    }
}

String M5PanelUIElement::forwardTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    if (choices != NULL)
    {
        String newCurrentElement = choices->processTouch(currentElement, x, y, canvas);
        if (newCurrentElement != "")
        {
            return newCurrentElement;
        }
    }

    if (detail != NULL)
    {
        String newCurrentElement = detail->processTouch(currentElement, x, y, canvas);
        if (newCurrentElement != "")
        {
            return newCurrentElement;
        }
    }

    // touch does not concern choices nor detail page of this item
    return "";
}

void postValue(String link, String newState)
{
    HTTPClient httpPost;
    httpPost.setReuse(false);
    httpPost.begin(commandWifiClient, link);
    httpPost.addHeader(F("Content-Type"), F("text/plain"));
    httpPost.POST(newState);
    httpPost.end();
}

void sendChoiceTouch(M5PanelUIElement *touchedElement)
{
    Serial.println("send touch on choice");
    JsonObject json = touchedElement->json;
    JsonArray choices = json["item"]["commandDescription"]["commandOptions"];
    // find command for touched choice
    for (size_t i = 0; i < choices.size(); i++)
    {
        JsonObject choice = choices[i];
        String touchedChoice = touchedElement->title;
        if (choice["label"] == touchedChoice)
        {
            postValue(json["item"]["link"], choice["command"]);
            return;
        }
    }
}

void sendPlusTouch(M5PanelUIElement *touchedElement)
{
    Serial.println("send touch on plus");
}

void sendMinusTouch(M5PanelUIElement *touchedElement)
{
    Serial.println("send touch on minus");
}

void sendSwitchTouch(M5PanelUIElement *touchedElement)
{
    Serial.println("send touch on switch");
    JsonObject json = touchedElement->json;
    // Decide how to process switch: Basic switches (without value mappings) get switched from "on" to "off", with value mappings switch one value further.
    // TODO same for json["item"]["commandDescription"]["commandOptions"]
    String newState;
    if (json["mappings"].isNull())
    {
        newState = touchedElement->state == "ON" ? "OFF" : "ON";
    }
    else
    {
        JsonArray mappings = json["mappings"];
        size_t nextStateIndex;
        // find next state in mapping list
        for (size_t i = 0; i < mappings.size(); i++)
        {
            JsonObject mapping = mappings[i];
            String jsonState = json["state"];
            if (mapping["command"] == jsonState)
            {
                nextStateIndex = (i + 1) % mappings.size();
                break;
            }
        }
        // implicitly uses first state when current state not found
        newState = mappings[nextStateIndex]["command"].as<String>();
    }
    postValue(json["item"]["link"], newState);
}

String M5PanelUIElement::processTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas, int *highlightX, int *highlightY, void (**callback)(M5PanelUIElement *))
{
    // process touch on title / icon or control area for interaction
    Serial.printf("Touched on item %s (title: %s) with coordinates (%d,%d) (relative to element frame)\n", identifier.c_str(), title.c_str(), x, y);

    if (y < ELEMENT_AREA_SIZE - ELEMENT_CONTROL_HEIGHT - MARGIN || type == M5PanelElementType::Frame || type == M5PanelElementType::Choice || type == M5PanelElementType::Text)
    {
        // touched in area for navigation
        if (detail != NULL)
        {
            return navigate(detail, canvas);
        }

        if (type == M5PanelElementType::Choice)
        {
            // TODO send control action to OpenHab
            // navigate back to parent page (parent of parent element of parent page)
            *callback = &sendChoiceTouch;
            return navigate(parent->parent->parent, canvas);
        }
    }
    else
    {
        int elementSize = ELEMENT_AREA_SIZE - 2 * MARGIN;
        // touched in area for state / control
        switch (type)
        {
        case M5PanelElementType::Selection:
            return navigate(choices, canvas);
        case M5PanelElementType::Setpoint:
        case M5PanelElementType::Slider:
            canvas->createCanvas(elementSize / 2, ELEMENT_CONTROL_HEIGHT);
            *highlightY = elementSize - ELEMENT_CONTROL_HEIGHT + MARGIN;
            canvas->fillRect(0, 0, elementSize / 2, ELEMENT_CONTROL_HEIGHT, 8);
            if (x < ELEMENT_AREA_SIZE / 2)
            {
                // touched -
                *callback = &sendMinusTouch;
                *highlightX = MARGIN;
            }
            else
            {
                *callback = &sendPlusTouch;
                *highlightX = elementSize / 2 + MARGIN;
            }
            return "";
        case M5PanelElementType::Switch:
            // touched switch
            *callback = &sendSwitchTouch;
            canvas->createCanvas(elementSize, ELEMENT_CONTROL_HEIGHT);
            *highlightY = elementSize - ELEMENT_CONTROL_HEIGHT + MARGIN;
            *highlightX = MARGIN;
            canvas->fillRect(0, 0, elementSize, ELEMENT_CONTROL_HEIGHT, 8);
            return "";
        default:
            break;
        }
    }

    return "";
}
