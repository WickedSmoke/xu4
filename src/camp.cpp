/*
 * $Id$
 */

#include <cstring>

#include "camp.h"

#include "city.h"
#include "config.h"
#include "context.h"
#include "discourse.h"
#include "mapmgr.h"
#include "screen.h"
#include "settings.h"
#include "tileset.h"
#include "utils.h"
#include "xu4.h"

extern int combat_dispatch(Stage*, const InputEvent*);


#define CAMP_FADE_OUT_TIME  1500
#define CAMP_FADE_IN_TIME    500
#define INN_FADE_OUT_TIME   1000
#define INN_FADE_IN_TIME    5000

/* Number of moves before camping will heal the party */
#define CAMP_HEAL_INTERVAL  100


CampController::CampController() {
    /* setup camp for a possible, but not for-sure combat situation */
    MapId id = (c->location->context & CTX_DUNGEON) ? MAP_CAMP_DNG
                                                    : MAP_CAMP_CON;
    map = getCombatMap(xu4.config->map(id));
    xu4.game->setMap(map, true, NULL, this);

    camping = true;
    initCreature(NULL);
    initAltarRoom();

    if (c->horseSpeed == HORSE_GALLOP)
        c->horseSpeed = HORSE_WALK;

    // make sure everyone's asleep
    Party* party = c->party;
    for (int i = 0; i < party->size(); i++)
        party->member(i)->putToSleep();

    placePartyMembers();
}

void CampController::beginCombat(Stage* st)
{
    const Creature *m = Creature::randomAmbushing();

    stage = st;

    musicPlayLocale();
    screenMessage("Ambushed!\n");

    /* create an ambushing creature (so it leaves a chest) */
    Location* prev = c->location->prev;
    setCreature(prev->map->addCreature(m, prev->coords));

    /* fill the creature table with creatures and place them */
    fillCreatureTable(m);
    placeCreatures();

    /* creatures go first! */
    finishTurn();
}

void CampController::wakeUp()
{
    /* Wake everyone up! */
    for (int i = 0; i < c->party->size(); i++)
        c->party->member(i)->wakeUp();

    /* Make sure we've waited long enough for camping to be effective */
    bool healed = false;
    int restCycles = c->saveGame->moves / CAMP_HEAL_INTERVAL;

    if (restCycles >= 0x10000 ||
        (restCycles & 0xffff) != c->saveGame->lastcamp)
        healed = c->party->applyRest(HT_CAMPHEAL);

    screenMessage(healed ? "Party Healed!\n" : "No effect.\n");
    c->saveGame->lastcamp = restCycles & 0xffff;

    xu4.game->exitToParentMap();
    musicFadeIn(CAMP_FADE_IN_TIME, true);
}

void CampController::endCombat(bool adjustKarma) {
    // wake everyone up!
    for (int i = 0; i < c->party->size(); i++)
        c->party->member(i)->wakeUp();
    CombatController::endCombat(adjustKarma);
}

/*
 * STAGE_CAMP
 */
void* camp_enter(Stage* st, void* args) {
    CampController* cc = new CampController();

    musicFadeOut(1000);

    screenHideCursor();
    screenMessage("Resting...\n");

    stage_startMsecTimer(st, xu4.settings->campTime * 1000);
    return cc;
}

void camp_leave(Stage* st) {
    delete (CampController*) st->data;
}

int camp_dispatch(Stage* st, const InputEvent* ev) {
    if (ev->type == IE_MSEC_TIMER) {
        stage_stopMsecTimer(st);

        screenShowCursor();

        /* Is the party ambushed during their rest? */
        CampController* cc = (CampController*) st->data;
        if (xu4.settings->campingAlwaysCombat || (xu4_random(8) == 0)) {
            cc->beginCombat(st);
            st->dispatch = combat_dispatch;
        } else {
            cc->wakeUp();
            stage_done();
        }
    }
    return 0;
}

//-------------------------------------------------------------------------

InnController::InnController() {
    /*
     * Normally in cities, only one opponent per encounter; inn's
     * override this to get the regular encounter size.
     */
    forceStandardEncounterSize = true;
}

