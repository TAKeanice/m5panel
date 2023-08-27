#include "M5PanelUIStatusArea.h"
#include "M5PanelUI_LayoutConstants.h"
#include "ImageResource.h"
#include "FontSizes.h"
#include <M5EPD.h>

void M5PanelStatusArea::startLoadingIndicator()
{
    loading = true;
    // TODO start task on appropriate core that displays the loading indicator images in sequence
}

void M5PanelStatusArea::stopLoadingIndicator()
{
    loading = false;
    // TODO loading = false must stop the task that shows loading indicator images
}

void M5PanelStatusArea::showBatteryIndicator(M5EPD_Canvas *canvas)
{
    M5.BatteryADCBegin();

    canvas->createCanvas(150, 40);

    int img_y = 5;
    int img_x = 40;
    int img_height = 32;
    int img_width = 32;

    canvas->pushImage(img_x, img_y, 32, 32, ImageResource_status_bar_battery_32x32);
    uint32_t vol = M5.getBatteryVoltage();

    if (vol < 3300)
    {
        vol = 3300;
    }
    else if (vol > 4350)
    {
        vol = 4350;
    }
    float battery = (float)(vol - 3300) / (float)(4350 - 3300);
    if (battery <= 0.01)
    {
        battery = 0.01;
    }
    if (battery > 1)
    {
        battery = 1;
    }
    uint8_t px = battery * 25;
    char buf[5];
    sprintf(buf, "%d%%", (int)(battery * 100));
    canvas->fillRect(img_x + 3, img_y + 10, px, 13, 15);

    canvas->setTextDatum(ML_DATUM);
    canvas->setTextSize(FONT_SIZE_LABEL_SMALL);
    canvas->drawString(buf, img_x + img_width + 5, img_y + img_height / 2);
    canvas->pushCanvas(0, 500, UPDATE_MODE_GLD16);

    canvas->deleteCanvas();
}

void M5PanelStatusArea::showWifiConnected(M5EPD_Canvas *canvas)
{
    // TODO show wifi connected indicator
}

void M5PanelStatusArea::showWifiDisconnected(M5EPD_Canvas *canvas)
{
    // TODO show wifi disconnected indicator
}