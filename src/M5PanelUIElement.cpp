#include <Arduino.h>
#include <ArduinoJson.h>
#include "M5PanelUI.h"

// M5PanelUIElement

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

M5PanelUIElement::M5PanelUIElement(M5PanelPage *parent, JsonObject json)
{
    this->json = json;

    this->parent = parent;

    updateFromCurrentJson();

    String typeString = json["type"];
    if (typeString == "Frame")
    {
        type = M5PanelElementType::Frame;
    }
    else if (typeString == "Selection")
    {
        type = M5PanelElementType::Selection;
        choices = new M5PanelPage(json, this);
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

    detail = new M5PanelPage(this, widgets.size() != 0 ? json : linkedPageJson);
}

M5PanelUIElement::M5PanelUIElement(M5PanelPage *parent, M5PanelUIElement *selection, JsonObject json, int i)
{
    this->parent = parent;

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
