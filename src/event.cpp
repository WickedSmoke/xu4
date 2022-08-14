/*
 * event.cpp
 */

#include <cassert>
#include <cctype>
#include <cstring>
#include <list>

#include "event.h"

#include "config.h"
#include "context.h"
#include "location.h"
#include "savegame.h"
#include "screen.h"
#include "sound.h"
#include "textview.h"
#include "u4.h"
#include "xu4.h"


/**
 * Constructs the event handler object.
 */
EventHandler::EventHandler(int gameCycleDuration) :
    timerInterval(gameCycleDuration),
    updateScreen(NULL)
{
    paused = false;
    anim_init(&flourishAnim, 64, NULL, NULL);
    anim_init(&fxAnim, 32, NULL, NULL);

#ifdef DEBUG
    recordFP = -1;
    recordMode = 0;
#endif
}

EventHandler::~EventHandler() {
#ifdef DEBUG
    endRecording();
#endif
    anim_free(&flourishAnim);
    anim_free(&fxAnim);
}

/*
 * The x,y values are window coordinates
 */
const MouseArea* EventHandler::mouseAreaForPoint(const MouseArea* areas,
                                                 int count, int x, int y) const
{
    if (areas) {
        screenPointToMouseArea(&x, &y);
        for (int i = 0; i < count; ++areas, ++i) {
            if (pointInMouseArea(x, y, areas))
                return areas;
        }
    }
    return NULL;
}

void EventHandler::setScreenUpdate(void (*updateFunc)(void)) {
    updateScreen = updateFunc;
}

#if 1
void EventHandler::togglePause() {}
bool EventHandler::runPause() { return false; }
#else
#define GPU_PAUSE

void EventHandler::togglePause() {
    if (paused) {
        paused = false;
    } else {
#ifdef GPU_PAUSE
        // Don't pause if Game Browser (or other LAYER_TOP_MENU user) is open.
        if (screenLayerUsed(LAYER_TOP_MENU))
            return;
#endif
        paused = true;
        // Set ended to break the run loop.  runPause() resets it so that
        // the game does not end.
        ended = true;
    }
}

#include "support/getTicks.c"

#ifdef GPU_PAUSE
#include "gpu.h"
#include "gui.h"

static void renderPause(ScreenState* ss, void* data)
{
    gpu_drawGui(xu4.gpu, GPU_DLIST_GUI);
}
#endif

// Return true if game should end.
bool EventHandler::runPause() {
    Controller waitCon;

    soundSuspend(1);
    screenSetMouseCursor(MC_DEFAULT);
#ifdef GPU_PAUSE
    static uint8_t pauseGui[] = {
        LAYOUT_V, BG_COLOR_CI, 128,
        MARGIN_V_PER, 42, FONT_VSIZE, 38, LABEL_DT_S,
        LAYOUT_END
    };
    const void* guiData[1];
    guiData[0] = "\x13\xAPaused";
    TxfDrawState ds;
    ds.fontTable = screenState()->fontTable;
    float* attr = gui_layout(GPU_DLIST_GUI, NULL, &ds, pauseGui, guiData);
    gpu_endTris(xu4.gpu, GPU_DLIST_GUI, attr);

    screenSetLayer(LAYER_TOP_MENU, renderPause, this);
#else
    screenTextAt(16, 12, "( Paused )");
    screenUploadToGPU();
#endif
    screenSwapBuffers();

    ended = false;
    do {
        msecSleep(333);
        handleInputEvents(&waitCon, NULL);
    } while (paused && ! ended);

#ifdef GPU_PAUSE
    screenSetLayer(LAYER_TOP_MENU, NULL, NULL);
#endif
    soundSuspend(0);
    return ended;
}
#endif

/**
 * Delays program execution for the specified number of milliseconds.
 * This doesn't actually stop events, but it stops the user from interacting
 * while some important event happens (e.g., getting hit by a cannon ball or
 * a spell effect).
 *
 * This method is not expected to handle msec values of less than the display
 * refresh interval.
 *
 * \return true if game should exit.
 */
bool EventHandler::wait_msecs(unsigned int msec) {
    stage_runG(STAGE_WAIT, &msec);
    mainLoop(&xu4.loop);
    return ! stage_current();   // TODO: Test this.
}

