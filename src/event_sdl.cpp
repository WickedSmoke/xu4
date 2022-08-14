/*
 * $Id$
 */

#include <SDL.h>
#include "u4.h"

#include "event.h"

#include "context.h"
#include "xu4.h"

static void handleActiveEvent(const SDL_Event &event, updateScreenCallback updateScreen) {
    if (event.active.state & SDL_APPACTIVE) {
        // application was previously iconified and is now being restored
        if (event.active.gain) {
            if (updateScreen)
                (*updateScreen)();
        }
    }
}

static int translateKey(const SDL_Event &event) {
    int key;

    if (event.key.keysym.unicode != 0)
        key = event.key.keysym.unicode & 0x7F;
    else
        key = event.key.keysym.sym;

    if (event.key.keysym.mod & KMOD_ALT)
#if defined(MACOSX)
        key = U4_ALT + event.key.keysym.sym; // macosx translates alt keys into strange unicode chars
#else
    key += U4_ALT;
#endif
    if (event.key.keysym.mod & KMOD_META)
        key += U4_META;

    if (event.key.keysym.sym == SDLK_UP)
        key = U4_UP;
    else if (event.key.keysym.sym == SDLK_DOWN)
        key = U4_DOWN;
    else if (event.key.keysym.sym == SDLK_LEFT)
        key = U4_LEFT;
    else if (event.key.keysym.sym == SDLK_RIGHT)
        key = U4_RIGHT;
    else if (event.key.keysym.sym == SDLK_BACKSPACE ||
             event.key.keysym.sym == SDLK_DELETE)
        key = U4_BACKSPACE;
    else if (event.key.keysym.sym == SDLK_PAUSE)
        key = U4_PAUSE;

#if defined(MACOSX)
    // Mac OS X translates function keys weirdly too
    if ((event.key.keysym.sym >= SDLK_F1) && (event.key.keysym.sym <= SDLK_F15))
        key = U4_FKEY + (event.key.keysym.sym - SDLK_F1);
#endif

#ifdef DEBUG
    xu4.eventHandler->recordKey(key);
#endif

    if (xu4.verbose)
        printf("key event: unicode = %d, sym = %d, mod = %d; translated = %d\n",
               event.key.keysym.unicode,
               event.key.keysym.sym,
               event.key.keysym.mod,
               key);

    return key;
}

void screenHandleEvents(updateScreenCallback update) {
    SDL_Event event;
    InputEvent ie;
    Stage* st = stage_current();
    if (! st)
        return;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        default:
            break;

        case SDL_KEYDOWN:
        {
            ie.type = IE_KEY_PRESS;
            ie.n = translateKey(event);
            int processed = st->dispatch(st, &ie);
            if (! processed)
                processed = EventHandler::globalKeyHandler(ie.n);

            if (processed && update)
                (*update)();
        }
            break;

        case SDL_MOUSEBUTTONDOWN:
            ie.type = IE_MOUSE_PRESS;
mouse_button:
            ie.n = event.button.button;
            ie.x = event.button.x;
            ie.y = event.button.y;
mouse_event:
            ie.state = 0;
            st->dispatch(st, &ie);
            break;

        case SDL_MOUSEBUTTONUP:
            ie.type = IE_MOUSE_RELEASE;
            goto mouse_button;

        /*
        case SDL_MOUSEWHEEL:
            ie.type = IE_MOUSE_WHEEL;
            ie.n = 0;
            ie.x = event.wheel.x;
            ie.y = event.wheel.y;
            goto mouse_event;
        */

        case SDL_MOUSEMOTION:
            ie.type = IE_MOUSE_MOVE;
            ie.n = 0;
            ie.x = event.motion.x;
            ie.y = event.motion.y;
            goto mouse_event;

        case SDL_ACTIVEEVENT:
            handleActiveEvent(event, update);
            break;

        case SDL_QUIT:
            stage_exit();
            break;
        }
    }
}
