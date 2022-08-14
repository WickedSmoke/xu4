/*
 * death.cpp
 */

#include "config.h"
#include "game.h"
#include "mapmgr.h"
#include "party.h"
#include "portal.h"
#include "screen.h"
#include "settings.h"
#include "stats.h"
#include "u4.h"
#include "xu4.h"

//#define REVIVE_WORLD_X 86
//#define REVIVE_WORLD_Y 107
#define REVIVE_CASTLE_X 19
#define REVIVE_CASTLE_Y 8

#define PAUSE_SEC   5
#define INITIAL_DELAY   99

void deathStart(int delay) {
    stage_runG(STAGE_DEATH, delay ? (void*) 1 : NULL);
}

static void deathRevive() {
    while(! c->location->map->isWorldMap() && c->location->prev != NULL)
        xu4.game->exitToParentMap();

    gameSetViewMode(VIEW_NORMAL);

    /* Move our world map location to Lord British's Castle */
    c->location->coords = c->location->map->portals[0]->coords;

    /* Now, move the avatar into the castle and put him
       in front of Lord British */
    xu4.game->setMap(xu4.config->map(MAP_CASTLE_LB2), 1, NULL);
    Coords& coord = c->location->coords;
    coord.x = REVIVE_CASTLE_X;
    coord.y = REVIVE_CASTLE_Y;
    coord.z = 0;

    c->aura.set(Aura::NONE, 0);
    c->horseSpeed = 0;
    gameStampCommandTime();
    musicPlayLocale();

    c->party->reviveParty();

    screenShowCursor();
    c->stats->setView(STATS_PARTY_OVERVIEW);
}

/*
 * STAGE_DEATH
 */
void* death_enter(Stage* st, void* args)
{
    // stop playing music
    musicFadeOut(1000);
    screenHideCursor();

    st->dstate = args ? INITIAL_DELAY : 0;
    stage_startMsecTimer(st, PAUSE_SEC * 1000);
    return NULL;
}

static const char* deathMsgs[] = {
    "\n\n\nAll is Dark...\n",
    "\nBut wait...\n",
    "Where am I?...\n",
    "Am I dead?...\n",
    "Afterlife?...\n",
    "You hear:\n%s\n",
    "I feel motion...\n",
    "\nLord British says: I have pulled thy spirit and some possessions from the void.  Be more careful in the future!\n\020"
};

#define N_MSGS  (sizeof(deathMsgs) / sizeof(char*))

int death_dispatch(Stage* st, const InputEvent* ev)
{
    if (ev->type == IE_MSEC_TIMER) {
        uint16_t msg = st->dstate;
        if (msg < N_MSGS) {
            if (msg == 0) {
                screenEraseMapArea();
                gameSetViewMode(VIEW_CUTSCENE);
            }

            if (msg == 5) {
                string name = c->party->member(0)->getName();
                int spaces = (TEXT_AREA_W - name.size()) / 2;
                name.insert(0, spaces, ' ');
                screenMessage(deathMsgs[msg], name.c_str());
            } else
                screenMessage(deathMsgs[msg]);

            screenHideCursor();

            if (++msg >= N_MSGS) {
                deathRevive();
                stage_done();
            } else
                st->dstate = msg;
        } else
            st->dstate = 0;     // INITIAL_DELAY is over, start messages.
    }
    return 0;
}