/*
 * STAGE_WAIT
 */
void* wait_enter(Stage* st, void* args)
{
    stage_startMsecTimer(st, *((unsigned int*) args) );
    return NULL;
}

int wait_dispatch(Stage* st, const InputEvent* ev)
{
    if (ev->type == IE_MSEC_TIMER) {
        stage_abort();
        return 1;
    }
    return 0;
}

//----------------------------------------------------------------------------

#ifdef DEBUG
#include <fcntl.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define close   _close
#define read    _read
#define write   _write
#else
#include <unistd.h>
#endif

#ifndef CDI32
#include "cdi.h"
#endif

#define RECORD_CDI  CDI32(0xDA,0x7A,0x4F,0xC0)
#define HDR_SIZE    8

enum RecordMode {
    MODE_DISABLED,
    MODE_RECORD,
    MODE_REPLAY,
};

enum RecordCommand {
    RECORD_NOP,
    RECORD_KEY,
    RECORD_KEY1,
    RECORD_END = 0xff
};

struct RecordKey {
    uint8_t op, key;
    uint16_t delay;
};

bool EventHandler::beginRecording(const char* file, uint32_t seed) {
    uint32_t head[2];

    recordClock = recordLast = 0;
    recordMode = MODE_DISABLED;

    if (recordFP >= 0)
        close(recordFP);
#ifdef _WIN32
    recordFP = _open(file, _O_WRONLY | _O_CREAT, _S_IWRITE);
#else
    recordFP = open(file, O_WRONLY | O_CREAT,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
    if (recordFP < 0)
        return false;

    head[0] = RECORD_CDI;
    head[1] = seed;
    if (write(recordFP, head, HDR_SIZE) != HDR_SIZE)
        return false;
    recordMode = MODE_RECORD;
    return true;
}

/**
 * Stop either recording or playback.
 */
void EventHandler::endRecording() {
    if (recordFP >= 0) {
        if (recordMode == MODE_RECORD) {
            char op = RECORD_END;
            write(recordFP, &op, 1);
        }
        close(recordFP);
        recordFP = -1;
        recordMode = MODE_DISABLED;
    }
}

//void EventHandler::recordMouse(int x, int y, int button) {}

void EventHandler::recordKey(int key) {
    if (recordMode == MODE_RECORD) {
        RecordKey rec;
        rec.op    = (key > 0xff) ? RECORD_KEY1 : RECORD_KEY;
        rec.key   = key & 0xff;
        rec.delay = recordClock - recordLast;

        recordLast = recordClock;
        write(recordFP, &rec, 4);
    }
}

/**
 * Update for both recording and playback modes.
 *
 * \return XU4 key code or zero if no key was pressed. When recording, a zero
 *         is always returned.
 */
int EventHandler::recordedKey() {
    int key = 0;
    if (recordMode == MODE_REPLAY) {
        if (replayKey) {
            if (recordClock >= recordLast) {
                key = replayKey;
                replayKey = 0;
            }
        } else {
            RecordKey rec;
            if (read(recordFP, &rec, 4) == 4 &&
                (rec.op == RECORD_KEY || rec.op == RECORD_KEY1)) {
                int fullKey = rec.key;
                if (rec.op == RECORD_KEY1)
                    fullKey |= 0x100;

                if (rec.delay)
                    replayKey = fullKey;
                else
                    key = fullKey;
                recordLast = recordClock + rec.delay;
            } else {
                endRecording();
            }
        }
    }

    return key;
}

/**
 * Begin playback from recorded input file.
 *
 * \return Random seed or zero if the recording file could not be opened.
 */
uint32_t EventHandler::replay(const char* file) {
    uint32_t head[2];

    recordClock = recordLast = 0;
    recordMode = MODE_DISABLED;
    replayKey = 0;

    if (recordFP >= 0)
        close(recordFP);
#ifdef _WIN32
    recordFP = _open(file, _O_RDONLY);
#else
    recordFP = open(file, O_RDONLY);
#endif
    if (recordFP < 0)
        return 0;
    if (read(recordFP, head, HDR_SIZE) != HDR_SIZE || head[0] != RECORD_CDI)
        return 0;

    recordMode = MODE_REPLAY;
    return head[1];
}

/*
 * Call once per frame after normal event processing.
 */
void EventHandler::emitRecordedKeys() {
    InputEvent event;
    int key;
    Stage* st = stage_current();

    while ((key = recordedKey())) {
        event.type = IE_KEY_PRESS;
        event.n    = key;
        if (st->dispatch(st, &event) && updateScreen)
            (*updateScreen)();
    }
    recordTick();
}
#endif

//----------------------------------------------------------------------------

#define TEST_BIT(bits, c)   (bits[c >> 3] & 1 << (c & 7))
#define MAX_BITS    128

static void addCharBits(uint8_t* bits, const char* cp) {
    int c;
    while ((c = *cp++))
        bits[c >> 3] |= 1 << (c & 7);
}

// Set 0-9, A-Z, a-z, backspace, new line, carriage return, & space.
static const uint8_t alphaNumBitset[MAX_BITS/8] = {
    0x00, 0x25, 0x00, 0x00, 0x01, 0x00, 0xFF, 0x03,
    0xFE, 0xFF, 0xFF, 0x07, 0xFE, 0xFF, 0xFF, 0x07
};

/**
 * Read a string at the screen cursor, terminated by the enter key.
 */
const char* EventHandler::readString(int maxlen, const char* extraChars) {
    InputString ctrl;
    input_stringInitView(&ctrl, maxlen, NULL, extraChars);

    stage_runG(STAGE_READS, &ctrl);
    mainLoop(&xu4.loop);

    char* buf = xu4.eventHandler->readStringBuf;
    memcpy(buf, ctrl.value, ctrl.used + 1);
    return buf;
}

/**
 * Read an integer at the screen cursor, terminated by the enter key.
 * Non-numeric keys are ignored.
 */
int EventHandler::readInt(int maxlen) {
    InputString ctrl;
    input_stringInitView(&ctrl, maxlen, NULL, NULL);

    memset(ctrl.accepted, 0, MAX_BITS/8);
    addCharBits(ctrl.accepted, "0123456789 \n\r\010");

    stage_runG(STAGE_READS, &ctrl);
    mainLoop(&xu4.loop);
    return (int) strtol(ctrl.value, NULL, 10);
}

/*
 * STAGE_READS
 */
int reads_dispatch(Stage* st, const InputEvent* ev)
{
    if (input_stringDispatch((InputString*) st->data, ev) != INPUT_WAITING)
        stage_abort();
    return 1;
}

//----------------------------------------------------------------------------

struct ReadChoice {
    const char* choices;
    int value;
};

/*
 * Read a single key from a provided list.
 */
char EventHandler::readChoice(const char* choices) {
    ReadChoice rc;
    rc.choices = choices;
    rc.value = 0;

    stage_runG(STAGE_READC, &rc);
    mainLoop(&xu4.loop);
    return rc.value;
}

/*
 * STAGE_READC
 */
int readc_dispatch(Stage* st, const InputEvent* ev)
{
    ReadChoice* rc = (ReadChoice*) st->data;
    if (input_choiceDispatch(rc->choices, ev, &rc->value) == INPUT_DONE) {
        stage_abort();
        return 1;
    }
    return 0;
}

//----------------------------------------------------------------------------

/*
 * Read a direction entered with the arrow keys.
 */
Direction EventHandler::readDir() {
    Direction pick = DIR_NONE;
    stage_runG(STAGE_READDIR, &pick);
    mainLoop(&xu4.loop);
    return pick;
}

/*
 * STAGE_READDIR
 */
int readdir_dispatch(Stage* st, const InputEvent* ev) {
    if (ev->type == IE_KEY_PRESS) {
        int key = ev->n;
        Direction* pickPtr = (Direction*) st->data;
        Direction d = keyToDirection(key);

        switch(key) {
        case U4_ESC:
        case U4_SPACE:
        case U4_ENTER:
            *pickPtr = DIR_NONE;
            stage_abort();
            break;

        default:
            if (d != DIR_NONE) {
                *pickPtr = d;
                stage_abort();
            }
            break;
        }
        return 1;
    }
    return 0;
}

//----------------------------------------------------------------------------

void EventHandler::waitAnyKey() {
    stage_runG(STAGE_ANYKEY, NULL);
    mainLoop(&xu4.loop);
}

// Wait briefly (10 seconds) for a key press.
void EventHandler::waitAnyKeyTimeout() {
    int timeout = 10000;
    stage_runG(STAGE_ANYKEY, &timeout);
    mainLoop(&xu4.loop);
}

/*
 * STAGE_ANYKEY
 */
void* anykey_enter(Stage* st, void* args) {
    if (args)
        stage_startMsecTimer(st, *((int*) args));
    return NULL;
}

int anykey_dispatch(Stage* st, const InputEvent* ev) {
    if (ev->type == IE_KEY_PRESS || ev->type == IE_MSEC_TIMER)
        stage_abort();
    return 0;
}

//----------------------------------------------------------------------------

/**
 * Read a player number.
 * Return -1 if none is selected.
 */
int EventHandler::choosePlayer() {
    ReadChoice rc;
    rc.choices = "12345678 \033\n";
    rc.value   = 0;
    stage_runG(STAGE_READC, &rc);
#ifdef IOS
    U4IOS::beginCharacterChoiceDialog();
#endif
    mainLoop(&xu4.loop);
#ifdef IOS
    U4IOS::endCharacterChoiceDialog();
#endif
    if (rc.value < '1' || rc.value > ('0' + c->saveGame->members))
        return -1;
    return rc.value - '1';
}

//----------------------------------------------------------------------------

struct AlphaAction {
    int value;
    char lastValidLetter;
    const char* prompt;
};

/**
 * Handle input for commands requiring a letter argument in the
 * range 'a' - lastValidLetter.
 */
int EventHandler::readAlphaAction(char lastValidLetter, const char* prompt) {
    AlphaAction ctrl;
    ctrl.value = 0;
    ctrl.lastValidLetter = lastValidLetter;
    ctrl.prompt = prompt;

    stage_runG(STAGE_READAA, &ctrl);
    mainLoop(&xu4.loop);
    return ctrl.value;
}

/*
 * STAGE_READAA
 */
int readaa_dispatch(Stage* st, const InputEvent* ev)
{
    if (ev->type == IE_KEY_PRESS) {
        AlphaAction* aa = (AlphaAction*) st->data;
        int key = ev->n;
        if (islower(key))
            key = toupper(key);

        if (key >= 'A' && key <= toupper(aa->lastValidLetter)) {
            screenMessage("%c\n", key);
            aa->value = key - 'A';
            stage_abort();
        } else if (key == U4_SPACE || key == U4_ESC || key == U4_ENTER) {
            screenMessage("\n");
            aa->value = -1;
            stage_abort();
        } else {
            screenMessage("\n%s", aa->prompt);
        }
        return 1;
    }
    return 0;
}

//----------------------------------------------------------------------------

/**
 * Handles any and all keystrokes.
 * Generally used to exit the application, switch applications,
 * minimize, maximize, etc.
 */
bool EventHandler::globalKeyHandler(int key) {
    switch(key) {
    case U4_PAUSE:
    case U4_ALT + 'p':
        xu4.eventHandler->togglePause();
        return true;

    case U4_ESC:
        stage_runG(STAGE_BROWSER, NULL);
        break;

#ifdef DEBUG
    case '`':
        if (c && c->location) {
            const Location* loc = c->location;
            const Tile* tile = loc->map->tileTypeAt(loc->coords, WITH_OBJECTS);
            printf("x = %d, y = %d, level = %d, tile = %d (%s)\n",
                    loc->coords.x, loc->coords.y, loc->coords.z,
                    xu4.config->usaveIds()->ultimaId( MapTile(tile->id) ),
                    tile->nameStr());
        }
        break;
#endif

#if defined(MACOSX)
    case U4_META + 'q': /* Cmd+q */
    case U4_META + 'x': /* Cmd+x */
#endif
    case U4_ALT + 'x': /* Alt+x */
#if defined(WIN32)
    case U4_ALT + U4_FKEY + 3:
#endif
        stage_exit();
        break;

    default:
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------

static void soundInvalidInput() {
    soundPlay(SOUND_BLOCKED);
}

/*
 * \param in            State to be initialized.
 * \param view          View to draw text & cursor in.  May be NULL.
 * \param extraChars    Other characters to accept.
 */
void input_stringInitView(InputString* in, int maxlen, TextView* view,
                          const char* extraChars)
{
    const int avail = sizeof(InputString::value) - 1;
    if (maxlen > avail)
        maxlen = avail;

    in->value[0] = '\0';
    in->used    = 0;
    in->maxlen  = maxlen;

    if (view) {
        in->screenX = view->cursorX();
        in->screenY = view->cursorY();
    } else {
        const ScreenState* ss = screenState();
        in->screenX = ss->cursorX;
        in->screenY = ss->cursorY;
        /*
        in->screenX = TEXT_AREA_X + c->col;
        in->screenY = TEXT_AREA_Y + c->line;
        */
    }
    in->view = view;

    memcpy(in->accepted, alphaNumBitset, MAX_BITS/8);
    if (extraChars)
        addCharBits(in->accepted, extraChars);
}

int input_stringDispatch(InputString* in, const InputEvent* ev)
{
    if (ev->type != IE_KEY_PRESS)
        return INPUT_WAITING;

    int key = ev->n;
    if (key < MAX_BITS && TEST_BIT(in->accepted, key)) {
        TextView* view = in->view;
        int len = in->used;
        int endX = in->screenX + len;

        if (key == U4_BACKSPACE) {
            if (len > 0) {
                /* remove the last character */
                --in->used;
                in->value[in->used] = '\0';

                if (view) {
                    view->textAt(endX, in->screenY, " ");
                    view->setCursorPos(endX - 1, in->screenY);
                } else {
                    screenHideCursor();
                    screenTextAt(endX, in->screenY, " ");
                    screenSetCursorPos(endX, in->screenY);
                    screenShowCursor();
                }
            } else
                soundInvalidInput();
        }
        else if (key == '\n' || key == '\r') {
            return INPUT_DONE;
        }
        else if (key == U4_ESC) {
            in->used = 0;
            in->value[0] = '\0';
            return INPUT_CANCEL;
        }
        else if (len < in->maxlen) {
            /* add a character to the end */
            in->value[len] = key;
            in->value[len+1] = '\0';
            ++in->used;

            if (view) {
                char str[2];
                str[0] = key;
                str[1] = '\0';
                view->textAt(endX, in->screenY, str);
                view->setCursorPos(endX + 1, in->screenY);
            } else {
                screenHideCursor();
                screenTextAt(endX, in->screenY, "%c", key);
                screenSetCursorPos(endX + 1, in->screenY);
                c->col = len + 1;
                screenShowCursor();
            }
        }
        else
            soundInvalidInput();
    } else {
        soundInvalidInput();
    }
    return INPUT_WAITING;
}

//----------------------------------------------------------------------------

/*
 * \param choices   A lowercase string of characters to accept.
 *
 * \return INPUT_WAITING or INPUT_DONE when a valid choice is made.
 */
int input_choiceDispatch(const char* choices, const InputEvent* ev, int* value)
{
    if (ev->type != IE_KEY_PRESS)
        return INPUT_WAITING;

    int key = ev->n;
    // isupper() accepts 1-byte characters, yet the modifier keys
    // (ALT, SHIFT, ETC) produce values beyond 255
    if ((key <= 0x7F) && (isupper(key)))
        key = tolower(key);

    if (strchr(choices, key)) {
        // If the value is printable, display it
        const ScreenState* ss = screenState();
        if (ss->cursorVisible && key > ' ' && key <= 0x7F)
            screenShowChar(toupper(key), ss->cursorX, ss->cursorY);

        *value = key;
        return INPUT_DONE;
    }

    return INPUT_WAITING;
}

/*
 * \return INPUT_WAITING or INPUT_DONE when a key or button is pressed.
 */
int input_anykey(const InputEvent* ev)
{
    switch (ev->type) {
        case IE_KEY_PRESS:
            return INPUT_DONE;
        case IE_MOUSE_PRESS:
            if (ev->n == CMOUSE_LEFT)
                return INPUT_DONE;
            break;
    }
    return INPUT_WAITING;
}
