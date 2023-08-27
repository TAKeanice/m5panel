#include "M5PanelUI.h"

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