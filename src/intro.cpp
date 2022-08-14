/*
 * $Id$
 */


#include <algorithm>
#include <cassert>
#include <cstring>
#include "u4.h"

#include "intro.h"

#include "debug.h"
#include "error.h"
#include "imagemgr.h"
#include "sound.h"
#include "party.h"
#include "screen.h"
#include "settings.h"
#include "tileset.h"
#include "u4file.h"
#include "utils.h"
#include "xu4.h"

#ifdef GPU_RENDER
extern bool loadMapData(Map *map, U4FILE *uf, Symbol borderTile);
#endif
#include "gpu.h"

#ifdef IOS
#include "ios_helpers.h"
#endif

extern uint32_t getTicks();

using namespace std;

#define INTRO_MAP_HEIGHT 5
#define INTRO_MAP_WIDTH 19
#define INTRO_TEXT_X 0
#define INTRO_TEXT_Y 19
#define INTRO_TEXT_WIDTH 40
#define INTRO_TEXT_HEIGHT 6

#define GYP_PLACES_FIRST 0
#define GYP_PLACES_TWOMORE 1
#define GYP_PLACES_LAST 2
#define GYP_UPON_TABLE 3
#define GYP_SEGUE1 13
#define GYP_SEGUE2 14

#define INTRO_CON       IntroController* ic = xu4.intro
#define INTRO_CON_ST    IntroController* ic = (IntroController*) st->data
#define INTRO_RES       (STAGE_INTRO+1)

#ifndef GPU_RENDER
class IntroObjectState {
public:
    IntroObjectState() : x(0), y(0), tile(0) {}
    int x, y;
    MapTile tile; /* base tile + tile frame */
};
#endif

const int IntroBinData::INTRO_TEXT_OFFSET = 17445 - 1;  // (start at zero)
const int IntroBinData::INTRO_MAP_OFFSET = 30339;
const int IntroBinData::INTRO_FIXUPDATA_OFFSET = 29806;
const int IntroBinData::INTRO_SCRIPT_TABLE_SIZE = 548;
const int IntroBinData::INTRO_SCRIPT_TABLE_OFFSET = 30434;
const int IntroBinData::INTRO_BASETILE_TABLE_SIZE = 15;
const int IntroBinData::INTRO_BASETILE_TABLE_OFFSET = 16584;
const int IntroBinData::BEASTIE1_FRAMES = 0x80;
const int IntroBinData::BEASTIE2_FRAMES = 0x40;
const int IntroBinData::BEASTIE_FRAME_TABLE_OFFSET = 0x7380;
const int IntroBinData::BEASTIE1_FRAMES_OFFSET = 0;
const int IntroBinData::BEASTIE2_FRAMES_OFFSET = 0x78;

IntroBinData::IntroBinData() :
    sigData(NULL),
    scriptTable(NULL),
    baseTileTable(NULL),
    beastie1FrameTable(NULL),
    beastie2FrameTable(NULL) {
}

IntroBinData::~IntroBinData() {
    delete [] sigData;
    delete [] scriptTable;
    delete [] baseTileTable;
    delete [] beastie1FrameTable;
    delete [] beastie2FrameTable;

    introQuestions.clear();
    introText.clear();
    introGypsy.clear();
}

bool IntroBinData::load() {
    int i;
    const UltimaSaveIds* usaveIds = xu4.config->usaveIds();
    const Tileset* tileset = xu4.config->tileset();

    U4FILE *title = u4fopen("title.exe");
    if (!title)
        return false;

    introQuestions = u4read_stringtable(title, INTRO_TEXT_OFFSET, 28);
    introText = u4read_stringtable(title, -1, 24);
    introGypsy = u4read_stringtable(title, -1, 15);

    /* clean up stray newlines at end of strings */
    for (i = 0; i < 15; i++) {
        string& str = introGypsy[i];
        if (! str.empty() && str.back() == '\n')
           str.pop_back();
    }

    if (sigData)
        delete sigData;
    sigData = new unsigned char[533];
    u4fseek(title, INTRO_FIXUPDATA_OFFSET, SEEK_SET);
    u4fread(sigData, 1, 533, title);

    u4fseek(title, INTRO_MAP_OFFSET, SEEK_SET);
#ifdef GPU_RENDER
    introMap.id     = 255;      // Unique Id required by screenUpdateMap.
    introMap.border_behavior = Map::BORDER_WRAP;
    introMap.width  = INTRO_MAP_WIDTH;
    introMap.height = INTRO_MAP_HEIGHT;
    introMap.flags  = NO_LINE_OF_SIGHT;
    introMap.tileset = xu4.config->tileset();
    loadMapData(&introMap, title, 0);
#else
    introMap.resize(INTRO_MAP_WIDTH * INTRO_MAP_HEIGHT, MapTile(0));
    for (i = 0; i < INTRO_MAP_HEIGHT * INTRO_MAP_WIDTH; i++)
        introMap[i] = usaveIds->moduleId(u4fgetc(title));
#endif

    u4fseek(title, INTRO_SCRIPT_TABLE_OFFSET, SEEK_SET);
    scriptTable = new unsigned char[INTRO_SCRIPT_TABLE_SIZE];
    u4fread(scriptTable, 1, INTRO_SCRIPT_TABLE_SIZE, title);

    u4fseek(title, INTRO_BASETILE_TABLE_OFFSET, SEEK_SET);
    baseTileTable = new const Tile*[INTRO_BASETILE_TABLE_SIZE];
    for (i = 0; i < INTRO_BASETILE_TABLE_SIZE; i++) {
        MapTile tile = usaveIds->moduleId(u4fgetc(title));
        baseTileTable[i] = tileset->get(tile.id);
    }

    /* --------------------------
       load beastie frame table 1
       -------------------------- */
    beastie1FrameTable = new unsigned char[BEASTIE1_FRAMES];
    u4fseek(title, BEASTIE_FRAME_TABLE_OFFSET + BEASTIE1_FRAMES_OFFSET, SEEK_SET);
    for (i = 0; i < BEASTIE1_FRAMES; i++) {
        beastie1FrameTable[i] = u4fgetc(title);
    }

    /* --------------------------
       load beastie frame table 2
       -------------------------- */
    beastie2FrameTable = new unsigned char[BEASTIE2_FRAMES];
    u4fseek(title, BEASTIE_FRAME_TABLE_OFFSET + BEASTIE2_FRAMES_OFFSET, SEEK_SET);
    for (i = 0; i < BEASTIE2_FRAMES; i++) {
        beastie2FrameTable[i] = u4fgetc(title);
    }

    u4fclose(title);

    return true;
}

//----------------------------------------------------------------------------

struct SettingsMenus {
    static void menusNotice(int, void*, void*);

    SettingsMenus();

    void showMenu(Menu *menu);
    void drawMenu();
    void dispatchMenu(const MenuEvent* event);
    void saveSettings();
    void updateConfMenu(int);
    void updateVideoMenu(int);
    void updateSoundMenu(int);

    enum MenuConstants {
        MI_CONF_VIDEO,
        MI_CONF_SOUND,
        MI_CONF_INPUT,
        MI_CONF_SPEED,
        MI_CONF_GAMEPLAY,
        MI_CONF_INTERFACE,
        MI_CONF_01,
        MI_VIDEO_CONF_GFX,
        MI_VIDEO_02,
        MI_VIDEO_03,
        MI_VIDEO_04,
        MI_VIDEO_05,
        MI_VIDEO_06,
        MI_VIDEO_07,
        MI_VIDEO_08,
        MI_GFX_TILE_TRANSPARENCY,
        MI_GFX_TILE_TRANSPARENCY_SHADOW_SIZE,
        MI_GFX_TILE_TRANSPARENCY_SHADOW_OPACITY,
        MI_GFX_RETURN,
        MI_SOUND_01,
        MI_SOUND_02,
        MI_SOUND_03,
        MI_INPUT_01,
        MI_INPUT_02,
        MI_INPUT_03,
        MI_SPEED_01,
        MI_SPEED_02,
        MI_SPEED_03,
        MI_SPEED_04,
        MI_SPEED_05,
        MI_SPEED_06,
        MI_SPEED_07,
        MI_GAMEPLAY_01,
        MI_GAMEPLAY_02,
        MI_GAMEPLAY_03,
        MI_GAMEPLAY_04,
        MI_GAMEPLAY_05,
        MI_GAMEPLAY_06,
        MI_INTERFACE_01,
        MI_INTERFACE_02,
        MI_INTERFACE_03,
        MI_INTERFACE_04,
        MI_INTERFACE_05,
        MI_INTERFACE_06,
        USE_SETTINGS = 0xFE,
        CANCEL = 0xFF
    };

    Menu mainMenu;
    Menu confMenu;
    Menu videoMenu;
    Menu gfxMenu;
    Menu soundMenu;
    Menu inputMenu;
    Menu speedMenu;
    Menu gameplayMenu;
    Menu interfaceMenu;

    Menu* active;
    TextView extendedMenuArea;
    int listenerId;

    /* temporary place-holder for settings changes */
    SettingsData settingsChanged;
};

