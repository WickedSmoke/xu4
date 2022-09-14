#include "controller.h"
#include "gpu.h"
#include "gui.h"

struct ModuleInfo {
    StringTable modi;
    uint8_t resPathI;
    uint8_t modFileI;
    uint8_t category;
    uint8_t parent;
};

struct ScreenState;

class GameBrowser : public Controller {
public:
    GameBrowser();

    /* controller functions */
    virtual bool present();
    virtual void conclude();
    virtual bool keyPressed(int key);
    virtual bool inputEvent(const InputEvent*);

    enum Widgets {
        WI_LIST,
        WI_OK,
        WI_QUIT,
        WI_CANCEL,
        WI_COUNT
    };

private:
    StringTable modFiles;
    StringTable modFormat;
    std::vector<ModuleInfo> infoList;
    uint16_t sel;
    uint16_t selMusic;          // 0 = none
    GuiArea gbox[ WI_COUNT ];
    PrimGroup primGroup[2];
    void* atree;
    float lineHeight;
    float psizeList;            // List font point size.

    void selectModule(const GuiArea* area, int y);
    float* layoutList(float*, TxfDrawState*);
    void layout();
    void updateList();

    static void renderBrowser(ScreenState* ss, void* data);
    static void displayReset(int, void*, void*);
};
