/*
 * event.h
 */

#ifndef EVENT_H
#define EVENT_H

#include <cstddef>

#include "anim.h"
#include "types.h"

#define U4_UP           '['
#define U4_DOWN         '/'
#define U4_LEFT         ';'
#define U4_RIGHT        '\''
#define U4_BACKSPACE    8
#define U4_TAB          9
#define U4_SPACE        ' '
#define U4_ESC          27
#define U4_ENTER        13

// Key modifier masks.
#define U4_ALT          0x080
#define U4_META         0x200   // FIXME: Not handled by RecordCommand

// The following are 0x100 + USB HID Usage Ids.
#define U4_FKEY         0x13a
#define U4_PAUSE        0x148
#define U4_KEYPAD_ENTER 0x158

enum InputEventType {
    IE_CYCLE_TIMER,
    IE_MSEC_TIMER,
    IE_FRAME_UPDATE,
    IE_KEY_PRESS,
    IE_MOUSE_MOVE,
    IE_MOUSE_PRESS,
    IE_MOUSE_RELEASE,
    IE_MOUSE_WHEEL
};

enum MouseButton {
    CMOUSE_LEFT = 1,
    CMOUSE_MIDDLE,
    CMOUSE_RIGHT
};

struct InputEvent {
    uint16_t type;      // InputEventType
    uint16_t n;         // Button id or Key
    uint16_t state;     // Button mask
    int16_t  x, y;      // Axis value
};

//----------------------------------------------------------------------------

enum InputResult {
    INPUT_WAITING,
    INPUT_DONE,
    INPUT_CANCEL
};

class TextView;

struct InputString {
    char value[24];
    int used;
    int maxlen, screenX, screenY;
    TextView* view;
    uint8_t accepted[16];   // Character bitset.
};

void input_stringInitView(InputString*, int maxlen, TextView *view,
                          const char *extraChars = NULL);
int  input_stringDispatch(InputString*, const InputEvent*);

int  input_choiceDispatch(const char* choices, const InputEvent*, int* value);
int  input_anykey(const InputEvent* ev);

//----------------------------------------------------------------------------

typedef void(*updateScreenCallback)(void);

struct MouseArea;

/**
 * A class for handling game events.
 */
class EventHandler {
public:
    /* Static functions */
    static bool globalKeyHandler(int key);

    /* Blocking user input functions. Calling an input_* function from
       Stage::dispatch is preferred to avoid recursion of mainLoop(). */
    static int       choosePlayer();
    static int       readAlphaAction(char letter, const char* prompt);
    static char      readChoice(const char* choices);
    static Direction readDir();
    static int       readInt(int maxlen);
    static const char* readString(int maxlen, const char *extraChars = NULL);
    static void waitAnyKey();
    static void waitAnyKeyTimeout();
    static bool wait_msecs(unsigned int msecs);

    /* Constructors */
    EventHandler(int gameCycleDuration);
    ~EventHandler();

    /* Event functions */
    void setScreenUpdate(updateScreenCallback);
    void togglePause();

    /* Mouse area functions */
    const MouseArea* mouseAreaForPoint(const MouseArea* areas, int count,
                                       int x, int y) const;

#ifdef DEBUG
    bool beginRecording(const char* file, uint32_t seed);
    void endRecording();
    void recordKey(int key);
    int  recordedKey();
    void recordTick() { ++recordClock; }
    uint32_t replay(const char* file);
    void emitRecordedKeys();
#endif

    void advanceFlourishAnim() {
        anim_advance(&flourishAnim, float(timerInterval) * 0.001f);
    }

    Animator flourishAnim;
    Animator fxAnim;

protected:
    bool runPause();

    uint32_t timerInterval;     // Milliseconds between timedEvents ticks.
#ifdef DEBUG
    int recordFP;
    int recordMode;
    int replayKey;
    uint32_t recordClock;
    uint32_t recordLast;
#endif
    bool paused;
    updateScreenCallback updateScreen;
    char readStringBuf[33];
};

#endif