SettingsMenus::SettingsMenus() :
    extendedMenuArea(2 * CHAR_WIDTH, 10 * CHAR_HEIGHT, 36, 13)
{
    active = &confMenu;

    confMenu.setTitle("XU4 Configuration:", 0, 0);
    confMenu.add(MI_CONF_VIDEO,               "\010 Video Options",              2,  2,/*'v'*/  2);
    confMenu.add(MI_CONF_SOUND,               "\010 Sound Options",              2,  3,/*'s'*/  2);
    confMenu.add(MI_CONF_INPUT,               "\010 Input Options",              2,  4,/*'i'*/  2);
    confMenu.add(MI_CONF_SPEED,               "\010 Speed Options",              2,  5,/*'p'*/  3);
    confMenu.add(MI_CONF_01, new BoolMenuItem("Game Enhancements         %s",    2,  7,/*'e'*/  5, &settingsChanged.enhancements));
    confMenu.add(MI_CONF_GAMEPLAY,            "\010 Enhanced Gameplay Options",  2,  9,/*'g'*/ 11);
    confMenu.add(MI_CONF_INTERFACE,           "\010 Enhanced Interface Options", 2, 10,/*'n'*/ 12);
    confMenu.add(CANCEL,                      "\017 Main Menu",                  2, 12,/*'m'*/  2);
    confMenu.addShortcutKey(CANCEL, ' ');
    confMenu.setClosesMenu(CANCEL);

    /* set the default visibility of the two enhancement menus */
    confMenu.itemOfId(MI_CONF_GAMEPLAY)->setVisible(xu4.settings->enhancements);
    confMenu.itemOfId(MI_CONF_INTERFACE)->setVisible(xu4.settings->enhancements);

    videoMenu.setTitle("Video Options:", 0, 0);
    videoMenu.add(MI_VIDEO_CONF_GFX,              "\010 Game Graphics Options",  2,  2,/*'g'*/  2);
    videoMenu.add(MI_VIDEO_04,    new IntMenuItem("Scale                x%d", 2,  4,/*'s'*/  0, reinterpret_cast<int *>(&settingsChanged.scale), 1, 5, 1));
    videoMenu.add(MI_VIDEO_05,  (new BoolMenuItem("Mode                 %s",  2,  5,/*'m'*/  0, &settingsChanged.fullscreen))->setValueStrings("Fullscreen", "Window"));
    videoMenu.add(MI_VIDEO_06,   new EnumMenuItem("Filter               %s",  2,  6,/*'f'*/  0, &settingsChanged.filter, screenGetFilterNames()));
    videoMenu.add(MI_VIDEO_08,    new IntMenuItem("Gamma                %s",  2,  7,/*'a'*/  1, &settingsChanged.gamma, 50, 150, 10, MENU_OUTPUT_GAMMA));
    videoMenu.add(USE_SETTINGS,                   "\010 Use These Settings",  2, 11,/*'u'*/  2);
    videoMenu.add(CANCEL,                         "\010 Cancel",              2, 12,/*'c'*/  2);
    videoMenu.addShortcutKey(CANCEL, ' ');
    videoMenu.setClosesMenu(USE_SETTINGS);
    videoMenu.setClosesMenu(CANCEL);

    gfxMenu.setTitle("Game Graphics Options", 0,0);
    gfxMenu.add(MI_GFX_TILE_TRANSPARENCY,               new BoolMenuItem("Transparency Hack  %s", 2,  2,/*'t'*/ 0, &settingsChanged.enhancementsOptions.u4TileTransparencyHack));
    gfxMenu.add(MI_GFX_TILE_TRANSPARENCY_SHADOW_SIZE,    new IntMenuItem("  Shadow Size:     %d", 2,  3,/*'s'*/ 9, &settingsChanged.enhancementsOptions.u4TrileTransparencyHackShadowBreadth, 0, 16, 1));
    gfxMenu.add(MI_GFX_TILE_TRANSPARENCY_SHADOW_OPACITY, new IntMenuItem("  Shadow Opacity:  %d", 2,  4,/*'o'*/ 9, &settingsChanged.enhancementsOptions.u4TileTransparencyHackPixelShadowOpacity, 8, 256, 8));
    gfxMenu.add(MI_VIDEO_02,                          new StringMenuItem("Gem Layout         %s", 2,  6,/*'e'*/ 1, &settingsChanged.gemLayout, screenGetGemLayoutNames()));
    gfxMenu.add(MI_VIDEO_03,                            new EnumMenuItem("Line Of Sight      %s", 2,  7,/*'l'*/ 0, &settingsChanged.lineOfSight, screenGetLineOfSightStyles()));
    gfxMenu.add(MI_VIDEO_07,                            new BoolMenuItem("Screen Shaking     %s", 2,  8,/*'k'*/ 8, &settingsChanged.screenShakes));
    gfxMenu.add(MI_GFX_RETURN,               "\010 Return to Video Options",              2,  12,/*'r'*/  2);
    gfxMenu.setClosesMenu(MI_GFX_RETURN);


    soundMenu.setTitle("Sound Options:", 0, 0);
    soundMenu.add(MI_SOUND_01,  new IntMenuItem("Music Volume         %s", 2,  2,/*'m'*/  0, &settingsChanged.musicVol, 0, MAX_VOLUME, 1, MENU_OUTPUT_VOLUME));
    soundMenu.add(MI_SOUND_02,  new IntMenuItem("Sound Effect Volume  %s", 2,  3,/*'s'*/  0, &settingsChanged.soundVol, 0, MAX_VOLUME, 1, MENU_OUTPUT_VOLUME));
    soundMenu.add(MI_SOUND_03, new BoolMenuItem("Fading               %s", 2,  4,/*'f'*/  0, &settingsChanged.volumeFades));
    soundMenu.add(USE_SETTINGS,                 "\010 Use These Settings", 2, 11,/*'u'*/  2);
    soundMenu.add(CANCEL,                       "\010 Cancel",             2, 12,/*'c'*/  2);
    soundMenu.addShortcutKey(CANCEL, ' ');
    soundMenu.setClosesMenu(USE_SETTINGS);
    soundMenu.setClosesMenu(CANCEL);

    inputMenu.setTitle("Keyboard Options:", 0, 0);
    inputMenu.add(MI_INPUT_01,  new IntMenuItem("Repeat Delay        %4d msec", 2,  2,/*'d'*/  7, &settingsChanged.keydelay, 100, MAX_KEY_DELAY, 100));
    inputMenu.add(MI_INPUT_02,  new IntMenuItem("Repeat Interval     %4d msec", 2,  3,/*'i'*/  7, &settingsChanged.keyinterval, 10, MAX_KEY_INTERVAL, 10));
    /* "Mouse Options:" is drawn in dispatchMenu() */
    inputMenu.add(MI_INPUT_03, new BoolMenuItem("Mouse                %s",      2,  7,/*'m'*/  0, &settingsChanged.mouseOptions.enabled));
    inputMenu.add(USE_SETTINGS,                 "\010 Use These Settings",      2, 11,/*'u'*/  2);
    inputMenu.add(CANCEL,                       "\010 Cancel",                  2, 12,/*'c'*/  2);
    inputMenu.addShortcutKey(CANCEL, ' ');
    inputMenu.setClosesMenu(USE_SETTINGS);
    inputMenu.setClosesMenu(CANCEL);

    speedMenu.setTitle("Speed Options:", 0, 0);
    speedMenu.add(MI_SPEED_01, new IntMenuItem("Game Cycles per Second    %3d",      2,  2,/*'g'*/  0, &settingsChanged.gameCyclesPerSecond, 1, MAX_CYCLES_PER_SECOND, 1));
    speedMenu.add(MI_SPEED_02, new IntMenuItem("Battle Speed              %3d",      2,  3,/*'b'*/  0, &settingsChanged.battleSpeed, 1, MAX_BATTLE_SPEED, 1));
    speedMenu.add(MI_SPEED_03, new IntMenuItem("Spell Effect Length       %s",       2,  4,/*'p'*/  1, &settingsChanged.spellEffectSpeed, 1, MAX_SPELL_EFFECT_SPEED, 1, MENU_OUTPUT_SPELL));
    speedMenu.add(MI_SPEED_04, new IntMenuItem("Camping Length            %3d sec",  2,  5,/*'m'*/  2, &settingsChanged.campTime, 1, MAX_CAMP_TIME, 1));
    speedMenu.add(MI_SPEED_05, new IntMenuItem("Inn Rest Length           %3d sec",  2,  6,/*'i'*/  0, &settingsChanged.innTime, 1, MAX_INN_TIME, 1));
    speedMenu.add(MI_SPEED_06, new IntMenuItem("Shrine Meditation Length  %3d sec",  2,  7,/*'s'*/  0, &settingsChanged.shrineTime, 1, MAX_SHRINE_TIME, 1));
    speedMenu.add(MI_SPEED_07, new IntMenuItem("Screen Shake Interval     %3d msec", 2,  8,/*'r'*/  2, &settingsChanged.shakeInterval, MIN_SHAKE_INTERVAL, MAX_SHAKE_INTERVAL, 10));
    speedMenu.add(USE_SETTINGS,                "\010 Use These Settings",            2, 11,/*'u'*/  2);
    speedMenu.add(CANCEL,                      "\010 Cancel",                        2, 12,/*'c'*/  2);
    speedMenu.addShortcutKey(CANCEL, ' ');
    speedMenu.setClosesMenu(USE_SETTINGS);
    speedMenu.setClosesMenu(CANCEL);

    /* move the BATTLE DIFFICULTY, DEBUG, and AUTOMATIC ACTIONS settings to "enhancementsOptions" */
    gameplayMenu.setTitle                              ("Enhanced Gameplay Options:", 0, 0);
    gameplayMenu.add(MI_GAMEPLAY_01,   new EnumMenuItem("Battle Difficulty          %s", 2,  2,/*'b'*/  0, &settingsChanged.battleDiff, Settings::battleDiffStrings()));
    gameplayMenu.add(MI_GAMEPLAY_02,   new BoolMenuItem("Fixed Chest Traps          %s", 2,  3,/*'t'*/ 12, &settingsChanged.enhancementsOptions.c64chestTraps));
    gameplayMenu.add(MI_GAMEPLAY_03,   new BoolMenuItem("Gazer Spawns Insects       %s", 2,  4,/*'g'*/  0, &settingsChanged.enhancementsOptions.gazerSpawnsInsects));
    gameplayMenu.add(MI_GAMEPLAY_04,   new BoolMenuItem("Gem View Shows Objects     %s", 2,  5,/*'e'*/  1, &settingsChanged.enhancementsOptions.peerShowsObjects));
    gameplayMenu.add(MI_GAMEPLAY_05,   new BoolMenuItem("Slime Divides              %s", 2,  6,/*'s'*/  0, &settingsChanged.enhancementsOptions.slimeDivides));
    gameplayMenu.add(MI_GAMEPLAY_06,   new BoolMenuItem("Debug Mode (Cheats)        %s", 2,  8,/*'d'*/  0, &settingsChanged.debug));
    gameplayMenu.add(USE_SETTINGS,                      "\010 Use These Settings",       2, 11,/*'u'*/  2);
    gameplayMenu.add(CANCEL,                            "\010 Cancel",                   2, 12,/*'c'*/  2);
    gameplayMenu.addShortcutKey(CANCEL, ' ');
    gameplayMenu.setClosesMenu(USE_SETTINGS);
    gameplayMenu.setClosesMenu(CANCEL);

    interfaceMenu.setTitle("Enhanced Interface Options:", 0, 0);
    interfaceMenu.add(MI_INTERFACE_01, new BoolMenuItem("Automatic Actions          %s", 2,  2,/*'a'*/  0, &settingsChanged.shortcutCommands));
    /* "(Open, Jimmy, etc.)" */
    interfaceMenu.add(MI_INTERFACE_02, new BoolMenuItem("Set Active Player          %s", 2,  4,/*'p'*/ 11, &settingsChanged.enhancementsOptions.activePlayer));
    interfaceMenu.add(MI_INTERFACE_03, new BoolMenuItem("Smart 'Enter' Key          %s", 2,  5,/*'e'*/  7, &settingsChanged.enhancementsOptions.smartEnterKey));
    interfaceMenu.add(MI_INTERFACE_04, new BoolMenuItem("Text Colorization          %s", 2,  6,/*'t'*/  0, &settingsChanged.enhancementsOptions.textColorization));
    interfaceMenu.add(MI_INTERFACE_05, new BoolMenuItem("Ultima V Shrines           %s", 2,  7,/*'s'*/  9, &settingsChanged.enhancementsOptions.u5shrines));
    interfaceMenu.add(MI_INTERFACE_06, new BoolMenuItem("Ultima V Spell Mixing      %s", 2,  8,/*'m'*/ 15, &settingsChanged.enhancementsOptions.u5spellMixing));
    interfaceMenu.add(USE_SETTINGS,                     "\010 Use These Settings",       2, 11,/*'u'*/  2);
    interfaceMenu.add(CANCEL,                           "\010 Cancel",                   2, 12,/*'c'*/  2);
    interfaceMenu.addShortcutKey(CANCEL, ' ');
    interfaceMenu.setClosesMenu(USE_SETTINGS);
    interfaceMenu.setClosesMenu(CANCEL);
}

