#include "M5PanelUI.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "defs.h"

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

#define ELEMENT_CONTROL_HEIGHT 70
#define FONT_SIZE_ELEMENT_TITLE 32

#define LINE_THICKNESS 3

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

void setParent(M5PanelUIElement *element, M5PanelPage *parent)
{
    log_d("setting parent of %s (%s) to %s (%s)",
          element->title.c_str(),
          element->identifier.c_str(),
          parent != NULL ? parent->title.c_str() : "NULL",
          parent != NULL ? parent->identifier.c_str() : "NULL");
    element->parent = parent;
}

void setParent(M5PanelPage *page, M5PanelUIElement *parent)
{
    log_d("setting parent of %s (%s) to %s (%s)",
          page->title.c_str(),
          page->identifier.c_str(),
          parent != NULL ? parent->title.c_str() : "NULL",
          parent != NULL ? parent->identifier.c_str() : "NULL");
    page->parent = parent;
}

// M5PanelPage

M5PanelPage::M5PanelPage(JsonObject json) : M5PanelPage(json, 0) {}

M5PanelPage::M5PanelPage(JsonObject json, int pageIndex)
{
    this->pageIndex = pageIndex;

    String widgetId = json["widgetId"].isNull() ? "" : json["widgetId"].as<String>();
    String id = json["id"].isNull() ? "" : json["id"].as<String>();
    identifier = id + widgetId + "_" + pageIndex;

    // the root page has title, the subpages labels
    String label = json["label"].isNull() ? "" : json["label"].as<String>();
    String titleString = json["title"].isNull() ? "" : json["title"].as<String>();
    title = parseWidgetLabel(label + titleString);

    log_d("Initialized page: %s (number %d)", title.c_str(), (pageIndex + 1));

    size_t pageOffset = pageIndex * MAX_ELEMENTS;
    JsonArray widgets = json["widgets"];
    numElements = min((size_t)MAX_ELEMENTS, widgets.size() - pageOffset);

    // initialize elements on page
    for (size_t i = 0; i < numElements; i++)
    {
        JsonObject elementJson = widgets[pageOffset + i];
        elements[i] = new M5PanelUIElement(elementJson);
        setParent(elements[i], this);
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
        setParent(elements[i], this);
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
    log_d("delete page %s (%s)", title.c_str(), identifier.c_str());
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

    return next == NULL ? false : next->draw(pageIdentifier, canvas);
}

void M5PanelPage::draw(M5EPD_Canvas *canvas)
{
    // clear
    M5.EPD.Clear(false);

    drawNavigation(canvas);

    // draw elements
    for (size_t i = 0; i < numElements; i++)
    {
        drawElement(canvas, i, false);
    }

    M5.EPD.UpdateFull(UPDATE_MODE_GLD16);
}

void M5PanelPage::drawElement(M5EPD_Canvas *canvas, int elementIndex, boolean updateImmediately)
{
    int y = MARGIN + (elementIndex / ELEMENT_COLS) * ELEMENT_AREA_SIZE;
    int x = NAV_WIDTH + MARGIN + (elementIndex % ELEMENT_COLS) * ELEMENT_AREA_SIZE;
    elements[elementIndex]->draw(canvas, x, y, ELEMENT_AREA_SIZE);
    if (updateImmediately)
    {
        M5.EPD.UpdateArea(x, y, ELEMENT_AREA_SIZE, ELEMENT_AREA_SIZE, UPDATE_MODE_DU);
    }
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
    canvas->pushCanvas(MARGIN, MARGIN, UPDATE_MODE_NONE);
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

    canvas->pushCanvas(0, NAV_MARGIN_TOP_BOTTOM, UPDATE_MODE_NONE);
    canvas->deleteCanvas();
}

M5PanelPage *navigate(M5PanelPage *navigationTarget, M5EPD_Canvas *canvas)
{
    log_d("Navigating to %s (%s)", navigationTarget->title.c_str(), navigationTarget->identifier.c_str());
    return navigationTarget;
}

M5PanelPage *M5PanelPage::processNavigationTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    log_d("Touched navigation area");
    // touch within navigation area
    int arrowAreaHeight = PANEL_HEIGHT - 2 * NAV_MARGIN_TOP_BOTTOM;
    int singleArrowHeight = arrowAreaHeight / 3;

    if (y > arrowAreaHeight)
    {
        return this; // not touched on arrows
    }
    int arrow = y / singleArrowHeight;
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
        return this;
    }
    else
    {
        // highlight touched arrow
        canvas->createCanvas(NAV_WIDTH - 4 * MARGIN, singleArrowHeight);
        canvas->fillCanvas(15);
        canvas->pushCanvas(2 * MARGIN, NAV_MARGIN_TOP_BOTTOM + singleArrowHeight * arrow, UPDATE_MODE_DU);
        canvas->deleteCanvas();

        return navigate(toDraw, canvas);
    }
}

