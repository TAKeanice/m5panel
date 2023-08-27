#include <Arduino.h>
#include <ArduinoJson.h>
#include "M5PanelUI.h"

// M5PanelPage

M5PanelPage::M5PanelPage(M5PanelUIElement *parent, JsonObject json) : M5PanelPage(parent, json, 0) {}

M5PanelPage::M5PanelPage(M5PanelUIElement *parent, JsonObject json, int pageIndex)
{
    this->parent = parent;

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
        elements[i] = new M5PanelUIElement(this, elementJson);
    }

    // initialize additional pages if necessary
    if (widgets.size() - pageOffset <= MAX_ELEMENTS)
    {
        // no additional pages needed
        return;
    }

    next = new M5PanelPage(parent, json, pageIndex + 1);
    next->previous = this;
}

M5PanelPage::M5PanelPage(JsonObject json, M5PanelUIElement *selection) : M5PanelPage(json, selection, 0) {}

M5PanelPage::M5PanelPage(JsonObject json, M5PanelUIElement *selection, int pageIndex)
{
    parent = selection;

    JsonArray choices = json["item"]["stateDescription"]["options"];

    identifier = selection->identifier + "_choices_" + pageIndex;
    title = selection->title;
    size_t pageOffset = pageIndex * MAX_ELEMENTS;
    numElements = min((size_t)MAX_ELEMENTS, choices.size() - pageOffset);
    // initialize choice elements
    for (size_t i = 0; i < numElements; i++)
    {
        elements[i] = new M5PanelUIElement(this, selection, json, pageOffset + i);
    }

    if (choices.size() - pageOffset <= MAX_ELEMENTS)
    {
        // no additional pages needed
        return;
    }

    next = new M5PanelPage(json, selection, pageIndex + 1);
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