/*
 * Set the active menu.
 */
void SettingsMenus::showMenu(Menu *menu)
{
    active = menu;
    menu->reset();
}

void SettingsMenus::drawMenu()
{
    // draw the extended background for all option screens
    // beasties are always visible on the menus
    INTRO_CON;
    ic->backgroundArea.draw(BKGD_OPTIONS_TOP, 0, 0);
    ic->backgroundArea.draw(BKGD_OPTIONS_BTM, 0, 120);
    ic->drawBeasties();

    active->show(&extendedMenuArea);

    // after drawing the menu, extra menu text can be added here
    if (active == &inputMenu)
        extendedMenuArea.textAt(0, 5, "Mouse Options:");
    else if (active == &interfaceMenu)
        extendedMenuArea.textAt(2, 3, "  (Open, Jimmy, etc.)");

    screenUploadToGPU();
}

/*
 * Update the screen when an observed menu is reset or has an item
 * activated.
 */
void SettingsMenus::menusNotice(int sender, void* eventData, void* user) {
    ((SettingsMenus*) user)->dispatchMenu((MenuEvent*) eventData);
}

void SettingsMenus::saveSettings() {
    xu4.settings->setData(settingsChanged);
    xu4.settings->write();
}

void SettingsMenus::dispatchMenu(const MenuEvent* event)
{
    const Menu* menu = event->menu;

    //printf("KR menu %d\n", event->type);

    if (event->type == MenuEvent::ACTIVATE ||
        event->type == MenuEvent::INCREMENT ||
        event->type == MenuEvent::DECREMENT)
    {
        int itemId = event->item->getId();

        if (menu == &confMenu) {
            updateConfMenu(itemId);
        }
        else if (menu == &videoMenu) {
            updateVideoMenu(itemId);
        }
        else if (menu == &gfxMenu) {
            if(itemId == MI_GFX_RETURN)
                showMenu(&videoMenu);
        }
        else if (menu == &soundMenu) {
            updateSoundMenu(itemId);
        }
        else if (menu == &inputMenu) {
            if (itemId == USE_SETTINGS) {
                saveSettings();
                screenShowMouseCursor(xu4.settings->mouseOptions.enabled);
            }
        }
        else if (menu == &speedMenu) {
            if (itemId == USE_SETTINGS) {
                saveSettings();

                // re-initialize events
                xu4.timerInterval = 1000 / xu4.settings->gameCyclesPerSecond;
            }
        }
        else if (menu == &gameplayMenu ||
                 menu == &interfaceMenu) {
            if (itemId == USE_SETTINGS)
                saveSettings();
        }

        if (itemId == CANCEL)
            settingsChanged = *xu4.settings;    // discard settings
    }

    drawMenu();
}

void SettingsMenus::updateConfMenu(int itemId) {
    // show or hide game enhancement options if enhancements are enabled/disabled
    confMenu.itemOfId(MI_CONF_GAMEPLAY)->setVisible(settingsChanged.enhancements);
    confMenu.itemOfId(MI_CONF_INTERFACE)->setVisible(settingsChanged.enhancements);

    saveSettings();

    switch(itemId) {
    case MI_CONF_VIDEO:
        showMenu(&videoMenu);
        break;
    case MI_VIDEO_CONF_GFX:
        showMenu(&gfxMenu);
        break;
    case MI_CONF_SOUND:
        showMenu(&soundMenu);
        break;
    case MI_CONF_INPUT:
        showMenu(&inputMenu);
        break;
    case MI_CONF_SPEED:
        showMenu(&speedMenu);
        break;
    case MI_CONF_GAMEPLAY:
        showMenu(&gameplayMenu);
        break;
    case MI_CONF_INTERFACE:
        showMenu(&interfaceMenu);
        break;
    }
}

void SettingsMenus::updateVideoMenu(int itemId) {
    switch(itemId) {
    case USE_SETTINGS:
        /* save settings (if necessary) */
        if (*xu4.settings != settingsChanged) {
            saveSettings();

            /* FIXME: resize images, etc. */
            INTRO_CON;
            ic->deleteIntro();  // delete intro stuff
            screenReInit();
            ic->init();         // re-fix the backgrounds and scale images, etc.

            // go back to menu mode
            ic->mode = IntroController::INTRO_MENU;
        }
        break;
    case MI_VIDEO_CONF_GFX:
        showMenu(&gfxMenu);
        break;
    }
}

void SettingsMenus::updateSoundMenu(int itemId) {
    switch(itemId) {
        case MI_SOUND_01:
            musicSetVolume(settingsChanged.musicVol);
            break;
        case MI_SOUND_02:
            soundSetVolume(settingsChanged.soundVol);
            soundPlay(SOUND_FLEE);
            break;
        case USE_SETTINGS:
            // save settings
            xu4.settings->setData(settingsChanged);
            xu4.settings->write();
            {
            INTRO_CON;
            musicPlay(ic->introMusic);
            }
            break;
        case CANCEL:
            musicSetVolume(xu4.settings->musicVol);
            soundSetVolume(xu4.settings->soundVol);
            break;
    }
}

//----------------------------------------------------------------------------

IntroController::IntroController() :
    backgroundArea(),
    menuArea(1 * CHAR_WIDTH, 13 * CHAR_HEIGHT, 38, 11),
    questionArea(INTRO_TEXT_X * CHAR_WIDTH, INTRO_TEXT_Y * CHAR_HEIGHT, INTRO_TEXT_WIDTH, INTRO_TEXT_HEIGHT),
    mapArea(BORDER_WIDTH, (TILE_HEIGHT * 6) + BORDER_HEIGHT, INTRO_MAP_WIDTH, INTRO_MAP_HEIGHT),
    menus(NULL),
    binData(NULL),
    titles(),                   // element list
    title(titles.begin()),      // element iterator
    gateAnimator(NULL),
    bSkipTitles(false),
    egaGraphics(true)
{
}

IntroController::~IntroController() {
    delete menus;

    for (unsigned i=0; i < titles.size(); i++) {
        delete titles[i].srcImage;
        delete titles[i].destImage;
    }
}

#ifdef GPU_RENDER
void IntroController::enableMap() {
    Coords center(INTRO_MAP_WIDTH / 2, INTRO_MAP_HEIGHT / 2);
    screenUpdateMap(&mapArea, &binData->introMap, center);
}

#define MAP_ENABLE  enableMap()
#define MAP_DISABLE mapArea.clear()
#else
#define MAP_ENABLE
#define MAP_DISABLE
#endif

/**
 * Initializes intro state and loads in introduction graphics, text
 * and map data from title.exe.
 */
bool IntroController::init() {

    justInitiatedNewGame = false;
    introMusic = MUSIC_TOWNS;

    uint16_t saveGroup = xu4_setResourceGroup(INTRO_RES);

    // sigData is referenced during Titles initialization
    binData = new IntroBinData();
    binData->load();

    Symbol sym[2];
    xu4.config->internSymbols(sym, 2, "beast0frame00 beast1frame00");
    beastiesImg = xu4.imageMgr->get(BKGD_ANIMATE);  // Assign resource group.
    beastieSub[0] = beastiesImg->subImageIndex[sym[0]];
    beastieSub[1] = beastiesImg->subImageIndex[sym[1]];

    if (xu4.errorMessage)
        bSkipTitles = true;

    if (bSkipTitles)
    {
        // the init() method is called again from within the
        // game via ALT-Q, so return to the menu
        //
#ifndef IOS
        mode = INTRO_MENU;
#else
        mode = INTRO_MAP;
#endif
        beastiesVisible = true;
        beastieOffset = 0;

        musicPlay(introMusic);
    }
    else
    {
        // initialize the titles
        initTitles();
        mode = INTRO_TITLES;
        beastiesVisible = false;
        beastieOffset = -32;
    }

    beastie1Cycle = 0;
    beastie2Cycle = 0;

    sleepCycles = 0;
    scrPos = 0;
#ifndef GPU_RENDER
    objectStateTable = new IntroObjectState[IntroBinData::INTRO_BASETILE_TABLE_SIZE];
#endif

    backgroundArea.reinit();
    menuArea.reinit();
    if (menus)
        menus->extendedMenuArea.reinit();
    questionArea.reinit();
    mapArea.reinit();

    // only update the screen if we are returning from game mode
    if (bSkipTitles)
        updateScreen();

    xu4_setResourceGroup(saveGroup);
    return true;
}

bool IntroController::hasInitiatedNewGame()
{
    return this->justInitiatedNewGame;
}

/**
 * Frees up data not needed after introduction.
 */
