/*
 * $Id$
 */

#ifndef INTRO_H
#define INTRO_H

#include <string>
#include <vector>

#include "menu.h"
#include "savegame.h"
#include "imageview.h"
#include "stage.h"
#include "textview.h"
#include "tileview.h"

#ifdef GPU_RENDER
#include "map.h"
#endif

class IntroObjectState;
class Tile;

/**
 * Binary data loaded from the U4DOS title.exe file.
 */
class IntroBinData {
public:
    const static int INTRO_TEXT_OFFSET;
    const static int INTRO_MAP_OFFSET;
    const static int INTRO_FIXUPDATA_OFFSET;
    const static int INTRO_SCRIPT_TABLE_SIZE;
    const static int INTRO_SCRIPT_TABLE_OFFSET;
    const static int INTRO_BASETILE_TABLE_SIZE;
    const static int INTRO_BASETILE_TABLE_OFFSET;
    const static int BEASTIE1_FRAMES;
    const static int BEASTIE2_FRAMES;
    const static int BEASTIE_FRAME_TABLE_OFFSET;
    const static int BEASTIE1_FRAMES_OFFSET;
    const static int BEASTIE2_FRAMES_OFFSET;

    IntroBinData();
    ~IntroBinData();

    bool load();

#ifdef GPU_RENDER
    Map introMap;
#else
    std::vector<MapTile> introMap;
#endif
    unsigned char *sigData;
    unsigned char *scriptTable;
    const Tile **baseTileTable;
    unsigned char *beastie1FrameTable;
    unsigned char *beastie2FrameTable;
    std::vector<std::string> introText;
    std::vector<std::string> introQuestions;
    std::vector<std::string> introGypsy;

private:
    // disallow assignments, copy contruction
    IntroBinData(const IntroBinData&);
    const IntroBinData &operator=(const IntroBinData&);
};

struct SettingsMenus;
struct GateAnimator;

/**
 * Controls the title animation sequences, including the traditional
 * plotted "Lord British" signature, the pixelized fade-in of the
 * "Ultima IV" game title, as well as the other more simple animated
 * features, followed by the traditional animated map and "Journey
 * Onward" menu, plus the xU4-specific configuration menu.
 *
 * @todo
 * <ul>
 *      <li>make initial menu a Menu too</li>
 *      <li>get rid of mode and switch(mode) statements</li>
 *      <li>get rid global intro instance -- should only need to be accessed from u4.cpp</li>
 * </ul>
 */
class IntroController {
public:
    IntroController();
    ~IntroController();

    bool hasInitiatedNewGame();

    bool keyPressed(Stage*, int key);
    unsigned char *getSigData();
    void updateScreen();
    void timerFired();

//STAGE private:
    bool init();
    void preloadMap();
    void deleteIntro();

    void initTitles();
    bool updateTitle();

    void drawMap();
    void drawMapStatic();
    void drawMapAnimated();
    void drawBeasties();
    void drawBeastie(int beast, int vertoffset, int frame);
    void drawCard(int pos, int card, const uint8_t* origin);
    void drawAbacusBeads(int row, int selectedVirtue, int rejectedVirtue);

    void initQuestionTree();
    bool doQuestion(int answer);
    void initPlayers(SaveGame *saveGame);
    std::string getQuestion(int v1, int v2);
    void showStory(Stage*, int storyInd);
    void showQuestion(Stage*, int state);
    const char* createSaveGame();
    void journeyOnward(Stage*);
#ifdef GPU_RENDER
    void enableMap();
#endif
    void showText(const string &text);

    /**
     * The states of the intro.
     */
    enum {
        INTRO_TITLES,                       // displaying the animated intro titles
        INTRO_MAP,                          // displaying the animated intro map
        INTRO_MENU                          // displaying the main menu: journey onward, etc.
    } mode;

    ImageView backgroundArea;
    TextView menuArea;
    TextView questionArea;
    TileView mapArea;

    // menus
    SettingsMenus* menus;

    // data loaded in from title.exe
    IntroBinData *binData;

    // additional introduction state data
    int dstate;             // Stage dispatch state.
    int answerInd;
    int questionRound;
    int questionTree[15];
    int beadSub[3];
    int beastie1Cycle;
    int beastie2Cycle;
    int beastieOffset;
    int beastieSub[2];
    bool beastiesVisible;
    int sleepCycles;
    int scrPos;  /* current position in the script table */
#ifdef GPU_RENDER
    int mapScissor[4];
#else
    IntroObjectState *objectStateTable;
#endif
    ImageInfo* beastiesImg;
    ImageInfo* abacusImg;

    bool justInitiatedNewGame;

    //
    // Title defs, structs, methods, and data members
    //
    enum AnimType {
        SIGNATURE,
        AND,
        BAR,
        ORIGIN,
        PRESENT,
        TITLE,
        SUBTITLE,
        MAP
    };

    struct AnimPlot {
        uint8_t x, y;
        uint8_t r, g, b, a;
    };

    struct AnimElement {
        int rx, ry;                         // screen/source x and y
        int rw, rh;                         // source width and height
        AnimType method;                    // render method
        int animStep;                       // tracks animation position
        int animStepMax;
        int timeBase;                       // initial animation time
        int timeDelay;                      // delay before rendering begins
        int timeDuration;                   // total animation time
        Image *srcImage;                    // storage for the source image
        Image *destImage;                   // storage for the animation frame
        std::vector <AnimPlot> plotData;    // plot data
    };

    void addTitle(int x, int y, int w, int h, AnimType method, int delay, int duration);
    void compactTitle();
    void drawTitle();
    void getTitleSourceData();
    void skipTitles();
    std::vector<AnimElement> titles;            // list of title elements
    std::vector<AnimElement>::iterator title;   // current title element

    // newgame
    InputString inString;
    std::string nameBuffer;
    int  sex;

    // story
    uint16_t saveGroup;
    GateAnimator* gateAnimator;

    // gypsy
    uint8_t* origin;

    int  introMusic;
    bool bSkipTitles;
    bool egaGraphics;
};

#endif