M5PanelPage *M5PanelPage::processElementTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    int elementColumn = x / ELEMENT_AREA_SIZE;
    int elementRow = y / ELEMENT_AREA_SIZE;
    int elementIndex = elementColumn + (elementRow * ELEMENT_COLS);
    int originX = elementColumn * ELEMENT_AREA_SIZE;
    int originY = elementRow * ELEMENT_AREA_SIZE;
    if (elementIndex < numElements)
    {
        int highlightX, highlightY;
        boolean (*callback)(M5PanelUIElement *) = NULL;
        M5PanelUIElement *element = elements[elementIndex];
        M5PanelPage *navigationTarget = element->processTouch(x - originX, y - originY, canvas, &highlightX, &highlightY, &callback);
        // react to touch graphically to give immediate feedback, since no navigation occurred
        canvas->pushCanvas(highlightX + originX + NAV_WIDTH + MARGIN, highlightY + originY + MARGIN, UPDATE_MODE_DU);
        canvas->deleteCanvas();
        if (callback != NULL)
        {
            boolean changed = callback(element);
            if (!changed)
            {
                // redraw to get rid of highlight
                drawElement(canvas, elementIndex, true);
            }
        }
        if (navigationTarget != NULL)
        {
            return navigate(navigationTarget, canvas);
        }
    }
    return this;
}

M5PanelPage *M5PanelPage::processTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
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
        M5PanelPage *newCurrentElement = NULL;
        if (next != NULL)
        {
            newCurrentElement = next->processTouch(currentElement, x, y, canvas);
        }
        if (newCurrentElement != NULL)
        {
            return newCurrentElement;
        }
        // since none of the following pages could process the touch either, let child pages process it
        for (size_t i = 0; i < numElements; i++)
        {
            newCurrentElement = elements[i]->forwardTouch(currentElement, x, y, canvas);
            if (newCurrentElement != NULL)
            {
                return newCurrentElement;
            }
        }
        return NULL;
    }
}

M5PanelPage *M5PanelPage::updateWidget(JsonObject json, String widgetId, String currentPage, M5EPD_Canvas *canvas)
{
    for (size_t i = 0; i < numElements; i++)
    {
        M5PanelUIElement *element = elements[i];
        if (element->identifier != widgetId)
        {
            continue;
        }
        log_d("found widget to update: %s", widgetId.c_str());
        //  update widget
        boolean updated = elements[i]->update(json);

        if (updated && currentPage == identifier)
        {
            log_d("redraw element %s", element->identifier.c_str());
            //  redraw widget
            drawElement(canvas, i, true);
        }
        // widget to update was found on this page
        return this;
    }

    // search subpages for element to be updated
    for (size_t i = 0; i < numElements; i++)
    {
        M5PanelUIElement *element = elements[i];
        // forward update command
        if (element->detail != NULL)
        {
            log_d("Updating element %s detail", element->identifier.c_str());
            M5PanelPage *foundOnPage = element->detail->updateWidget(json, widgetId, currentPage, canvas);
            if (foundOnPage != NULL)
            {
                return foundOnPage;
            }
        }
    }

    // not found on any of the subpages, search sibling page
    if (next != NULL)
    {
        log_d("Updating next page %s", next->identifier.c_str());
        return next->updateWidget(json, widgetId, currentPage, canvas);
    }

    // not found at all in this branch
    return NULL;
}

void M5PanelPage::updateAllWidgets(DynamicJsonDocument json)
{
    for (size_t i = 0; i < numElements; i++)
    {
        elements[i]->update(json["widgets"][i + pageIndex * MAX_ELEMENTS]);
    }
}

// M5PanelUIElement