void IntroController::deleteIntro() {
    delete binData;
    binData = NULL;

#ifdef GPU_RENDER
    // Ensure that any running map animations are freed.
    Animator* fa = &xu4.eventHandler->flourishAnim;
    if (fa->used)
        anim_clear(fa);
#else
    delete [] objectStateTable;
    objectStateTable = NULL;
#endif

    xu4_freeResourceGroup(INTRO_RES);
    beastiesImg = NULL;
}

unsigned char *IntroController::getSigData() {
    ASSERT(binData->sigData != NULL, "intro sig data not loaded");
    return binData->sigData;
}

/**
 * Handles keystrokes during the introduction.
 */
bool IntroController::keyPressed(Stage* st, int key) {
    bool valid = true;

    switch (mode) {

    case INTRO_TITLES:
        // the user pressed a key to abort the sequence
        skipTitles();
        break;

    case INTRO_MAP:
        MAP_DISABLE;
        mode = INTRO_MENU;
        updateScreen();
        break;

    case INTRO_MENU:
        switch (key) {
        case 'i':
            stage_run(STAGE_NEWGAME);
            break;
        case 'j':
            journeyOnward(st);
            break;
        case 'r':
            mode = INTRO_MAP;
            updateScreen();
            MAP_ENABLE;
            break;
        case 'c':
            stage_run(STAGE_SETMENU);
            break;
        case 'a':
            stage_run(STAGE_ABOUT);
            break;
        case 'q':
            stage_exit();
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        {
            int n = key - '0';
            if (n > MUSIC_NONE && n < MUSIC_MAX)
                musicPlay(introMusic = n);
        }
            break;
        default:
            valid = false;
            break;
        }
        break;

    default:
        ASSERT(0, "key handler called in wrong mode");
        return true;
    }

    return valid;
}

/**
 * Draws the small map on the intro screen.
 */
void IntroController::drawMap() {
    if (sleepCycles > 0) {
        drawMapStatic();
        drawMapAnimated();
        sleepCycles--;
    }
    else {
        TileId tileId;
        int frame;
        int x, y;
        unsigned char commandNibble;
        unsigned char dataNibble;
        const unsigned char* script = binData->scriptTable;

        do {
            commandNibble = script[scrPos] >> 4;

            switch(commandNibble) {
                /* 0-4 = set object position and tile frame */
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                /* ----------------------------------------------------------
                   Set object position and tile frame
                   Format: yi [t(3); x(5)]
                   i = table index
                   x = x coordinate (5 least significant bits of second byte)
                   y = y coordinate
                   t = tile frame (3 most significant bits of second byte)
                   ---------------------------------------------------------- */
                dataNibble = script[scrPos] & 0xf;
                x = script[scrPos+1] & 0x1f;
                y = commandNibble;

                // See if the tile id needs to be recalculated
                tileId = binData->baseTileTable[dataNibble]->getId();
                frame = script[scrPos+1] >> 5;
                if (frame >= binData->baseTileTable[dataNibble]->getFrames()) {
                    frame -= binData->baseTileTable[dataNibble]->getFrames();
                    tileId += 1;
                }

#ifdef GPU_RENDER
                {
                VisualEffect* fx =
                    mapArea.useEffect(dataNibble, tileId, (float)x, (float)y);
                fx->vid += frame;
                }
#else
                objectStateTable[dataNibble].x = x;
                objectStateTable[dataNibble].y = y;
                objectStateTable[dataNibble].tile = MapTile(tileId);
                objectStateTable[dataNibble].tile.frame = frame;
#endif

                scrPos += 2;
                break;
            case 7:
                /* ---------------
                   Delete object
                   Format: 7i
                   i = table index
                   --------------- */
                dataNibble = script[scrPos] & 0xf;
#ifdef GPU_RENDER
                mapArea.removeEffect(dataNibble);
#else
                objectStateTable[dataNibble].tile = 0;
#endif
                scrPos++;
                break;
            case 8:
                /* ----------------------------------------------
                   Redraw intro map and objects, then go to sleep
                   Format: 8c
                   c = cycles to sleep
                   ---------------------------------------------- */
                drawMapStatic();
                drawMapAnimated();

                /* set sleep cycles */
                sleepCycles = script[scrPos] & 0xf;
                scrPos++;
                break;
            case 0xf:
                /* -------------------------------------
                   Jump to the start of the script table
                   Format: f?
                   ? = doesn't matter
                   ------------------------------------- */
                scrPos = 0;
                break;
            default:
                /* invalid command */
                scrPos++;
                break;
            }

        } while (commandNibble != 8);
    }
}

void IntroController::drawMapStatic() {
#ifndef GPU_RENDER
    int x, y;

    // draw unmodified map
    for (y = 0; y < INTRO_MAP_HEIGHT; y++)
        for (x = 0; x < INTRO_MAP_WIDTH; x++)
            mapArea.drawTile(binData->introMap[x + (y * INTRO_MAP_WIDTH)], x, y);
#endif
}

void IntroController::drawMapAnimated() {
#ifndef GPU_RENDER
    int i;

    // draw animated objects
    Image::enableBlend(1);
    for (i = 0; i < IntroBinData::INTRO_BASETILE_TABLE_SIZE; i++) {
        IntroObjectState& state = objectStateTable[i];
        if (state.tile != 0)
            mapArea.drawTile(state.tile, state.x, state.y);
    }
    Image::enableBlend(0);
#endif
}

/**
 * Draws the animated beasts in the upper corners of the screen.
 */
void IntroController::drawBeasties() {
    drawBeastie(0, beastieOffset, binData->beastie1FrameTable[beastie1Cycle]);
    drawBeastie(1, beastieOffset, binData->beastie2FrameTable[beastie2Cycle]);
    if (beastieOffset < 0)
        beastieOffset++;
}

/**
 * Animates the "beasties".  The animate intro image is made up frames
 * for the two creatures in the top left and top right corners of the
 * screen.  This function draws the frame for the given beastie on the
 * screen.  vertoffset is used lower the creatures down from the top
 * of the screen.
 */
void IntroController::drawBeastie(int beast, int vertoffset, int frame) {
    ASSERT(beast == 0 || beast == 1, "invalid beast: %d", beast);

    backgroundArea.draw(beastiesImg, beastieSub[beast] + frame,
                        beast ? (320 - 48) : 0,
                        vertoffset);
}

/**
 * Animates the moongate in the tree intro image.  There are two
 * overlays in the part of the image normally covered by the text.  If
 * the frame parameter is "moongate", the moongate overlay is painted
 * over the image.  If frame is "items", the second overlay is
 * painted: the circle without the moongate, but with a small white
 * dot representing the anhk and history book.
 */
struct GateAnimator
{
    ImageInfo* info;
    const SubImage* subimage;
    int fi, fcount;
    int gateH, deltaH;
    int x, y, ytop;
    Symbol frame;

    void initialize(Symbol aframe, bool egaGraphics) {
        info = xu4.imageMgr->imageInfo(IMG_MOONGATE, &subimage);
        if (! subimage)
            return;

        // Hack to account for different tree images.
        if (egaGraphics) {
            x = 72;
            ytop = 68;
        } else {
            x = 84;
            ytop = 53;
        }

        y = ytop + subimage->height;
        fcount = subimage->height;

        frame = aframe;
        if (frame == IMG_MOONGATE) {
            soundPlay(SOUND_GATE_OPEN);
            gateH = 1;
            deltaH = 1;
        } else {
            gateH = subimage->height - 1;
            deltaH = -1;
        }
        fi = 0;
    }

    // Wait 1/24 sec. if returns true.  Otherwise the animation is complete.
    bool nextFrame(IntroController* ic) {
        if (! subimage)
            return false;

        if (deltaH < 0)
            ic->backgroundArea.draw(frame, x, ytop);

        info->image->drawSubRect(x, y - gateH, subimage->x, subimage->y,
                                 subimage->width, gateH);
        gateH += deltaH;
        if (gateH < 0)
            gateH = 0;
        else if (gateH > subimage->height)
            gateH = subimage->height;

        screenUploadToGPU();

        return (++fi < fcount);
    }
};

/**
 * Draws the cards in the character creation sequence with the gypsy.
 */
void IntroController::drawCard(int pos, int card, const uint8_t* origin) {
    static const char *cardNames[] = {
        "honestycard", "compassioncard", "valorcard", "justicecard",
        "sacrificecard", "honorcard", "spiritualitycard", "humilitycard"
    };

    ASSERT(pos == 0 || pos == 1, "invalid pos: %d", pos);
    ASSERT(card < 8, "invalid card: %d", card);

    backgroundArea.draw(xu4.config->intern(cardNames[card]),
                        pos ? origin[2] : origin[0], origin[1]);
}

/**
 * Draws the beads in the abacus during the character creation sequence
 */
void IntroController::drawAbacusBeads(int row, int selectedVirtue, int rejectedVirtue) {
    ASSERT(row >= 0 && row < 7, "invalid row: %d", row);
    ASSERT(selectedVirtue < 8 && selectedVirtue >= 0, "invalid virtue: %d", selectedVirtue);
    ASSERT(rejectedVirtue < 8 && rejectedVirtue >= 0, "invalid virtue: %d", rejectedVirtue);

    const SubImage* pos = abacusImg->subImages + beadSub[2];
    int y = pos->y + (row * pos->height);
    Image::enableBlend(1);
    backgroundArea.draw(abacusImg, beadSub[0],  // whitebead
                        pos->x + (selectedVirtue * pos->width), y);
    backgroundArea.draw(abacusImg, beadSub[1],  // blackbead
                        pos->x + (rejectedVirtue * pos->width), y);
    Image::enableBlend(0);
}

/**
 * Paints the screen.
 */
void IntroController::updateScreen() {
    screenHideCursor();

    switch (mode) {
    case INTRO_MAP:
        backgroundArea.draw(BKGD_INTRO);
        drawMap();
        drawBeasties();
        // display the profile name if a local profile is being used
        {
        const string& pname = xu4.settings->profile;
        if (! pname.empty())
            screenTextAt(40-pname.length(), 24, "%s", pname.c_str());
        }
        break;

    case INTRO_MENU:
        // draw the extended background for all option screens
        backgroundArea.draw(BKGD_INTRO);
        backgroundArea.draw(BKGD_OPTIONS_BTM, 0, 120);

        menuArea.textAt(1,  1, "In another world, in a time to come.");
        menuArea.textAt(14, 3, "Options:");
        menuArea.textAtKey(10, 5, "Return to the view", 0);
        menuArea.textAtKey(10, 6, "Journey Onward", 0);
        menuArea.textAtKey(10, 7, "Initiate New Game", 0);
        menuArea.textAtKey(10, 8, "Configure", 0);
        menuArea.textAtKey(10, 9, "About", 0);
        drawBeasties();

        // draw the cursor last
        screenSetCursorPos(24, 16);
        break;

    default:
        ASSERT(0, "bad mode in updateScreen");
    }
}

