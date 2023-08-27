#include "M5PanelUI.h"
#include "M5PanelUI_LayoutConstants.h"
#include "FontSizes.h"
#include <LittleFS.h>

// Graphic settings

#define LINE_THICKNESS 3

// Draw Element

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

// Draw page

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