void setParentForPageAndSuccessors(M5PanelPage *firstChild, M5PanelUIElement *parent)
{
    M5PanelPage *page = firstChild;
    while (page != NULL)
    {
        setParent(page, parent);
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

M5PanelUIElement::M5PanelUIElement(JsonObject json)
{
    this->json = json;

    updateFromCurrentJson();

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

    log_d("Initialized element: %s  with icon: %s state: %s type: %s", title.c_str(), icon.c_str(), state.c_str(), typeString.c_str());

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
}

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
    log_d("delete element %s (%s)", title.c_str(), identifier.c_str());
    delete detail;
    delete choices;
}

boolean M5PanelUIElement::updateFromCurrentJson()
{
    boolean changed = false;

    String newTitle = parseWidgetLabel(json["label"].as<String>()); // TODO if empty -> item label?
    changed |= newTitle == title;
    title = newTitle;

    String newIcon = json["icon"].as<String>();
    changed |= newIcon == icon;
    icon = newIcon;

    String newIdentifier = json["widgetId"].as<String>();
    changed |= newIdentifier == identifier;
    identifier = newIdentifier;

    String newState = getStateString(json);
    changed |= newState == state;
    state = newState;

    return changed;
}

boolean M5PanelUIElement::update(JsonObject newJson)
{
    // update contents of old json (the elements to update are derived from the BasicUI update function)
    json["state"].set(newJson["state"]);

    json["item"]["state"].set(newJson["item"]["state"]);

    json["label"].set(newJson["label"]);

    json["visibility"].set(newJson["visibility"]);

    return updateFromCurrentJson();
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

    canvas->pushCanvas(x + MARGIN, y + MARGIN, UPDATE_MODE_NONE);
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

    boolean titleCanBeVerticallyCentered = icon == "";

    switch (type)
    {
    case M5PanelElementType::Choice:
    case M5PanelElementType::Frame:
        titleY = titleCanBeVerticallyCentered ? elementCenter : MARGIN * 2;
        alignment = titleCanBeVerticallyCentered ? MC_DATUM : TC_DATUM;
        break;
    default:
        titleY = MARGIN * 2;
        alignment = TC_DATUM;
        break;
    }

    canvas->setTextSize(FONT_SIZE_LABEL);
    canvas->setTextDatum(alignment);
    canvas->drawString(title, elementCenter, titleY);
}

String getLocalIconFile(String icon, String state)
{
    String iconFile = "/icons/" + icon + "-" + state + ".png"; // Try to find dynamic icon ...
    iconFile.toLowerCase();
    if (!LittleFS.exists(iconFile))
    {
        iconFile = "/icons/" + icon + ".png"; // else try to find non dynamic icon
        iconFile.toLowerCase();
        if (!LittleFS.exists(iconFile))
        {
            iconFile = "";
        }
    }
    return iconFile;
}

void M5PanelUIElement::drawIcon(M5EPD_Canvas *canvas, int size)
{
    if (icon == "")
    {
        return;
    }

    int iconSize = 96;
    int yOffset = iconSize / 2;
    String iconFile = getLocalIconFile(icon, state);
    if (iconFile != "")
    {
        canvas->drawPngFile(LittleFS, iconFile.c_str(), size / 2 - iconSize / 2, size / 2 - yOffset, 0, 0, 0, 0, 1);
    }
    else
    {
        icon = ""; // draw as if there was no icon defined
    }
}

void M5PanelUIElement::drawStatusAndControlArea(M5EPD_Canvas *canvas, int elementSize)
{
    boolean statusVerticallyCentered = icon == "" && type != M5PanelElementType::Choice && type != M5PanelElementType::Frame;

    int elementCenter = elementSize / 2;
    int controlY = elementSize - ELEMENT_CONTROL_HEIGHT;
    int controlYCenter = elementSize - ELEMENT_CONTROL_HEIGHT / 2;

    int valueYCenter = statusVerticallyCentered ? elementSize / 2 : controlYCenter;

    // clear previous status content
    canvas->fillRect(MARGIN, valueYCenter - ELEMENT_CONTROL_HEIGHT / 2 + MARGIN, elementSize - 2 * MARGIN, ELEMENT_CONTROL_HEIGHT - 2 * MARGIN, 0);

    // control symbols
    switch (type)
    {
    case M5PanelElementType::Slider:
    case M5PanelElementType::Setpoint:
        // draw +/- symbols
        canvas->setTextSize(FONT_SIZE_CONTROL);
        canvas->setTextDatum(ML_DATUM);
        canvas->drawString("-", MARGIN, controlYCenter);
        canvas->setTextDatum(MR_DATUM);
        canvas->drawString("+", elementSize - MARGIN, controlYCenter);
        break;
    case M5PanelElementType::Selection:
        // draw dots to indicate selection
        canvas->fillCircle(MARGIN + 3, controlYCenter - MARGIN, 3, 15);
        canvas->fillCircle(MARGIN + 3, controlYCenter + MARGIN, 3, 15);
        canvas->fillCircle(elementSize - MARGIN - 3, controlYCenter - MARGIN, 3, 15);
        canvas->fillCircle(elementSize - MARGIN - 3, controlYCenter + MARGIN, 3, 15);
        break;
    case M5PanelElementType::Switch:
        // draw divider to indicate switch
        canvas->drawLine(0, controlY, elementSize - LINE_THICKNESS, controlY, LINE_THICKNESS, 15);
        break;
    default:
        break;
    }

    // status

    switch (type)
    {
    case M5PanelElementType::Slider:
    case M5PanelElementType::Setpoint:
    case M5PanelElementType::Selection:
    case M5PanelElementType::Switch:
    case M5PanelElementType::Text:
        // draw status
        canvas->setTextSize(FONT_SIZE_LABEL);
        canvas->setTextDatum(MC_DATUM);
        canvas->drawString(state, elementCenter, valueYCenter);
        break;
    default:
        break;
    }
}

M5PanelPage *M5PanelUIElement::forwardTouch(String currentElement, uint16_t x, uint16_t y, M5EPD_Canvas *canvas)
{
    if (choices != NULL)
    {
        M5PanelPage *newCurrentElement = choices->processTouch(currentElement, x, y, canvas);
        if (newCurrentElement != NULL)
        {
            return newCurrentElement;
        }
    }

    if (detail != NULL)
    {
        M5PanelPage *newCurrentElement = detail->processTouch(currentElement, x, y, canvas);
        if (newCurrentElement != NULL)
        {
            return newCurrentElement;
        }
    }

    // touch does not concern choices nor detail page of this item
    return NULL;
}

void postValue(String link, String newState)
{
    log_d("Sending value %s", newState.c_str());
    WiFiClient commandWifiClient;
    HTTPClient httpPost;
    httpPost.begin(commandWifiClient, link);
    httpPost.addHeader(F("Content-Type"), F("text/plain"));
    httpPost.POST(newState);
    httpPost.end();
}

boolean sendChoiceTouch(M5PanelUIElement *touchedElement)
{
    log_d("send touch on choice");
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
            return true;
        }
    }
    return true;
}