/*
 * STAGE_IERROR
 */
void* error_enter(Stage*, void*)
{
    INTRO_CON;

    screenHideCursor();

    ic->backgroundArea.draw(BKGD_INTRO);
    ic->backgroundArea.draw(BKGD_OPTIONS_BTM, 0, 120);

    assert(xu4.errorMessage);
    int len = strlen(xu4.errorMessage);
    ic->menuArea.textAt(19 - len / 2, 5, xu4.errorMessage);
    xu4.errorMessage = NULL;

    ic->drawBeasties();
    screenUploadToGPU();

    return ic;
}

int error_dispatch(Stage* st, const InputEvent* ev)
{
    // Waiting 3 seconds using Stage::tickInterval.
    if (ev->type == IE_CYCLE_TIMER) {
        INTRO_CON_ST;
        ic->updateScreen();
        stage_done();
    }
    return 0;
}

/*
 * STAGE_NEWGAME
 *
 * Initiate a new savegame by reading the name, sex, then presenting a
 * series of questions to determine the class of the new character.
 */
void* newgame_enter(Stage* st, void*)
{
    INTRO_CON;

#if 0
    xu4.errorMessage = "\027Test Error Message\031";
    stage_run(STAGE_IERROR);
    return NULL;
#endif

#if 1
    // draw the extended background for all option screens
    ic->backgroundArea.draw(BKGD_INTRO);
    ic->backgroundArea.draw(BKGD_OPTIONS_BTM, 0, 120);

    // display name prompt and read name from keyboard
    ic->menuArea.textAt(3, 3, "By what name shalt thou be known");
    ic->menuArea.textAt(3, 4, "in this world and time?");

    // enable the text cursor after setting it's initial position
    // this will avoid drawing in undesirable areas like 0,0
    ic->menuArea.setCursorPos(11, 7);

    ic->drawBeasties();

    ic->dstate = 0;
    input_stringInitView(&ic->inString, 12, &ic->menuArea, "\033");
#else
    // For testing.
    ic->nameBuffer = "test";
    ic->sex = 'm';
    for (int i = 0; i < 8; i++) {
        ic->questionTree[i] = i;
        ic->questionTree[i+8] = i;
    }
    stage_run(STAGE_BEGIN);
#endif

    return ic;
}

int newgame_dispatch(Stage* st, const InputEvent* ev)
{
    INTRO_CON_ST;

    switch (ic->dstate) {
    case 0:
        switch (input_stringDispatch(&ic->inString, ev)) {
            case INPUT_DONE:
                ic->nameBuffer = ic->inString.value;

                // draw the extended background for all option screens
                ic->backgroundArea.draw(BKGD_INTRO);
                ic->backgroundArea.draw(BKGD_OPTIONS_BTM, 0, 120);

                // display sex prompt and read sex from keyboard
                ic->menuArea.textAt(3, 3, "Art thou Male or Female?");

                // the cursor is already enabled, just change its position
                ic->menuArea.setCursorPos(28, 3);

                ic->drawBeasties();

                ic->dstate++;
                break;

            case INPUT_CANCEL:
                //ic->menuArea.hideCursor();
                screenShowCursor();
                ic->updateScreen();
                stage_done();
                break;
        }
        break;

    case 1:
        if (input_choiceDispatch("mf", ev, &ic->sex) == INPUT_DONE) {
            ic->menuArea.hideCursor();
            // Display entry for a moment.
            // FIXME: input_choice draws the char when the game context exists,
            //        but we must do it manally here.
            ic->menuArea.drawChar(toupper(ic->sex), 28, 3);
            screenUploadToGPU();

            stage_startMsecTimer(st, 500);
            ic->dstate++;
        }
        break;

    case 2:
        if (ev->type == IE_MSEC_TIMER)
            stage_run(STAGE_STORY);
        break;
    }

    return 1;
}

/*
 * STAGE_STORY - Show the lead up story
 */
void* story_enter(Stage* st, void*)
{
    INTRO_CON;

    // no more text entry, so disable the text cursor
    ic->menuArea.hideCursor();

    ic->saveGroup = xu4_setResourceGroup(INTRO_RES);
    ic->beastiesVisible = false;

    ImageInfo* tree = xu4.imageMgr->get(BKGD_TREE);
    ic->egaGraphics = tree && (tree->getFilename().compare(0, 3, "u4/") == 0);

    ic->gateAnimator = new GateAnimator;

    // Begin the 24 story scenes.
    ic->dstate = 0;
    ic->showStory(st, 0);

    return ic;
}

void story_leave(Stage* st)
{
    INTRO_CON_ST;

    delete ic->gateAnimator;
    ic->gateAnimator = NULL;

    xu4_setResourceGroup(ic->saveGroup);
}

int story_dispatch(Stage* st, const InputEvent* ev)
{
    INTRO_CON_ST;
    if (input_anykey(ev) == INPUT_DONE) {
        ic->dstate++;
        if (ic->dstate < 24) {
            ic->showStory(st, ic->dstate);
        } else {
            stage_run(STAGE_GYPSY);
        }
        return 1;
    }
    return 0;
}

// storyInd is 0-23.
void IntroController::showStory(Stage* st, int storyInd)
{
    Symbol bg = 0;

    if (storyInd == 0)
        bg = BKGD_TREE;
    else if (storyInd == 6)
        bg = BKGD_PORTAL;
    else if (storyInd == 11)
        bg = BKGD_TREE;
    else if (storyInd == 15)
        bg = BKGD_OUTSIDE;
    else if (storyInd == 17)
        bg = BKGD_INSIDE;
    else if (storyInd == 20)
        bg = BKGD_WAGON;
    else if (storyInd == 21)
        bg = BKGD_GYPSY;
    else if (storyInd == 23)
        bg = BKGD_ABACUS;

    if (bg)
        backgroundArea.draw(bg);

    showText(binData->introText[storyInd]);

    switch (storyInd) {
        case 3:
            questionArea.hideCursor();
            gateAnimator->initialize(IMG_MOONGATE, egaGraphics);
            stage_run(STAGE_GATE);
            return;
        case 5:
            questionArea.hideCursor();
            gateAnimator->initialize(IMG_ITEMS, egaGraphics);
            stage_run(STAGE_GATE);
            return;
        case 20:
            soundSpeakLine(VOICE_GYPSY, 0);
            break;
        case 22:
            soundSpeakLine(VOICE_GYPSY, 1);
            break;
        case 23:
            soundSpeakLine(VOICE_GYPSY, 2);
            break;
    }

    // enable the cursor here to avoid drawing in undesirable locations
    questionArea.showCursor();
}

/*
 * STAGE_GATE - Animate moongate for tree story scenes 3 & 5.
 */
void* gate_enter(Stage* st, void*)
{
    INTRO_CON;
    stage_startMsecTimer(st, 41);
    return ic;
}

int gate_dispatch(Stage* st, const InputEvent* ev)
{
    if (ev->type == IE_MSEC_TIMER) {
        INTRO_CON_ST;
        if (! ic->gateAnimator->nextFrame(ic)) {
            ic->questionArea.showCursor();
            stage_done();
        }
    }
    return 0;
}

static uint8_t originTable[6] = {
    12, 12, 218,    // EGA
    22, 16, 218,    // Utne
};

/*
 * STAGE_GYPSY
 *
 * Starts the gypsys questioning that eventually determines the new
 * characters class.
 */
void* gypsy_enter(Stage* st, void*)
{
    INTRO_CON;

    ic->beastiesVisible = false;

    ImageInfo* abacusImg = xu4.imageMgr->get(BKGD_ABACUS);
    if (! abacusImg)
        errorLoadImage(BKGD_ABACUS);
    ic->abacusImg = abacusImg;

    Symbol sym[3];
    xu4.config->internSymbols(sym, 3, "whitebead blackbead bead_pos");
    ic->beadSub[0] = abacusImg->subImageIndex[sym[0]];
    ic->beadSub[1] = abacusImg->subImageIndex[sym[1]];
    ic->beadSub[2] = abacusImg->subImageIndex[sym[2]];

    ic->origin = originTable;
    if (! ic->egaGraphics)
        ic->origin += 3;

    ic->questionRound = 0;
    ic->initQuestionTree();
    ic->questionArea.hideCursor();
    abacusImg->image->draw(0, 0);

    ic->dstate = 0;
    ic->showQuestion(st, 0);
    return ic;
}

int gypsy_dispatch(Stage* st, const InputEvent* ev)
{
    INTRO_CON_ST;
    int answer;

    switch (ic->dstate) {
    case 0:
        if (ev->type == IE_MSEC_TIMER)
            goto next_state;
        break;

    case 1:
        if (ev->type == IE_MSEC_TIMER) {
            stage_stopMsecTimer(st);
            goto next_state;
        }
        break;

    case 2:
        if (input_anykey(ev) == INPUT_DONE)
            goto next_state;
        break;

    case 3:
        if (input_choiceDispatch("ab", ev, &answer) == INPUT_DONE) {
            // update the question tree
            if (ic->doQuestion((answer == 'a') ? 0 : 1)) {
                stage_run(STAGE_BEGIN);
                return 1;
            }
            ic->dstate = 0;     // Reset for next question.
            goto show;
        }
        break;
    }
    return 0;

next_state:
    ic->dstate++;
show:
    ic->showQuestion(st, ic->dstate);
    return 1;
}

