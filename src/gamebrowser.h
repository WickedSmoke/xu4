// gamebrowser.h

#include "gui.h"

struct ModuleInfo {
    StringTable modi;
    uint8_t resPathI;
    uint8_t modFileI;
    uint8_t category;
    uint8_t parent;
};

struct TxfHeader;
struct ScreenState;

class GameBrowser {
public:
    GameBrowser();
    ~GameBrowser();
    bool present();

    enum Widgets {
        WI_LIST,
        WI_OK,
        WI_QUIT,
        WI_CANCEL,
        WI_COUNT
    };

private:
    bool keyPressed(Stage*, int key);

    StringTable modFiles;
    StringTable modFormat;
    std::vector<ModuleInfo> infoList;
    uint16_t sel;
    uint16_t selMusic;          // 0 = none
    GuiArea gbox[ WI_COUNT ];
    void* atree;
    float lineHeight;
    float psizeList;            // List font point size.

    void selectModule(const GuiArea* area, int y);
    void layout();

    static void renderBrowser(ScreenState* ss, void* data);
    static void displayReset(int, void*, void*);

    friend int browser_dispatch(Stage*, const InputEvent*);
};
