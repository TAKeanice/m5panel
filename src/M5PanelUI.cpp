#include "M5PanelUI.h"
#include <ArduinoJson.h>

M5PanelPage::M5PanelPage(JsonObject json) : M5PanelPage(json, 0) {}

M5PanelPage::M5PanelPage(JsonObject json, int pageIndex)
{
    String titleString = json["title"];
    // the root page has title, the subpages labels
    title = titleString != "null" ? titleString : json["label"];
    JsonArray widgets = json["widgets"];
    size_t pageOffset = pageIndex * 6;

    Serial.println("Initialized page: " + title + " (number " + (pageIndex + 1) + ")");

    // initialize elements on page
    for (size_t i = 0; i < min((size_t)6, widgets.size() - pageOffset); i++)
    {
        JsonObject elementJson = widgets[pageOffset + i];
        elements[i] = new M5PanelUIElement(elementJson);
        elements[i]->parent = this;
    }

    // initialize additional pages if necessary
    if (widgets.size() - pageOffset <= 6)
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
    for (size_t i = 0; i < 6; i++)
    {
        delete elements[i];
    }
    delete previous;
    delete next;
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