void IntroController::showQuestion(Stage* st, int state)
{
    const vector<string>& gypsyText = binData->introGypsy;
    int i0 = questionRound * 2;
    int n;

    switch (state) {
    case 0:
        if (questionRound == 0)
            n = GYP_PLACES_FIRST;
        else
            n = (questionRound == 6) ? GYP_PLACES_LAST : GYP_PLACES_TWOMORE;

        // draw the cards and show the lead up text

        questionArea.hideCursor();
        questionArea.clear();
        questionArea.textAt(0, 0, gypsyText[n].c_str());
        questionArea.textAt(0, 1, gypsyText[GYP_UPON_TABLE].c_str());
        stage_startMsecTimer(st, 1000);
        break;

    case 1:
    {
        const string& virtue1 = gypsyText[questionTree[i0] + 4];
        questionArea.textAtFmt(0, 2, "%s and", virtue1.c_str());
        drawCard(0, questionTree[i0], origin);
        //stage_startMSecTimer(st, 1000);   // Timer still running.
    }
        break;

    case 2:
    {
        const string& virtue1 = gypsyText[questionTree[i0] + 4];
        n = i0 + 1;
        soundSpeakLine(VOICE_GYPSY, 3);
        questionArea.textAtFmt(virtue1.size() + 4, 2, " %s.  She says",
                               gypsyText[questionTree[n] + 4].c_str());
        drawCard(1, questionTree[n], origin);
        questionArea.textAt(0, 3, "\"Consider this:\"");
        questionArea.showCursor();

#ifdef IOS
        U4IOS::switchU4IntroControllerToContinueButton();
#endif
        // wait for a key
    }
        break;

    case 3:
        // show the question to choose between virtues
        showText(getQuestion(questionTree[i0], questionTree[i0 + 1]));
        questionArea.showCursor();
#ifdef IOS
        U4IOS::switchU4IntroControllerToABButtons();
#endif
        // wait for an answer
        break;
    }
}

/**
 * Get the text for the question giving a choice between virtue v1 and
 * virtue v2 (zero based virtue index, starting at honesty).
 */
string IntroController::getQuestion(int v1, int v2) {
    int i = 0;
    int d = 7;

    ASSERT(v1 < v2, "first virtue must be smaller (v1 = %d, v2 = %d)", v1, v2);

    while (v1 > 0) {
        i += d;
        d--;
        v1--;
        v2--;
    }

    ASSERT((i + v2 - 1) < 28, "calculation failed");

    return binData->introQuestions[i + v2 - 1];
}

/*
 * Return error message or NULL if successful.
 */
const char* IntroController::createSaveGame()
{
    delete xu4.saveGame;
    xu4.saveGame = NULL;    // Make GameController::init() reload the game.

    FILE *saveGameFile = fopen((xu4.settings->getUserPath() + PARTY_SAV).c_str(), "wb");
    if (! saveGameFile)
        return "Unable to create save game!";

    {
    SaveGame saveGame;
    SaveGamePlayerRecord avatar;

    avatar.init();
    strcpy(avatar.name, nameBuffer.c_str());
    avatar.sex = (sex == 'm') ? SEX_MALE : SEX_FEMALE;

    saveGame.init(&avatar);
    initPlayers(&saveGame);
    saveGame.food = 30000;
    saveGame.gold = 200;
    saveGame.reagents[REAG_GINSENG] = 3;
    saveGame.reagents[REAG_GARLIC] = 4;
    saveGame.torches = 2;
    saveGame.write(saveGameFile);
    }

    fclose(saveGameFile);

    saveGameFile = fopen((xu4.settings->getUserPath() + MONSTERS_SAV).c_str(), "wb");
    if (saveGameFile) {
        saveGameMonstersWrite(NULL, saveGameFile);
        fclose(saveGameFile);
    }
    return NULL;
}

/*
 * STAGE_BEGIN - Write out save game and segue into game.
 */
void* begin_enter(Stage* st, void*)
{
    INTRO_CON;

    const char* err = ic->createSaveGame();
    if (err) {
        ic->questionArea.hideCursor();
        xu4.errorMessage = err;
        stage_run(STAGE_IERROR);
        return NULL;
    }

    ic->justInitiatedNewGame = true;

    // show the text that segues into the main game
    ic->showText(ic->binData->introGypsy[GYP_SEGUE1]);
    ic->questionArea.showCursor();
    soundSpeakLine(VOICE_GYPSY, 4);
#ifdef IOS
    U4IOS::switchU4IntroControllerToContinueButton();
#endif
    ic->dstate = 0;
    return ic;
}

int begin_dispatch(Stage* st, const InputEvent* ev)
{
    INTRO_CON_ST;
    switch (ic->dstate) {
    case 0:
        if (input_anykey(ev) == INPUT_DONE) {
            ic->showText(ic->binData->introGypsy[GYP_SEGUE2]);
            ic->dstate++;
            return 1;
        }
        break;
    case 1:
        if (input_anykey(ev) == INPUT_DONE) {
            // done: exit intro and let game begin
            ic->questionArea.hideCursor();
            stage_run(STAGE_PLAY);
            return 1;
        }
        break;
    }
    return 0;
}

/**
 * Starts the game.
 */
void IntroController::journeyOnward(Stage* st) {
    // Return to a running game or attempt to load a saved one.
    if (xu4.saveGame || saveGameLoad())
        stage_run(STAGE_PLAY);
    else {
        // Show the errorMessage set by saveGameLoad().
        stage_run(STAGE_IERROR);
    }
}


/*
 * STAGE_SETMENU - Show settings menus
 */
void* setmenu_enter(Stage*, void*)
{
    INTRO_CON;
    SettingsMenus* menus = ic->menus;

    if (! menus) {
       ic->menus = menus = new SettingsMenus;
    }

    // Make a copy of our settings so we can change them
    menus->settingsChanged = *xu4.settings;
    screenShowCursor();
    menus->listenerId = gs_listen(1<<SENDER_MENU, menus->menusNotice, menus);

    menus->showMenu(&menus->confMenu);
    menus->drawMenu();

    return ic;
}

void setmenu_leave(Stage* st)
{
    INTRO_CON_ST;

    gs_unplug(ic->menus->listenerId);
    //screenShowCursor();
    ic->updateScreen();
    screenUploadToGPU();
}

int setmenu_dispatch(Stage* st, const InputEvent* ev)
{
    if (ev->type == IE_KEY_PRESS) {
        INTRO_CON_ST;
        SettingsMenus* menus = ic->menus;
        int res = input_menuDispatch(menus->active, ev->n);
        if (res == INPUT_DONE) {
            if (menus->active == &menus->confMenu)
                stage_done();
            else
                menus->showMenu(&menus->confMenu);
        }
        return 1;
    }
    return 0;
}


/**
 * STAGE_ABOUT - Shows an about box.
 */
void* about_enter(Stage*, void*)
{
    INTRO_CON;

    // draw the extended background for all option screens
    ic->backgroundArea.draw(BKGD_INTRO);
    ic->backgroundArea.draw(BKGD_OPTIONS_BTM, 0, 120);

    screenHideCursor();
    ic->menuArea.textAtFmt(14, 1, "XU4 %s", VERSION);
    ic->menuArea.textAt(1, 3, "xu4 is free software; you can redist-");
    ic->menuArea.textAt(1, 4, "ribute it and/or modify it under the");
    ic->menuArea.textAt(1, 5, "terms of the GNU GPL as published by");
    ic->menuArea.textAt(1, 6, "the FSF.  See COPYING.");
    ic->menuArea.textAt(4, 8, "Copyright \011 2002-2022, xu4 Team");
    ic->menuArea.textAt(4, 9, "Copyright \011 1987, Lord British");
    ic->drawBeasties();

    return ic;
}

void about_leave(Stage* st)
{
    INTRO_CON_ST;

    screenShowCursor();
    ic->updateScreen();
}

/**
 * Shows text in the question area.
 */
void IntroController::showText(const string &text) {
    string current = text;
    int lineNo = 0;

    questionArea.clear();

    unsigned long pos = current.find("\n");
    while (pos < current.length()) {
        questionArea.textAt(0, lineNo++, current.substr(0, pos).c_str());
        current = current.substr(pos+1);
        pos = current.find("\n");
    }

    /* write the last line (possibly only line) */
    questionArea.textAt(0, lineNo++, current.substr(0, pos).c_str());
}

/**
 * Timer callback for the intro sequence.  Handles animating the intro
 * map, the beasties, etc..
 */
void IntroController::timerFired() {
    screenCycle();

    if (mode == INTRO_TITLES)
        if (updateTitle() == false)
        {
            // setup the map screen
            mode = INTRO_MAP;
            beastiesVisible = true;
            musicPlay(introMusic);
            updateScreen();
            MAP_ENABLE;
        }

    if (mode == INTRO_MAP)
        drawMap();

    if (beastiesVisible)
        drawBeasties();

    if (xu4_random(2) && ++beastie1Cycle >= IntroBinData::BEASTIE1_FRAMES)
        beastie1Cycle = 0;
    if (xu4_random(2) && ++beastie2Cycle >= IntroBinData::BEASTIE2_FRAMES)
        beastie2Cycle = 0;

    screenUploadToGPU();
}

/**
 * Initializes the question tree.  The tree starts off with the first
 * eight entries set to the numbers 0-7 in a random order.
 */
void IntroController::initQuestionTree() {
    int i, tmp, r;

    for (i = 0; i < 8; i++)
        questionTree[i] = i;

    for (i = 0; i < 8; i++) {
        r = xu4_random(8);
        tmp = questionTree[r];
        questionTree[r] = questionTree[i];
        questionTree[i] = tmp;
    }
    answerInd = 8;

    if (questionTree[0] > questionTree[1]) {
        tmp = questionTree[0];
        questionTree[0] = questionTree[1];
        questionTree[1] = tmp;
    }

}

/**
 * Updates the question tree with the given answer, and advances to
 * the next round.
 * @return true if all questions have been answered, false otherwise
 */
bool IntroController::doQuestion(int answerB) {
    int n = questionRound * 2;

    questionTree[answerInd] = questionTree[ answerB ? n + 1 : n ];
    drawAbacusBeads(questionRound, questionTree[answerInd],
                    questionTree[ answerB ? n : n + 1 ]);

    answerInd++;
    questionRound++;

    if (questionRound > 6)
        return true;

    n = questionRound * 2;
    if (questionTree[n] > questionTree[n + 1]) {
        int tmp = questionTree[n];
        questionTree[n] = questionTree[n + 1];
        questionTree[n + 1] = tmp;
    }
    return false;
}

/**
 * Build the initial avatar player record from the answers to the
 * gypsy's questions.
 */
