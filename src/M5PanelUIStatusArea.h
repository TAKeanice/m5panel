#include <M5EPD_Canvas.h>

class M5PanelStatusArea
{
private:
    bool loading;

public:
    void startLoadingIndicator();
    void stopLoadingIndicator();
    void showBatteryIndicator(M5EPD_Canvas *canvas);
    void showWifiConnected(M5EPD_Canvas *canvas);
    void showWifiDisconnected(M5EPD_Canvas *canvas);
};