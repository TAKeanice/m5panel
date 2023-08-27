#include "M5PanelUI.h"

// Page update

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

// Element update

boolean M5PanelUIElement::update(JsonObject newJson)
{
    // update contents of old json (the elements to update are derived from the BasicUI update function)
    json["state"].set(newJson["state"]);

    json["item"]["state"].set(newJson["item"]["state"]);

    json["label"].set(newJson["label"]);

    json["visibility"].set(newJson["visibility"]);

    return updateFromCurrentJson();
}