void IntroController::initPlayers(SaveGame *saveGame) {
    int i, p;
    static const struct {
        WeaponType weapon;
        ArmorType armor;
        int level, xp, x, y;
    } initValuesForClass[] = {
        { WEAP_STAFF,  ARMR_CLOTH,   2, 125, 231, 136 }, /* CLASS_MAGE */
        { WEAP_SLING,  ARMR_CLOTH,   3, 240,  83, 105 }, /* CLASS_BARD */
        { WEAP_AXE,    ARMR_LEATHER, 3, 205,  35, 221 }, /* CLASS_FIGHTER */
        { WEAP_DAGGER, ARMR_CLOTH,   2, 175,  59,  44 }, /* CLASS_DRUID */
        { WEAP_MACE,   ARMR_LEATHER, 2, 110, 158,  21 }, /* CLASS_TINKER */
        { WEAP_SWORD,  ARMR_CHAIN,   3, 325, 105, 183 }, /* CLASS_PALADIN */
        { WEAP_SWORD,  ARMR_LEATHER, 2, 150,  23, 129 }, /* CLASS_RANGER */
        { WEAP_STAFF,  ARMR_CLOTH,   1,   5, 186, 171 }  /* CLASS_SHEPHERD */
    };
    static const struct {
        const char *name;
        int str, dex, intel;
        SexType sex;
    } initValuesForNpcClass[] = {
        { "Mariah",    9, 12, 20, SEX_FEMALE }, /* CLASS_MAGE */
        { "Iolo",     16, 19, 13, SEX_MALE },   /* CLASS_BARD */
        { "Geoffrey", 20, 15, 11, SEX_MALE },   /* CLASS_FIGHTER */
        { "Jaana",    17, 16, 13, SEX_FEMALE }, /* CLASS_DRUID */
        { "Julia",    15, 16, 12, SEX_FEMALE }, /* CLASS_TINKER */
        { "Dupre",    17, 14, 17, SEX_MALE },   /* CLASS_PALADIN */
        { "Shamino",  16, 15, 15, SEX_MALE },   /* CLASS_RANGER */
        { "Katrina",  11, 12, 10, SEX_FEMALE }  /* CLASS_SHEPHERD */
    };

    saveGame->players[0].klass = static_cast<ClassType>(questionTree[14]);

    ASSERT(saveGame->players[0].klass < 8, "bad class: %d", saveGame->players[0].klass);

    saveGame->players[0].weapon = initValuesForClass[saveGame->players[0].klass].weapon;
    saveGame->players[0].armor = initValuesForClass[saveGame->players[0].klass].armor;
    saveGame->players[0].xp = initValuesForClass[saveGame->players[0].klass].xp;
    saveGame->x = initValuesForClass[saveGame->players[0].klass].x;
    saveGame->y = initValuesForClass[saveGame->players[0].klass].y;

    saveGame->players[0].str = 15;
    saveGame->players[0].dex = 15;
    saveGame->players[0].intel = 15;

    for (i = 0; i < VIRT_MAX; i++)
        saveGame->karma[i] = 50;

    for (i = 8; i < 15; i++) {
        saveGame->karma[questionTree[i]] += 5;
        switch (questionTree[i]) {
        case VIRT_HONESTY:
            saveGame->players[0].intel += 3;
            break;
        case VIRT_COMPASSION:
            saveGame->players[0].dex += 3;
            break;
        case VIRT_VALOR:
            saveGame->players[0].str += 3;
            break;
        case VIRT_JUSTICE:
            saveGame->players[0].intel++;
            saveGame->players[0].dex++;
            break;
        case VIRT_SACRIFICE:
            saveGame->players[0].dex++;
            saveGame->players[0].str++;
            break;
        case VIRT_HONOR:
            saveGame->players[0].intel++;
            saveGame->players[0].str++;
            break;
        case VIRT_SPIRITUALITY:
            saveGame->players[0].intel++;
            saveGame->players[0].dex++;
            saveGame->players[0].str++;
            break;
        case VIRT_HUMILITY:
            /* no stats for you! */
            break;
        }
    }

    PartyMember player(NULL, &saveGame->players[0]);
    saveGame->players[0].hp = saveGame->players[0].hpMax = player.getMaxLevel() * 100;
    saveGame->players[0].mp = player.getMaxMp();

    p = 1;
    for (i = 0; i < VIRT_MAX; i++) {
        player = PartyMember(NULL, &saveGame->players[i]);

        /* Initial setup for party members that aren't in your group yet... */
        if (i != saveGame->players[0].klass) {
            saveGame->players[p].klass = static_cast<ClassType>(i);
            saveGame->players[p].xp = initValuesForClass[i].xp;
            saveGame->players[p].str = initValuesForNpcClass[i].str;
            saveGame->players[p].dex = initValuesForNpcClass[i].dex;
            saveGame->players[p].intel = initValuesForNpcClass[i].intel;
            saveGame->players[p].weapon = initValuesForClass[i].weapon;
            saveGame->players[p].armor = initValuesForClass[i].armor;
            strcpy(saveGame->players[p].name, initValuesForNpcClass[i].name);
            saveGame->players[p].sex = initValuesForNpcClass[i].sex;
            saveGame->players[p].hp = saveGame->players[p].hpMax = initValuesForClass[i].level * 100;
            saveGame->players[p].mp = player.getMaxMp();
            p++;
        }
    }
}


/**
 * Preload map tiles
 */
void IntroController::preloadMap()
{
#ifndef GPU_RENDER
    int x, y, i;

    // draw unmodified map
    for (y = 0; y < INTRO_MAP_HEIGHT; y++)
        for (x = 0; x < INTRO_MAP_WIDTH; x++)
            mapArea.loadTile(binData->introMap[x + (y * INTRO_MAP_WIDTH)]);

    // draw animated objects
    for (i = 0; i < IntroBinData::INTRO_BASETILE_TABLE_SIZE; i++) {
        if (objectStateTable[i].tile != 0)
            mapArea.loadTile(objectStateTable[i].tile);
    }
#endif
}


//
// Initialize the title elements
//
void IntroController::initTitles()
{
    int titleDur = soundDuration(SOUND_TITLE_FADE);
    if (titleDur < 1000)
        titleDur = 5000;

    // add the intro elements
    //          x,  y,   w,  h, method,  delay, duration
    //
    addTitle(  97,  0, 130, 16, SIGNATURE,   1000, 3000 );  // "Lord British"
    addTitle( 148, 17,  24,  4, AND,         1000,  100 );  // "and"
    addTitle(  84, 31, 152,  1, BAR,         1000,  500 );  // <bar>
    addTitle(  86, 21, 148,  9, ORIGIN,      1000,  100 );  // "Origin Systems, Inc."
    addTitle( 133, 33,  54,  5, PRESENT,        0,  100 );  // "present"
    addTitle(  59, 33, 202, 46, TITLE,       1000, titleDur );  // "Ultima IV"
    addTitle(  40, 80, 240, 13, SUBTITLE,    1000,  100 );  // "Quest of the Avatar"
    addTitle(   0, 96, 320, 96, MAP,         1000,  100 );  // the map

    // get the source data for the elements
    getTitleSourceData();

    // reset the iterator
    title = titles.begin();

    // speed up the timer while the intro titles are displayed
    xu4.timerInterval = xu4.settings->titleSpeedOther;
}


//
// Add the intro element to the element list
//
void IntroController::addTitle(int x, int y, int w, int h, AnimType method, int delay, int duration)
{
    AnimElement data = {
        x, y,               // source x and y
        w, h,               // source width and height
        method,             // render method
        0,                  // animStep
        0,                  // animStepMax
        0,                  // timeBase
        delay,              // delay before rendering begins
        duration,           // total animation time
        NULL,               // storage for the source image
        NULL,               // storage for the animation frame
        std::vector<AnimPlot>()
    };
    titles.push_back(data);
}


//
// Get the source data for title elements
// that have already been initialized
//
void IntroController::getTitleSourceData()
{
    unsigned int r, g, b, a;        // color values
    unsigned char *srcData;         // plot data

    // The BKGD_INTRO image is assumed to have not been
    // loaded yet.  The unscaled version will be loaded
    // here, and elements of the image will be stored
    // individually.  Afterward, the BKGD_INTRO image
    // will be scaled appropriately.
    ImageInfo *info = xu4.imageMgr->get(BKGD_INTRO);
    if (!info)
        errorLoadImage(BKGD_INTRO);

    // for each element, get the source data
    for (unsigned i=0; i < titles.size(); i++)
    {
        if ((titles[i].method != SIGNATURE)
            && (titles[i].method != BAR))
        {
            // create a place to store the source image
            titles[i].srcImage = Image::create(titles[i].rw, titles[i].rh);

            // get the source image
            info->image->drawSubRectOn(titles[i].srcImage, 0, 0,
                                       titles[i].rx, titles[i].ry,
                                       titles[i].rw, titles[i].rh);
        }

        // after getting the srcImage
        switch (titles[i].method)
        {
            case SIGNATURE:
            {
                // PLOT: "Lord British"
                srcData = getSigData();

                RGBA color = info->image->setColor(0, 255, 255);    // cyan for EGA
                int blue[16] = {255, 250, 226, 226, 210, 194, 161, 161,
                                129,  97,  97,  64,  64,  32,  32,   0};
                int x = 0;
                int y = 0;

                while (srcData[titles[i].animStepMax] != 0)
                {
                    x = srcData[titles[i].animStepMax] - 0x4C;
                    y = 0xC0 - srcData[titles[i].animStepMax+1];

                    if (xu4.imageMgr->usingVGA())
                    {
                        // yellow gradient
                        color = info->image->setColor(255, (y == 2 ? 250 : 255), blue[y-1]);
                    }
                    AnimPlot plot = {
                        uint8_t(x),
                        uint8_t(y),
                        uint8_t(color.r),
                        uint8_t(color.g),
                        uint8_t(color.b),
                        255};
                    titles[i].plotData.push_back(plot);
                    titles[i].animStepMax += 2;
                }
                titles[i].animStepMax = titles[i].plotData.size();
                break;
            }

            case BAR:
            {
                titles[i].animStepMax = titles[i].rw;  // image width
                break;
            }

            case TITLE:
            {
                for (int y=0; y < titles[i].rh; y++)
                {
                    // Here x is set to exclude PRESENT at top of image.
                    int x = (y < 6) ? 133 : 0;
                    for ( ; x < titles[i].rw ; x++)
                    {
                        titles[i].srcImage->getPixel(x, y, r, g, b, a);
                        if (r || g || b)
                        {
                            AnimPlot plot = {
                                uint8_t(x+1), uint8_t(y+1),
                                uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)
                            };
                            titles[i].plotData.push_back(plot);
                        }
                    }
                }
                titles[i].animStepMax = titles[i].plotData.size();
                break;
            }

            case MAP:
            {
                // fill the map area with the transparent color
                titles[i].srcImage->fillRect(8, 8, 304, 80, 0, 0, 0, 0);
                titles[i].animStepMax = 20;
                break;
            }

            default:
            {
                titles[i].animStepMax = titles[i].rh ;  // image height
                break;
            }
        }

        // create the initial animation frame
        titles[i].destImage = Image::create(2 + titles[i].rw,
                                            2 + titles[i].rh);
        titles[i].destImage->fill(Image::black);
    }
}


