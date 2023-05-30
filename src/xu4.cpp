/*
 * xu4.cpp
 */

/** \mainpage xu4 Main Page
 *
 * \section intro_sec Introduction
 *
 * intro stuff goes here...
 */

#include <cstring>
#include <ctime>
#include "xu4.h"
#include "config.h"
#include "error.h"
#include "game.h"
#include "gamebrowser.h"
#include "imagemgr.h"
#include "intro.h"
#include "progress_bar.h"
#include "screen.h"
#include "settings.h"
#include "sound.h"
#include "utils.h"

#ifdef ANDROID
extern "C" const char* androidInternalData();
#endif

#if defined(MACOSX)
#include "macosx/osxinit.h"
#endif

#ifdef _WIN32
#include "win32console.c"
#endif

#ifdef DEBUG
extern int gameSave(const char*);
#endif


#ifdef USE_BORON
#include <boron/boron.h>

// Using the Boron internal RNG functions for xu4.randomFx.
extern "C" {
void     well512_init(void* ws, uint32_t seed);
uint32_t well512_genU32(void* ws);
}
#else
#include "well512.h"
#endif

static void xu4_srandom(uint32_t);


enum OptionsFlag {
    OPT_FULLSCREEN = 1,
    OPT_NO_INTRO   = 2,
    OPT_NO_AUDIO   = 4,
    OPT_VERBOSE    = 8,
    OPT_FILTER     = 0x10,
    OPT_RECORD     = 0x20,
    OPT_REPLAY     = 0x40,
    OPT_TEST_SAVE  = 0x80
};

struct Options {
    uint16_t flags;
    uint16_t used;
    uint32_t scale;
    uint8_t  filter;
    const char* module;
    const char* profile;
    const char* recordFile;
};

#define strEqual(A,B)       (strcmp(A,B) == 0)
#define strEqualAlt(A,B,C)  (strEqual(A,B) || strEqual(A,C))

/*
 * Return non-zero if the program should continue.
 */