void InnController::maybeMeetIsaac()
{
    // Does Isaac the Ghost pay a visit to the Avatar?
    if ((c->location->map->id == 11) && (xu4_random(4) == 0)) {
        City *city = dynamic_cast<City*>(c->location->map);

        int pos = discourse_findName(&city->disc, "Isaac");
        if (pos >= 0) {
            Coords coords(27, xu4_random(3) + 10, c->location->coords.z);

            // If Isaac is already around, just bring him back to the inn
            for (ObjectDeque::iterator i = c->location->map->objects.begin();
                 i != c->location->map->objects.end();
                 i++) {
                Person *p = dynamic_cast<Person*>(*i);
                if (p && strcmp(p->getName(), "Isaac") == 0) {
                    p->updateCoords(coords);
                    return;
                }
            }

            // Otherwise, we need to create Isaac near the Avatar
            Person *Isaac = new Person(xu4.config->creature(GHOST_ID)->tile);

            Isaac->movement = MOVEMENT_WANDER;
            Isaac->setDiscourseId(pos);
            Isaac->getStart() = coords;
            Isaac->prevTile = Isaac->tile;

            city->addPerson(Isaac);
        }
    }
}

bool InnController::maybeAmbush()
{
    if (xu4.settings->innAlwaysCombat || (xu4_random(8) == 0)) {
        MapId mapid;
        uint32_t creatId;

        /* Rats seem much more rare than meeting rogues in the streets */
        if (xu4_random(4) == 0) {
            /* Rats! */
            mapid = MAP_BRICK_CON;
            creatId = RAT_ID;
            showMessage = true;
        } else {
            /* While strolling down the street, attacked by rogues! */
            mapid = MAP_INN_CON;
            creatId = ROGUE_ID;
            screenMessage("\nIn the middle of the night while out on a stroll...\n\n");
            showMessage = false;
        }
        Creature* creature =
            c->location->map->addCreature(xu4.config->creature(creatId),
                                          c->location->coords);

        map = getCombatMap(xu4.config->map(mapid));
        xu4.game->setMap(map, true, NULL, this);

        initCreature(creature);
        return true;
    }
    return false;
}

void InnController::awardLoot() {
    // never get a chest from inn combat
}

/*
 * STAGE_INNREST
 */
void* innrest_enter(Stage* st, void* args) {
    InnController* cc = new InnController();

    /* in the original, the vendor music plays straight through sleeping */
    if (xu4.settings->enhancements)
        musicFadeOut(INN_FADE_OUT_TIME); /* Fade volume out to ease into rest */

    /* first, show the avatar before sleeping */
    gameUpdateScreen();
    stage_startMsecTimer(st, INN_FADE_OUT_TIME);
    st->dstate = 0;
    return cc;
}

void innrest_leave(Stage* st) {
    delete (InnController*) st->data;
}

int innrest_dispatch(Stage* st, const InputEvent* ev) {
    switch (st->dstate) {
    case 0:
        if (ev->type == IE_MSEC_TIMER) {
            /* show the sleeping avatar */
            const Tileset* ts = c->location->map->tileset;
            c->party->setTransport(ts->getByName(Tile::sym.corpse)->getId());
            gameUpdateScreen();

            screenHideCursor();

            stage_startMsecTimer(st, xu4.settings->innTime * 1000);
            ++st->dstate;
        }
        break;

    case 1:
        if (ev->type == IE_MSEC_TIMER) {
            screenShowCursor();

            /* restore the avatar to normal */
            const Tileset* ts = c->location->map->tileset;
            c->party->setTransport(ts->getByName(Tile::sym.avatar)->getId());
            gameUpdateScreen();

            /* the party is always healed */
            c->party->applyRest(HT_INNHEAL);

            /* Is there a special encounter during your stay? */
            // mwinterrowd suggested code, based on u4dos
            InnController* inn = (InnController*) st->data;
            if (c->party->member(0)->isDead()) {
                inn->maybeMeetIsaac();
            } else {
                if (xu4_random(8) != 0)
                    inn->maybeMeetIsaac();
                else {
                    if (inn->maybeAmbush()) {
                        inn->beginCombat(st);
                        ++st->dstate;
                        return 0;
                    }
                }
            }

            screenMessage("\nMorning!\n");
            screenPrompt();

            musicFadeIn(INN_FADE_IN_TIME, true);
            stage_done();
        }
        break;

    case 2:
        return combat_dispatch(st, ev);
    }
    return 0;
}