//
// Update the title element, drawing the appropriate frame of animation
//
bool IntroController::updateTitle()
{
    int animStepTarget = 0;

    int timeCurrent = getTicks();
    float timePercent = 0;

    if (title->animStep == 0 && !bSkipTitles)
    {
        if (title->timeBase == 0)
        {
            // reset the base time
            title->timeBase = timeCurrent;

            if (title == titles.begin()) {
                // clear the screen
                xu4.screenImage->fill(Image::black);
            }
        }
    }

    // abort after processing all elements
    if (title == titles.end())
        return false;

    // delay the drawing of this phase
    if ((timeCurrent - title->timeBase) < title->timeDelay)
        return true;

    // determine how much of the animation should have been drawn up until now
    timePercent = float(timeCurrent - title->timeBase - title->timeDelay) / title->timeDuration;
    if (timePercent > 1 || bSkipTitles)
        timePercent = 1;
    animStepTarget = int(title->animStepMax * timePercent);

    // perform the animation
    switch (title->method)
    {
        case SIGNATURE:
        {
            while (animStepTarget > title->animStep)
            {
                // blit the pixel-pair to the src surface
                title->destImage->fillRect(
                    title->plotData[title->animStep].x,
                    title->plotData[title->animStep].y,
                    2,
                    1,
                    title->plotData[title->animStep].r,
                    title->plotData[title->animStep].g,
                    title->plotData[title->animStep].b);
                title->animStep++;
            }
            break;
        }

        case BAR:
        {
            RGBA color;
            while (animStepTarget > title->animStep)
            {
                title->animStep++;
                color = title->destImage->setColor(128, 0, 0); // dark red for the underline

                // blit bar to the canvas
                title->destImage->fillRect(
                    1,
                    1,
                    title->animStep,
                    1,
                    color.r,
                    color.g,
                    color.b);
            }
            break;
        }

        case AND:
        {
            // blit the entire src to the canvas
            title->srcImage->drawOn(title->destImage, 1, 1);
            title->animStep = title->animStepMax;
            break;
        }

        case ORIGIN:
        {
            if (bSkipTitles)
                title->animStep = title->animStepMax;
            else
            {
                title->animStep++;
                title->timeDelay = getTicks() - title->timeBase + 100;
            }

            // blit src to the canvas one row at a time, bottom up
            title->srcImage->drawSubRectOn(
                title->destImage,
                1,
                title->destImage->height() - 1 - title->animStep,
                0,
                0,
                title->srcImage->width(),
                title->animStep);
            break;
        }

        case PRESENT:
        {
            if (bSkipTitles)
                title->animStep = title->animStepMax;
            else
            {
                title->animStep++;
                title->timeDelay = getTicks() - title->timeBase + 100;
            }

            // blit src to the canvas one row at a time, top down
            title->srcImage->drawSubRectOn(
                title->destImage,
                1,
                1,
                0,
                title->srcImage->height() - title->animStep,
                title->srcImage->width(),
                title->animStep);
            break;
        }

        case TITLE:
        {
            if (title->animStep == 0 && !bSkipTitles) {
                // Begin sound on first frame of "Ultima IV".
                soundPlay(SOUND_TITLE_FADE);
            }

            // blit src to the canvas in a random pixelized manner
            title->animStep = animStepTarget;

            random_shuffle(title->plotData.begin(), title->plotData.end());
            title->destImage->fillRect(1, 1, title->rw, title->rh, 0, 0, 0);

            // @TODO: animStepTarget (for this loop) should not exceed
            // half of animStepMax.  If so, instead draw the entire
            // image, and then black out the necessary pixels.
            // this should speed the loop up at the end
            for (int i=0; i < animStepTarget; ++i)
            {
                title->destImage->putPixel(
                    title->plotData[i].x,
                    title->plotData[i].y,
                    title->plotData[i].r,
                    title->plotData[i].g,
                    title->plotData[i].b,
                    title->plotData[i].a);
            }

            // Re-draw the PRESENT area.
            title->srcImage->drawSubRectOn(title->destImage, 73, 1,
                72, 0, 58, 5);
            break;
        }

        case SUBTITLE:
        {
            if (bSkipTitles)
                title->animStep = title->animStepMax;
            else
            {
                title->animStep++;
                title->timeDelay = getTicks() - title->timeBase + 100;
            }

            // Blit src top & bottom halves so it expands horiz. center out.
            Image* src = title->srcImage;
            int h = src->height();
            int drawH = title->animStep;
            if (drawH <= h) {
                int y = int(title->rh / 2) + 2;
                int w = src->width();
                int botH = drawH / 2;
                int topH = drawH - botH;    // If odd, top gets extra row.
                src->drawSubRectOn(title->destImage, 1, y-topH, 0, 0, w, topH);
                src->drawSubRectOn(title->destImage, 1, y, 0, h-botH, w, botH);
            }
        }
            break;

        case MAP:
        {
            if (bSkipTitles)
                title->animStep = title->animStepMax;
            else
            {
                title->animStep++;
                title->timeDelay = getTicks() - title->timeBase + 100;
            }

            int step = (title->animStep == title->animStepMax ? title->animStepMax - 1 : title->animStep);

            // blit src to the canvas one row at a time, center out
            title->srcImage->drawSubRectOn(title->destImage,
                                    153-(step*8), 1,
                                    0, 0,
                                    (step+1) * 8, title->srcImage->height());
            title->srcImage->drawSubRectOn(title->destImage,
                                    161, 1,
                                    312-(step*8), 0,
                                    (step+1) * 8, title->srcImage->height());

#ifdef GPU_RENDER
            //printf( "KR reveal %d\n", step );
            if (step >= mapArea.columns)
                mapArea.scissor = NULL;
            else {
                int scale = screenState()->aspectH / U4_SCREEN_H;
                mapScissor[0] = scale * (160 - step * 8);   // Left
                mapScissor[1] = mapArea.screenRect[1];      // Bottom
                mapScissor[2] = scale * (step * 2 * 8);     // Width
                mapScissor[3] = mapArea.screenRect[3];      // Height
                mapArea.scissor = mapScissor;

                if (step == 1) {
                    MAP_ENABLE;
                }
            }
#else
            // create a destimage for the map tiles
            int newtime = getTicks();
            if (newtime > title->timeDuration + 250/4)
            {
                // draw the updated map display
                drawMapStatic();

                xu4.screenImage->drawSubRectOn(title->srcImage, 8, 8,
                                               8, 13*8, 38*8, 10*8);

                title->timeDuration = newtime + 250/4;
            }

            title->srcImage->drawSubRectOn(title->destImage,
                                           161 - (step * 8), 9,
                                           160 - (step * 8), 8,
                                           (step * 2) * 8, 10 * 8);
#endif
            break;
        }
    }

    // draw the titles
    drawTitle();

    // if the animation for this title has completed,
    // move on to the next title
    if (title->animStep >= title->animStepMax)
    {
        // free memory that is no longer needed
        compactTitle();
        title++;

        if (title == titles.end())
        {
            // reset the timer to the pre-titles granularity
            xu4.timerInterval = 1000 / xu4.settings->gameCyclesPerSecond;

            // make sure the titles only appear when the app first loads
            bSkipTitles = true;

            return false;
        }

        if (title->method == TITLE)
        {
            xu4.timerInterval = xu4.settings->titleSpeedRandom;
        }
        else if (title->method == MAP)
        {
            xu4.timerInterval = xu4.settings->titleSpeedOther;
        }
        else
        {
            xu4.timerInterval = xu4.settings->titleSpeedOther;
        }
    }

    return true;
}


//
// The title element has finished drawing all frames, so
// delete, remove, or free data that is no longer needed
//
void IntroController::compactTitle()
{
    if (title->srcImage)
    {
        delete title->srcImage;
        title->srcImage = NULL;
    }
    title->plotData.clear();
}


//
// Scale the animation canvas, then draw it to the screen
//
void IntroController::drawTitle()
{
    title->destImage->drawSubRect(title->rx, title->ry, 1, 1,
                                  title->rw, title->rh);
}


//
// skip the remaining titles
//
void IntroController::skipTitles()
{
    bSkipTitles = true;
    soundStop();
}

void* intro_enter(Stage* st, void*)
{
    INTRO_CON;

    xu4._stage = STAGE_INTRO;

    if (! ic)
        xu4.intro = ic = new IntroController;

    ic->init();
    ic->preloadMap();

    if (xu4.errorMessage)
        stage_run(STAGE_IERROR);

    return ic;
}

void intro_leave(Stage* st)
{
    INTRO_CON_ST;
    ic->deleteIntro();
}

int intro_dispatch(Stage* st, const InputEvent* ev)
{
    INTRO_CON_ST;
    switch (ev->type) {
        case IE_CYCLE_TIMER:
            ic->timerFired();
            break;

        case IE_KEY_PRESS:
            return ic->keyPressed(st, ev->n);

        case IE_MOUSE_PRESS:
            if (ic->mode == IntroController::INTRO_MENU && ev->n == CMOUSE_LEFT)
            {
                int cx, cy;
                ic->menuArea.mouseTextPos(ev->x, ev->y, cx, cy);

                // Matches text position in updateScreen().
                if (cx >= 10 && cx <= 28) {
                    if (cy >= 5 && cy <= 9) {
                        static const char menuKey[] = "rjica";
                        ic->keyPressed(st,  menuKey[cy - 5]);
                    }
                }
                return 1;
            }
            break;
    }
    return 0;
}
