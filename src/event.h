/*
 * event.h
 */

#ifndef EVENT_H
#define EVENT_H

#include <cstddef>
#include <list>
#include <vector>

#include "anim.h"
#include "controller.h"
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
    IE_MOUSE_MOVE,
    IE_MOUSE_PRESS,
    IE_MOUSE_RELEASE,
    IE_MOUSE_WHEEL
};

enum ControllerMouseButton {
    CMOUSE_LEFT = 1,
    CMOUSE_MIDDLE,
    CMOUSE_RIGHT
};

struct InputEvent {
    uint16_t type;      // InputEventType
    uint16_t n;         // Button id
    uint16_t state;     // Button mask
    int16_t  x, y;      // Axis value
};

//----------------------------------------------------------------------------

/**
 * A class for handling timed events.
 */
class TimedEvent {
public:
    /* Typedefs */
    typedef void (*Callback)(void *);

    /* Constructors */
    TimedEvent(Callback callback, int interval, void *data = NULL);

    /* Member functions */
    Callback getCallback() const;
    void *getData();
    void tick();

    /* Properties */
protected:
    Callback callback;
    void *data;
    int interval;
    int current;
};

#if defined(IOS)
#ifndef __OBJC__
typedef void *TimedManagerHelper;
typedef void *UIEvent;
#else
@class TimedManagerHelper;
@class UIEvent;
#endif
#endif


/**
 * A class for managing timed events
 */
class TimedEventMgr {
public:
    typedef std::list<TimedEvent*> List;

    TimedEventMgr() : locked(false) {}
    ~TimedEventMgr();

    /** Returns true if the event list is locked (in use) */
    bool isLocked() const { return locked; }

    void add(TimedEvent::Callback callback, int interval, void *data = NULL);
    List::iterator remove(List::iterator i);
    void remove(TimedEvent* event);
    void remove(TimedEvent::Callback callback, void *data = NULL);
    void tick();

private:
    /* Properties */
    bool locked;
    List events;
    List deferredRemovals;
};

#define FS_SAMPLES  8

struct FrameSleep {
    uint32_t frameInterval;     // Milliseconds between display updates.
    uint32_t realTime;
    int ftime[FS_SAMPLES];      // Msec elapsed for past frames.
    int ftimeSum;
    uint16_t ftimeIndex;
    uint16_t fsleep;            // Msec to sleep - adjusted slowly.
};

typedef void(*updateScreenCallback)(void);

struct _MouseArea;
class TextView;
class TextRegions;

/**
 * A class for handling game events.
 */
class EventHandler {
public:
    /* Constructors */
    EventHandler(int gameCycleDuration, int frameDuration);
    ~EventHandler();

    /* Static user input functions. */
    static int       choosePlayer();
    static int       readAlphaAction(char letter, const char* prompt);
    static char      readChoice(const char* choices,
                                const TextRegions* regions = NULL);
    static Direction readDir();
    static int       readInt(int maxlen);
    static const char* readString(int maxlen, const char *extraChars = NULL);
    static const char* readStringView(int maxlen, TextView *view,
                                      const char *extraChars = NULL);
    static void waitAnyKey();
    static void waitAnyKeyTimeout();
    static bool wait_msecs(unsigned int msecs);
    static void ignoreInput();

    /* Static functions */
    static bool timerQueueEmpty();
    static int setKeyRepeat(int delay, int interval);
    static bool globalKeyHandler(int key);
    static bool defaultKeyHandler(int key);

    /* Member functions */
    void setTimerInterval(int msecs);
    uint32_t getTimerInterval() const { return timerInterval; }
    TimedEventMgr* getTimer();

    /* Event functions */
    bool run();
    void setScreenUpdate(void (*updateScreen)(void));
    void togglePause();
    void expose();
#if defined(IOS)
    void handleEvent(UIEvent *);
    static void controllerStopped_helper();
    updateScreenCallback screenCallback() { return updateScreen; }
#endif

    /* Controller functions */
    void runController(Controller*);
    Controller *pushController(Controller *c);
    Controller *popController();
    Controller *getController() const;
    void setController(Controller *c);
    void setControllerDone(bool exit = true);
    bool getControllerDone();
    void quitGame();

    /* Mouse area functions */
    void pushMouseAreaSet(const _MouseArea *mouseAreas);
    void popMouseAreaSet();
    const _MouseArea* getMouseAreaSet() const;
    const _MouseArea* mouseAreaForPoint(int x, int y) const;

#ifdef DEBUG
    bool beginRecording(const char* file, uint32_t seed);
    void endRecording();
    void recordKey(int key);
    int  recordedKey();
    void recordTick() { ++recordClock; }
    uint32_t replay(const char* file);
#endif

    void advanceFlourishAnim() {
        anim_advance(&flourishAnim, float(timerInterval) * 0.001f);
    }

    Animator flourishAnim;
    Animator fxAnim;

protected:
    void handleInputEvents(Controller*, updateScreenCallback);
    bool runPause();

    FrameSleep fs;
    uint32_t timerInterval;     // Milliseconds between timedEvents ticks.
    uint32_t runTime;
    int runRecursion;
#ifdef DEBUG
    int recordFP;
    int recordMode;
    int replayKey;
    uint32_t recordClock;
    uint32_t recordLast;
#endif
    bool paused;
    bool controllerDone;
    bool ended;
    TimedEventMgr timedEvents;
    std::vector<Controller *> controllers;
    std::list<const _MouseArea*> mouseAreaSets;
    updateScreenCallback updateScreen;
    char readStringBuf[33];
};

#endif