int getStep(JsonObject widgetJson, JsonObject stateDescription)
{
    return !widgetJson["step"].isNull()
               ? widgetJson["step"].as<String>().toInt()
               : (!stateDescription["step"].isNull()
                      ? stateDescription["step"].as<String>().toInt()
                      : 10);
}

boolean sendPlusTouch(M5PanelUIElement *touchedElement)
{
    log_d("send touch on plus");
    JsonObject json = touchedElement->json;
    int currentState = json["item"]["state"].as<String>().toInt();
    JsonObject stateDescription = json["item"]["stateDescription"];
    int maxValue = !json["maxValue"].isNull()
                       ? json["maxValue"].as<String>().toInt()
                       : (!stateDescription["minimum"].isNull()
                              ? stateDescription["maximum"].as<String>().toInt()
                              : 100);
    int step = getStep(json, stateDescription);
    int newValue = min(maxValue, currentState + step);
    postValue(json["item"]["link"], String(newValue));
    return maxValue != currentState;
}

boolean sendMinusTouch(M5PanelUIElement *touchedElement)
{
    log_d("send touch on minus");
    JsonObject json = touchedElement->json;
    int currentState = json["item"]["state"].as<String>().toInt();
    JsonObject stateDescription = json["item"]["stateDescription"];
    int minValue = !json["minValue"].isNull()
                       ? json["minValue"].as<String>().toInt()
                       : (!stateDescription["minimum"].isNull()
                              ? stateDescription["minimum"].as<String>().toInt()
                              : 0);
    int step = getStep(json, stateDescription);
    int newValue = max(minValue, currentState - step);
    postValue(json["item"]["link"], String(newValue));
    return minValue != currentState;
}

