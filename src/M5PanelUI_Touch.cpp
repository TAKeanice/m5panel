#include "M5PanelUI.h"
#include "M5PanelUI_LayoutConstants.h"

#include <HTTPClient.h>

// Element touch processing

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

float getStep(JsonObject widgetJson, JsonObject stateDescription)
{
    return !widgetJson["step"].isNull()
               ? widgetJson["step"].as<String>().toFloat()
               : (!stateDescription["step"].isNull()
                      ? stateDescription["step"].as<String>().toFloat()
                      : 10);
}

boolean sendPlusTouch(M5PanelUIElement *touchedElement)
{
    log_d("send touch on plus");
    JsonObject json = touchedElement->json;
    float currentState = json["item"]["state"].as<String>().toFloat();
    JsonObject stateDescription = json["item"]["stateDescription"];
    float maxValue = !json["maxValue"].isNull()
                         ? json["maxValue"].as<String>().toFloat()
                         : (!stateDescription["minimum"].isNull()
                                ? stateDescription["maximum"].as<String>().toFloat()
                                : 100);
    float step = getStep(json, stateDescription);
    float newValue = min(maxValue, currentState + step);
    postValue(json["item"]["link"], String(newValue));
    return maxValue != currentState;
}

boolean sendMinusTouch(M5PanelUIElement *touchedElement)
{
    log_d("send touch on minus");
    JsonObject json = touchedElement->json;
    float currentState = json["item"]["state"].as<String>().toFloat();
    JsonObject stateDescription = json["item"]["stateDescription"];
    float minValue = !json["minValue"].isNull()
                         ? json["minValue"].as<String>().toFloat()
                         : (!stateDescription["minimum"].isNull()
                                ? stateDescription["minimum"].as<String>().toFloat()
                                : 0);
    float step = getStep(json, stateDescription);
    float newValue = max(minValue, currentState - step);
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

// Page touch processing

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