int parseOptions(Options* opt, int argc, char** argv) {
    int i;
    for (i = 0; i < argc; i++) {
        if (strEqual(argv[i], "--filter"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->filter = Settings::settingEnum(screenGetFilterNames(),argv[i]);
            opt->used |= OPT_FILTER;
        }
        else if (strEqualAlt(argv[i], "-s", "--scale"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->scale = strtoul(argv[i], NULL, 0);
        }
#ifdef CONF_MODULE
        else if (strEqualAlt(argv[i], "-m", "--module"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->module = argv[i];
        }
#endif
        else if (strEqualAlt(argv[i], "-p", "--profile"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->profile = argv[i];
        }
        else if (strEqualAlt(argv[i], "-i", "--skip-intro"))
        {
            opt->flags |= OPT_NO_INTRO;
            opt->used  |= OPT_NO_INTRO;
        }
        else if (strEqualAlt(argv[i], "-v", "--verbose"))
        {
            opt->flags |= OPT_VERBOSE;
            opt->used  |= OPT_VERBOSE;
        }
        else if (strEqualAlt(argv[i], "-f", "--fullscreen"))
        {
            opt->flags |= OPT_FULLSCREEN;
            opt->used  |= OPT_FULLSCREEN;
        }
        else if (strEqualAlt(argv[i], "-q", "--quiet"))
        {
            opt->flags |= OPT_NO_AUDIO;
            opt->used  |= OPT_NO_AUDIO;
        }
        else if (strEqualAlt(argv[i], "-h", "--help"))
        {
            printf("xu4: Ultima IV Recreated\n"
                   "v%s (%s)\n\n", VERSION, __DATE__ );
            printf(
            "Options:\n"
            "      --filter <string>   Specify display filtering mode.\n"
            "                          (point, HQX, xBR-lv2)\n"
            "  -f, --fullscreen        Run in fullscreen mode.\n"
            "  -h, --help              Print this message and quit.\n"
            "  -i, --skip-intro        Skip the intro. and load the last saved game.\n"
#ifdef CONF_MODULE
            "  -m, --module <file>     Specify game module (default is Ultima-IV).\n"
#endif
            "  -p, --profile <string>  Use another set of settings and save files.\n"
            "  -q, --quiet             Disable audio.\n"
            "  -s, --scale <int>       Specify display scaling factor (1-5).\n"
            "  -v, --verbose           Enable verbose console output.\n"
#ifdef DEBUG
            "\nDEBUG Options:\n"
            "  -c, --capture <file>    Record user input.\n"
            "  -r, --replay <file>     Play using recorded input.\n"
            "      --test-save         Save to /tmp/xu4/ and quit.\n"
#endif
            "\nHomepage: http://xu4.sourceforge.net\n");

            return 0;
        }
#ifdef DEBUG
        else if (strEqualAlt(argv[i], "-c", "--capture"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->recordFile = argv[i];
            opt->flags |= OPT_RECORD;
            opt->used  |= OPT_RECORD;
        }
        else if (strEqualAlt(argv[i], "-r", "--replay"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->recordFile = argv[i];
            opt->flags |= OPT_REPLAY;
            opt->used  |= OPT_REPLAY;
        }
        else if (strEqual(argv[i], "--test-save"))
        {
            opt->flags |= OPT_TEST_SAVE;
        }
#endif
        else {
            errorFatal("Unrecognized argument: %s\n\n"
                   "Use --help for a list of supported arguments.", argv[i]);
            return 0;
        }
    }
    return 1;

missing_value:
    errorFatal("%s requires a value. See --help for more detail.", argv[i-1]);
    return 0;
}

//----------------------------------------------------------------------------

static void initResourcePaths(StringTable* st) {
#ifdef ANDROID
    sst_init(st, 2, 64);
    sst_append(st, androidInternalData(), -1);
#else
    char* home;

    sst_init(st, 4, 32);

    sst_append(st, ".", 1);
#ifdef _WIN32
    home = getenv("LOCALAPPDATA");
    if (home && home[0])
        sst_appendCon(st, home, "\\xu4");
#else
    home = getenv("HOME");
    if (home && home[0]) {
#ifdef __APPLE__
        sst_appendCon(st, home, "/Library/Application Support/xu4");
#else
        sst_appendCon(st, home, "/.local/share/xu4");
#endif
    }
    sst_append(st, "/usr/share/xu4", -1);
    sst_append(st, "/usr/local/share/xu4", -1);
#endif
#endif
}

//----------------------------------------------------------------------------

#ifdef DEBUG
static void servicesFree(XU4GameServices*);
#endif

void servicesInit(XU4GameServices* gs, Options* opt) {
    gs->verbose = opt->flags & OPT_VERBOSE;

    initResourcePaths(&gs->resourcePaths);

    if (!u4fsetup())
    {
        errorFatal( "xu4 requires the PC version of Ultima IV to be present.\n"
            "\nIt may either be a zip file or subdirectory named \"ultima4\" in the same\n"
            "directory as the xu4 executable.\n"
            "\nFor more information visit http://xu4.sourceforge.net/faq.html\n");
    }

    /* Setup the message bus early to make it available to other services. */
    notify_init(&gs->notifyBus, 8);

    /* initialize the settings */
    gs->settings = new Settings;
    gs->settings->init(opt->profile);

    /* update the settings based upon command-line arguments */
    if (opt->used & OPT_FULLSCREEN)
        gs->settings->fullscreen = (opt->flags & OPT_FULLSCREEN) ? true : false;
    if (opt->scale)
        gs->settings->scale = opt->scale;
    if (opt->used & OPT_FILTER)
        gs->settings->filter = opt->filter;

    gs->config = configInit(opt->module ? opt->module : gs->settings->game,
                            gs->settings->soundtrack);
    screenInit(LAYER_COUNT);
    Tile::initSymbols(gs->config);

    if (! (opt->flags & OPT_NO_AUDIO))
        soundInit();

    gs->eventHandler = new EventHandler(1000/gs->settings->gameCyclesPerSecond);

    {
    uint32_t seed;

#ifdef DEBUG
    if (opt->flags & OPT_REPLAY) {
        seed = gs->eventHandler->replay(opt->recordFile);
        if (! seed) {
            servicesFree(gs);
            errorFatal("Cannot open recorded input from %s", opt->recordFile);
        }
        xu4_srandom(seed);
    } else if (opt->flags & OPT_RECORD) {
        seed = time(NULL);
        if (! gs->eventHandler->beginRecording(opt->recordFile, seed)) {
            servicesFree(gs);
            errorFatal("Cannot open recording file %s", opt->recordFile);
        }
        xu4_srandom(seed);
    } else
#endif
        seed = time(NULL);

    xu4_srandom(seed);
    well512_init(gs->randomFx, seed);
    }

    stage_runG( (opt->flags & OPT_NO_INTRO) ? STAGE_PLAY : STAGE_INTRO, NULL );
}

static void servicesFreeGame(XU4GameServices* gs) {
    delete gs->game;
    delete gs->intro;
    delete gs->saveGame;
    delete gs->eventHandler;

    gs->game = NULL;
    gs->intro = NULL;
    gs->saveGame = NULL;
    gs->eventHandler = NULL;

    soundDelete();
    screenDelete();
    configFree(gs->config);
}

static void servicesFree(XU4GameServices* gs) {
    servicesFreeGame(gs);

    delete gs->settings;
    notify_free(&gs->notifyBus);
    u4fcleanup();
    sst_free(&gs->resourcePaths);
}

static void servicesReset(XU4GameServices* gs) {
    servicesFreeGame(gs);

    const Settings* settings = gs->settings;
    gs->config = configInit(settings->game, settings->soundtrack);
    screenInit(LAYER_COUNT);
    Tile::initSymbols(gs->config);

    soundInit();

    gs->eventHandler = new EventHandler(1000/settings->gameCyclesPerSecond);

    uint32_t seed = time(NULL);
    xu4_srandom(seed);
    well512_init(gs->randomFx, seed);

    stage_runG(STAGE_INTRO, NULL);
}

XU4GameServices xu4;

//----------------------------------------------------------------------------

#include "stages_u4.cpp"
#include "support/getTicks.c"

static void frameSleepInit(FrameSleep* fs, int frameDuration) {
    fs->frameInterval = frameDuration;
    for (int i = 0; i < 8; ++i)
        fs->ftime[i] = frameDuration;
    fs->ftimeSum = frameDuration * 8;
    fs->ftimeIndex = 0;
    fs->fsleep = frameDuration - 6;
    fs->realTime = getTicks();
}

/*
 * Return non-zero if waitTime has been reached or passed.
 */
static int frameSleep(FrameSleep* fs, uint32_t waitTime) {
    uint32_t now;
    int32_t elapsed, elapsedLimit, frameAdjust;
    int i;

    now = getTicks();
    elapsed = now - fs->realTime;
    fs->realTime = now;

    elapsedLimit = fs->frameInterval * 8;
    if (elapsed > elapsedLimit)
        elapsed = elapsedLimit;

    i = fs->ftimeIndex;
    fs->ftimeSum += elapsed - fs->ftime[i];
    fs->ftime[i] = elapsed;
    fs->ftimeIndex = i ? i - 1 : FS_SAMPLES - 1;

    frameAdjust = int(fs->frameInterval) - fs->ftimeSum / FS_SAMPLES;
#if 0
    // Adjust by 1 msec.
    if (frameAdjust > 0) {
        if (fs->fsleep < fs->frameInterval - 1)
            ++fs->fsleep;
    } else if (frameAdjust < 0) {
        if (fs->fsleep)
            --fs->fsleep;
    }
#else
    // Adjust by 2 msec.
    if (frameAdjust > 0) {
        if (frameAdjust > 2)
            frameAdjust = 2;
        int sa = int(fs->fsleep) + frameAdjust;
        int limit = fs->frameInterval - 1;
        fs->fsleep = (sa > limit) ? limit : sa;
    } else if (frameAdjust < 0) {
        if (frameAdjust < -2)
            frameAdjust = -2;
        int sa = int(fs->fsleep) + frameAdjust;
        fs->fsleep = (sa < 0) ? 0 : sa;
    }
#endif

    if (waitTime && now >= waitTime)
        return 1;
#if 0
    printf("KR fsleep %d ela:%d avg:%d adj:%d\n",
           fs->fsleep, elapsed, fs->ftimeSum / FS_SAMPLES, frameAdjust);
#endif
    if (fs->fsleep)
        msecSleep(fs->fsleep);
    return 0;
}

void mainLoop(MainLoop* lp)
{
    while (stage_transition()) {
        // Perform stage updates.
        if (lp->runTime >= xu4.timerInterval) {
            lp->runTime -= xu4.timerInterval;
            lp->gameCycle = 1;
        } else {
            lp->gameCycle = 0;
        }
        stage_tick(lp->gameCycle);

        lp->runTime += lp->fs.frameInterval;
        xu4.rclock  += lp->fs.frameInterval;

        screenSwapBuffers();
        frameSleep(&lp->fs, 0);

        // Handle input (which may queue a new stage).
        screenHandleEvents(NULL /*updateScreen*/);
#ifdef DEBUG
        xu4.eventHandler->emitRecordedKeys();
#endif
    }
}

int main(int argc, char *argv[])
{
#if defined(MACOSX)
    osxInit(argv[0]);
#endif
#ifdef _WIN32
    redirectIOToConsole();
#endif
    //printf("sizeof(Tile) %ld\n", sizeof(Tile));

    {
    Options opt;

    /* Parse arguments before setup in case the user only wants some help. */
    memset(&opt, 0, sizeof opt);
    if (! parseOptions(&opt, argc-1, argv+1))
        return 0;

    stage_init(stagesU4);

    memset(&xu4, 0, sizeof xu4);
    servicesInit(&xu4, &opt);

#ifdef DEBUG
    if (opt.flags & OPT_TEST_SAVE) {
        int status;
        xu4.game = new GameController();
        if (xu4.game->initContext()) {
            gameSave("/tmp/xu4/");
            status = 0;
        } else {
            printf("initContext failed!\n");
            status = 1;
        }
        servicesFree(&xu4);
        return status;
    }
#endif
    }

#if 1
    // Modern systems are fast enough that a progress bar isn't really needed
    // but it can be useful during development to see something on screen
    // right away.

    ProgressBar pb((320/2) - (200/2), (200/2), 200, 10, 0, 2);
    pb.setBorderColor(240, 240, 240);
    pb.setColor(0, 0, 128);
    pb.setBorderWidth(1);
    screenTextAt(15, 11, "Loading...");
    ++pb;
#endif

begin_game:
    xu4.loop.runTime = 0;
    xu4.timerInterval = 1000/xu4.settings->gameCyclesPerSecond,
    frameSleepInit(&xu4.loop.fs,
                        1000/xu4.settings->screenAnimationFramesPerSecond);

    mainLoop(&xu4.loop);

    if (xu4.gameReset) {
        xu4.gameReset = 0;
        xu4.rclock = 0;
        servicesReset(&xu4);
        goto begin_game;
    }

    servicesFree(&xu4);
    return 0;
}

/**
 * Set the group that loaded assets will belong to.
 * Return the previously set group.
 */
uint16_t xu4_setResourceGroup(uint16_t group) {
    uint16_t prev = xu4.resGroup;
    xu4.resGroup = group;
    return prev;
}

/**
 * Free all assets that are part of the specified group.
 */
void xu4_freeResourceGroup(uint16_t group) {
    xu4.imageMgr->freeResourceGroup(group);
    soundFreeResourceGroup(group);
}

//----------------------------------------------------------------------------

/*
 * Seed the random number generator.
 */
static void xu4_srandom(uint32_t seed) {
#ifdef USE_BORON
    // Compiled code and module scripts share this generator.
    boron_randomSeed(xu4.config->boronThread(), seed);
#elif (defined(BSD) && (BSD >= 199103)) || (defined (MACOSX) || defined (IOS))
    srandom(seed);
#else
    srand(seed);
#endif
}

#ifdef REPORT_RNG
char rpos = '-';
#endif

extern "C" {

/*
 * Generate a random number between 0 and (upperRange - 1).
 */
int xu4_random(int upperRange) {
#ifdef USE_BORON
    if (upperRange < 2)
        return 0;
#ifdef REPORT_RNG
    uint32_t r = boron_random(xu4.config->boronThread());
    uint32_t n = r % upperRange;
    printf( "KR rn %d %d %c\n", r, n, rpos);
    return n;
#else
    return boron_random(xu4.config->boronThread()) % upperRange;
#endif
#else
#if (defined(BSD) && (BSD >= 199103)) || (defined (MACOSX) || defined (IOS))
    int r = random();
#else
    int r = rand();
#endif
    return (int) ((((double)upperRange) * r) / (RAND_MAX+1.0));
#endif
}

/*
 * Generate a random number between 0 and (upperRange - 1).
 *
 * This function is used by audio & visual effects that do not alter the
 * game simulation state in any way.  Having a separate generator allows for
 * a reproducibile game state (i.e. recording playback to work) even if sound
 * or graphics elements change between runs.
 */
int xu4_randomFx(int n) {
    return (n < 2) ? 0 : well512_genU32(xu4.randomFx) % n;
}

// Using randomFx for tile frame animation.
#define CONFIG_ANIM_RANDOM
#define anim_random xu4_randomFx
#include "anim.c"

}