boolean sendSwitchTouch(M5PanelUIElement *touchedElement)
{
    log_d("send touch on switch");
    JsonObject json = touchedElement->json;
    // Decide how to process switch: Basic switches (without value mappings) get switched from "on" to "off", with value mappings switch one value further.
    // TODO same for json["item"]["commandDescription"]["commandOptions"]
    String newState;
    JsonArray mappings = json["mappings"];
    if (mappings.isNull() || mappings.size() == 0)
    {
        mappings = json["item"]["commandDescription"]["commandOptions"];
    }

    if (mappings.isNull() || mappings.size() == 0)
    {
        newState = touchedElement->state == "ON" ? "OFF" : "ON";
    }
    else
    {
        size_t nextStateIndex;
        String itemState = json["item"]["state"];
        // find next state in mapping list
        for (size_t i = 0; i < mappings.size(); i++)
        {
            JsonObject mapping = mappings[i];
            if (mapping["command"] == itemState)
            {
                nextStateIndex = (i + 1) % mappings.size();
                break;
            }
        }
        // implicitly uses first state when current state not found
        newState = mappings[nextStateIndex]["command"].as<String>();
        log_d("Current state: %s, new state: %s", itemState, newState);
    }
    postValue(json["item"]["link"], newState);
    return true;
}

M5PanelPage *M5PanelUIElement::processTouch(uint16_t x, uint16_t y, M5EPD_Canvas *canvas, int *highlightX, int *highlightY, boolean (**callback)(M5PanelUIElement *))
{
    // process touch on title / icon or control area for interaction
    log_d("Touched on item %s (title: %s) with coordinates (%d,%d) (relative to element frame)", identifier.c_str(), title.c_str(), x, y);

    M5PanelPage *navigationTarget = NULL;

    int elementSize = ELEMENT_AREA_SIZE - 2 * MARGIN;

    if (type == M5PanelElementType::Frame || type == M5PanelElementType::Choice || type == M5PanelElementType::Text)
    {
        // touched in area for navigation
        if (detail != NULL)
        {
            navigationTarget = detail;
        }
        else if (type == M5PanelElementType::Choice)
        {
            // navigate back to parent page (parent of parent element of parent page)
            *callback = &sendChoiceTouch;
            navigationTarget = parent->parent->parent;
        }
        if (navigationTarget != NULL)
        {
            canvas->createCanvas(elementSize, elementSize);
            canvas->fillRect(0, 0, elementSize, elementSize, 15);
        }
        else
        {
            // we have to create a canvas to be drawn by the caller
            canvas->createCanvas(1, 1);
        }
        *highlightX = MARGIN;
        *highlightY = MARGIN;
    }
    else
    {
        // touched in area for state / control
        switch (type)
        {
        case M5PanelElementType::Selection:
            canvas->createCanvas(elementSize, ELEMENT_CONTROL_HEIGHT);
            *highlightY = elementSize - ELEMENT_CONTROL_HEIGHT + MARGIN;
            *highlightX = MARGIN;
            canvas->fillRect(0, 0, elementSize, ELEMENT_CONTROL_HEIGHT, 15);
            navigationTarget = choices;
            break;
        case M5PanelElementType::Setpoint:
        case M5PanelElementType::Slider:
            canvas->createCanvas(elementSize / 2, ELEMENT_CONTROL_HEIGHT);
            *highlightY = elementSize - ELEMENT_CONTROL_HEIGHT + MARGIN;
            canvas->fillRect(0, 0, elementSize / 2, ELEMENT_CONTROL_HEIGHT, 15);
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
            break;
        case M5PanelElementType::Switch:
            // touched switch
            *callback = &sendSwitchTouch;
            canvas->createCanvas(elementSize, ELEMENT_CONTROL_HEIGHT);
            *highlightY = elementSize - ELEMENT_CONTROL_HEIGHT + MARGIN;
            *highlightX = MARGIN;
            canvas->fillRect(0, 0, elementSize, ELEMENT_CONTROL_HEIGHT, 15);
            break;
        default:
            break;
        }
    }

    return navigationTarget;
}
