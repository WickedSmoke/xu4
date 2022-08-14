/*
 * xu4.h
 */

#include "notify.h"
#include "stage.h"
#include "stage_id.h"
#include "stringTable.h"

enum NotifySender {
    // Sender Id           Message
    SENDER_LOCATION,    // MoveEvent*
    SENDER_PARTY,       // PartyEvent*
    SENDER_AURA,        // Aura*
    SENDER_MENU,        // MenuEvent*
    SENDER_SETTINGS,    // Settings*
    SENDER_DISPLAY      // NULL or ScreenState*
};

enum DrawLayer {
    LAYER_CPU_BLIT,     // For legacy screenImage rendering.
    LAYER_MAP,          // When GPU_RENDER defined.
    LAYER_HUD,          // Borders and status GUI.
    LAYER_TOP_MENU,     // GameBrowser

    LAYER_COUNT
};

class Settings;
class Config;
class ImageMgr;
class Image;
class EventHandler;
struct SaveGame;
class IntroController;
class GameController;

#define FS_SAMPLES  8

struct FrameSleep {
    uint32_t frameInterval;     // Milliseconds between display updates.
    uint32_t realTime;
    int ftime[FS_SAMPLES];      // Msec elapsed for past frames.
    int ftimeSum;
    uint16_t ftimeIndex;
    uint16_t fsleep;            // Msec to sleep - adjusted slowly.
};

struct MainLoop {
    FrameSleep fs;
    uint32_t runTime;
    int gameCycle;
};

struct XU4GameServices {
    StringTable resourcePaths;
    NotifyBus notifyBus;
    MainLoop loop;
    Settings* settings;
    Config* config;
    ImageMgr* imageMgr;
    void* screen;
    void* screenSys;
    void* gpu;
    Image* screenImage;
    EventHandler* eventHandler;
    SaveGame* saveGame;
    IntroController* intro;
    GameController* game;
    const char* errorMessage;
    uint16_t resGroup;
    uint16_t _stage;            // Only used by TileAnimTransform::draw().
    uint16_t gameReset;         // Load another game.
    uint32_t timerInterval;     // Milliseconds between timedEvents ticks.
    uint32_t rclock;            // Global "realtime" clock (milliseconds).
    uint32_t randomFx[17];      // Effects random number generator state.
    bool verbose;
};

extern XU4GameServices xu4;

#define gs_listen(msk,func,user)    notify_listen(&xu4.notifyBus,msk,func,user)
#define gs_unplug(id)               notify_unplug(&xu4.notifyBus,id)
#define gs_emitMessage(sid,data)    notify_emit(&xu4.notifyBus,sid,data);

void mainLoop(MainLoop*);
uint16_t xu4_setResourceGroup(uint16_t group);
void     xu4_freeResourceGroup(uint16_t group);
extern "C" int xu4_random(int upperval);
extern "C" int xu4_randomFx(int upperval);
