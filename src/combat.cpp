/*
 * combat.cpp
 */

#include "combat.h"

#include "config.h"
#include "death.h"
#include "debug.h"
#include "dungeon.h"
#include "item.h"
#include "location.h"
#include "mapmgr.h"
#include "portal.h"
#include "screen.h"
#include "settings.h"
#include "spell.h"
#include "stats.h"
#include "tileset.h"
#include "utils.h"
#include "weapon.h"
#include "xu4.h"

#ifdef IOS
#include "ios_helpers.h"
#endif

/**
 * Returns a CombatMap pointer to the map
 * passed, or a CombatMap pointer to the current map
 * if no arguments were passed.
 *
 * Returns NULL if the map provided (or current map)
 * is not a combat map.
 */
CombatMap *getCombatMap(Map *punknown) {
    Map *m = punknown ? punknown : c->location->map;
    if (!isCombatMap(m))
        return NULL;
    else return dynamic_cast<CombatMap*>(m);
}

void CombatController::engage(MapId mid, const Creature* creatures) {
    CombatController* cc =
        CombatController::init( getCombatMap(xu4.config->map(mid)) );
    cc->initCreature(creatures);

    stage_runG(STAGE_COMBAT, cc);
}

/*
 * Engage combat in dungeon room.
 */
void CombatController::engageDungeon(Dungeon* dng, int room, Direction from) {
    dng->currentRoom = room;

    CombatController* cc = CombatController::init(dng->roomMaps[room]);
    cc->initCreature(NULL);
    cc->initDungeonRoom(room, from);

    stage_runG(STAGE_COMBAT, cc);
}

CombatController* CombatController::init(CombatMap* cmap) {
    CombatController* cc = xu4.game->combatCon;
    if (! cc)
        cc = xu4.game->combatCon = new CombatController;

    cc->camping = false;
    cc->forceStandardEncounterSize = false;
    cc->showMessage = true;
    cc->map = cmap;
    if (cmap)
        xu4.game->setMap(cmap, true, NULL, cc);
    return cc;
}

/*
 * If we entered an altar room, show the name and set the context.
 */
void CombatController::initAltarRoom() {
    if (map->isAltarRoom()) {
        screenMessage("\nThe Altar Room of %s\n",
                      getBaseVirtueName(map->getAltarRoom()));
        c->location->context =
            static_cast<LocationContext>(c->location->context | CTX_ALTAR_ROOM);
    }
}

/**
 * Initializes the combat controller with combat information
 */
void CombatController::initCreature(const Creature *m) {
    int i;

    creature = m;
    placeCreaturesOnMap = (m == NULL) ? false : true;
    winOrLose = true;
    map->setDungeonRoom(false);
    map->setAltarRoom(VIRT_NONE);

    /* initialize creature info */
    for (i = 0; i < AREA_CREATURES; i++)
        creatureTable[i] = NULL;

    for (i = 0; i < AREA_PLAYERS; i++)
        troop[i] = NULL;

    /* fill the creature table if a creature was provided to create */
    fillCreatureTable(m);

    /* initialize focus */
    focus = 0;
}

/**
 * Initializes dungeon room combat
 */
void CombatController::initDungeonRoom(int room, Direction from) {
    int offset, i;
    ASSERT(c->location->prev->context & CTX_DUNGEON, "Error: called initDungeonRoom from non-dungeon context");
    {
        Dungeon *dng = dynamic_cast<Dungeon*>(c->location->prev->map);
        unsigned char
            *party_x = &dng->rooms[room].party_north_start_x[0],
            *party_y = &dng->rooms[room].party_north_start_y[0];

        /* load the dungeon room properties */
        winOrLose = false;
        map->setDungeonRoom(true);
        exitDir = DIR_NONE;

        /* FIXME: this probably isn't right way to see if you're entering an altar room... but maybe it is */
        if ((c->location->prev->map->id != MAP_ABYSS) && (room == 0xF)) {
            /* figure out which dungeon room they're entering */
            if (c->location->prev->coords.x == 3)
                map->setAltarRoom(VIRT_LOVE);
            else if (c->location->prev->coords.x <= 2)
                map->setAltarRoom(VIRT_TRUTH);
            else map->setAltarRoom(VIRT_COURAGE);
        }

        /* load in creatures and creature start coordinates */
        for (i = 0; i < AREA_CREATURES; i++) {
            if (dng->rooms[room].creature_tiles[i] > 0) {
                placeCreaturesOnMap = true;
                creatureTable[i] = Creature::getByTile(dng->rooms[room].creature_tiles[i]);
            }
            map->creature_start[i].x = dng->rooms[room].creature_start_x[i];
            map->creature_start[i].y = dng->rooms[room].creature_start_y[i];
        }

        /* figure out party start coordinates */
        switch(from) {
        case DIR_WEST: offset = 3; break;
        case DIR_NORTH: offset = 0; break;
        case DIR_EAST: offset = 1; break;
        case DIR_SOUTH: offset = 2; break;
        case DIR_ADVANCE:
        case DIR_RETREAT:
        default:
            ASSERT(0, "Invalid 'from' direction passed to initDungeonRoom()");
            return;
        }

        for (i = 0; i < AREA_PLAYERS; i++) {
            map->player_start[i].x = *(party_x + (offset * AREA_PLAYERS * 2) + i);
            map->player_start[i].y = *(party_y + (offset * AREA_PLAYERS * 2) + i);
        }
    }
}

/**
 * Apply tile effects to all creatures depending on what they're standing on
 */
void CombatController::applyCreatureTileEffects() {
    CreatureVector creatures = map->getCreatures();
    CreatureVector::iterator i;

    for (i = creatures.begin(); i != creatures.end(); i++) {
        Creature *m = *i;
        TileEffect effect = map->tileTypeAt(m->coords, WITH_GROUND_OBJECTS)->getEffect();
        m->applyTileEffect(map, effect);
    }
}

/**
 * Begin combat
 */
void CombatController::beginCombat(Stage* st) {
    bool partyIsReadyToFight = false;

    stage = st;

    if (c->horseSpeed == HORSE_GALLOP)
        c->horseSpeed = HORSE_GALLOP_INTERRUPT;

    /* place party members on the map */
    placePartyMembers();

    /* place creatures on the map */
    if (placeCreaturesOnMap)
        placeCreatures();

    initAltarRoom();

    /* if there are creatures around, start combat! */
    if (showMessage && placeCreaturesOnMap && winOrLose)
        screenMessage("\n%c****%c COMBAT %c****%c\n", FG_GREY, FG_WHITE, FG_GREY, FG_WHITE);

    musicPlayLocale();

    /* Set focus to the first active party member, if there is one */
    for (int i = 0; i < AREA_PLAYERS; i++) {
        if (setActivePlayer(i)) {
            partyIsReadyToFight = true;
            break;
        }
    }

    if (! partyIsReadyToFight)
        c->location->turnCompleter->finishTurn();
}

/*
 * Transition back to parent map/controller and delete this controller.
 */
void CombatController::endCombat(bool adjustKarma) {
    /* The party is dead -- start the death sequence */
    if (c->party->isDead()) {
        /* remove the creature */
        if (creature)
            c->location->map->removeObject(creature);

        deathStart(5);
    }
    else {
        /* need to get this here because when we exit to the parent map, all the monsters are cleared */
        bool won = isWon();

        xu4.game->exitToParentMap();
        musicPlayLocale();

        if (winOrLose) {
            if (won) {
                if (creature) {
                    if (creature->isEvil())
                        c->party->adjustKarma(KA_KILLED_EVIL);
                    awardLoot();
                }

                screenMessage("\nVictory!\n\n");
            }
            else if (!c->party->isDead()) {
                /* minus points for fleeing from evil creatures */
                if (adjustKarma && creature && creature->isEvil()) {
                    screenMessage("\nBattle is lost!\n\n");
                    c->party->adjustKarma(KA_FLED_EVIL);
                }
                else if (adjustKarma && creature && creature->isGood())
                    c->party->adjustKarma(KA_FLED_GOOD);
            }
        }

        /* exiting a dungeon room */
        if (map->isDungeonRoom()) {
            screenMessage("Leave Room!\n");
            if (map->isAltarRoom()) {
                PortalTriggerAction action = ACTION_NONE;

                /* when exiting altar rooms, you exit to other dungeons.  Here it goes... */
                switch(exitDir) {
                case DIR_NORTH: action = ACTION_EXIT_NORTH; break;
                case DIR_EAST:  action = ACTION_EXIT_EAST; break;
                case DIR_SOUTH: action = ACTION_EXIT_SOUTH; break;
                case DIR_WEST:  action = ACTION_EXIT_WEST; break;
                case DIR_NONE:  break;
                case DIR_ADVANCE:
                case DIR_RETREAT:
                default: ASSERT(0, "Invalid exit dir %d", exitDir); break;
                }

                if (action != ACTION_NONE)
                    usePortalAt(c->location, c->location->coords, action);
            }
            else screenMessage("\n");

            if (exitDir != DIR_NONE) {
                c->saveGame->orientation = exitDir;  /* face the direction exiting the room */
                // XXX: why north, shouldn't this be orientation?
                c->location->move(DIR_NORTH, false);  /* advance 1 space outside of the room */
            }
        }

        /* remove the creature */
        if (creature)
            c->location->map->removeObject(creature);

        /* Make sure finishturn only happens if a new combat has not begun */
        TurnController* tcon = c->location->turnCompleter;
        if (! tcon->isCombatController())
            tcon->finishTurn();

        stage_runS(stage, STAGE_EX_DONE);   // stage_done()
    }
}

/**
 * Fills the combat creature table with the creatures that the party will be facing.
 * The creature table only contains *which* creatures will be encountered and
 * *where* they are placed (by position in the table).  Information like
 * hit points and creature status will be created when the creature is actually placed
 */
void CombatController::fillCreatureTable(const Creature *creature) {
    int i, j;

    if (creature != NULL) {
        const Creature *baseCreature = creature, *current;
        int numCreatures = initialNumberOfCreatures(creature);

        if (baseCreature->getId() == PIRATE_ID)
            baseCreature = xu4.config->creature(ROGUE_ID);

        for (i = 0; i < numCreatures; i++) {
            current = baseCreature;

            /* find a free spot in the creature table */
            do {j = xu4_random(AREA_CREATURES) ;} while (creatureTable[j] != NULL);

            /* see if creature is a leader or leader's leader */
            if (xu4.config->creature(baseCreature->getLeader()) != baseCreature && /* leader is a different creature */
                i != (numCreatures - 1)) { /* must have at least 1 creature of type encountered */

                if (xu4_random(32) == 0)       /* leader's leader */
                    current = xu4.config->creature(xu4.config->creature(baseCreature->getLeader())->getLeader());
                else if (xu4_random(8) == 0)   /* leader */
                    current = xu4.config->creature(baseCreature->getLeader());
            }

            /* place this creature in the creature table */
            creatureTable[j] = current;
        }
    }
}

/**
 * Generate the number of creatures in a group.
 */
int  CombatController::initialNumberOfCreatures(const Creature *creature) const {
    int ncreatures;
    Map *map = c->location->prev ? c->location->prev->map : c->location->map;

    /* if in an unusual combat situation, generally we stick to normal encounter sizes,
       (such as encounters from sleeping in an inn, etc.) */
    if (forceStandardEncounterSize || map->isWorldMap() || (c->location->prev && c->location->prev->context & CTX_DUNGEON)) {
        ncreatures = xu4_random(8) + 1;

        if (ncreatures == 1) {
            if (creature && creature->getEncounterSize() > 0)
                ncreatures = xu4_random(creature->getEncounterSize()) + creature->getEncounterSize() + 1;
            else
                ncreatures = 8;
        }

        while (ncreatures > 2 * c->saveGame->members) {
            ncreatures = xu4_random(16) + 1;
        }
    } else {
        if (creature && creature->getId() == GUARD_ID)
            ncreatures = c->saveGame->members * 2;
        else
            ncreatures = 1;
    }

    return ncreatures;
}

/**
 * Returns true if the player has won.
 */
bool CombatController::isWon() const {
    CreatureVector creatures = map->getCreatures();
    if (creatures.size())
        return false;
    return true;
}

/**
 * Returns true if the player has lost.
 */
bool CombatController::isLost() const {
    PartyMemberVector party = map->getPartyMembers();
    if (party.size())
        return false;
    return true;
}

/**
 * Performs all of the creature's actions
 */
void CombatController::moveCreatures() {
    Creature *m;

    // XXX: this iterator is rather complex; but the vector::iterator can
    // break and crash if we delete elements while iterating it, which we do
    // if a jinxed monster kills another
    for (unsigned int i = 0; i < map->getCreatures().size(); i++) {
        m = map->getCreatures().at(i);
        m->act(this);

        if (i < map->getCreatures().size() && map->getCreatures().at(i) != m) {
            // don't skip a later creature when an earlier one flees
            i--;
        }
    }
}

/**
 * Places creatures on the map from the creature table and from the creature_start coords
 */
void CombatController::placeCreatures() {
    int i;

    for (i = 0; i < AREA_CREATURES; i++) {
        const Creature *m = creatureTable[i];
        if (m)
            map->addCreature(m, map->creature_start[i]);
    }
}

/**
 * Places the party members on the map
 */
void CombatController::placePartyMembers() {
    int i;
    for (i = 0; i < c->party->size(); i++) {
        PartyMember *p = c->party->member(i);
        p->focused = false; // take the focus off of everyone

        /* don't place dead party members */
        if (p->getStatus() != STAT_DEAD) {
            /* add the party member to the map */
            p->placeOnMap(map, map->player_start[i]);
            map->objects.push_back(p);
            troop[i] = p;
        }
    }
}

void CombatController::announceActivePlayer() {
    PartyMember* p = getCurrentPlayer();
    screenMessage("\n%s with %s\n\020", p->getName(), p->getWeapon()->getName());
}

/**
 * Sets the active player for combat, showing which weapon they're weilding, etc.
 */
bool CombatController::setActivePlayer(int player) {
    PartyMember *p = troop[player];

    if (p && !p->isDisabled()) {
        if (troop[focus])
            troop[focus]->focused = false;

        p->focused = true;
        focus = player;

        announceActivePlayer();
        c->stats->highlightPlayer(focus);
        return true;
    }

    c->stats->highlightPlayer(-1);
    return false;
}

void CombatController::awardLoot() {
    const Coords& coords = creature->coords;
    Location* loc = c->location;
    const Tile *ground = loc->map->tileTypeAt(coords, WITHOUT_OBJECTS);

    /* add a chest, if the creature leaves one */
    if (creature->leavesChest() &&
        ground->isCreatureWalkable() &&
        (!(loc->context & CTX_DUNGEON) || ground->isDungeonFloor())) {
        MapTile chest = loc->map->tileset->getByName(Tile::sym.chest)->getId();
        loc->map->addObject(chest, chest, coords);
    }
    /* add a ship if you just defeated a pirate ship */
    else if (creature->tile.getTileType()->isPirateShip()) {
        MapTile ship = loc->map->tileset->getByName(Tile::sym.ship)->getId();
        ship.setDirection(creature->tile.getDirection());
        loc->map->addObject(ship, ship, coords);
    }
}

bool CombatController::attackHit(const Creature* attacker,
                                 const Creature* defender) {
    ASSERT(attacker != NULL, "attacker must not be NULL");
    ASSERT(defender != NULL, "defender must not be NULL");

    int attackValue = xu4_random(0x100) + attacker->getAttackBonus();
    return attackValue > defender->getDefense();
}

#ifdef GPU_RENDER
#include <math.h>

static void animateAttack(const vector<Coords>& path, int range, TileId tid) {
    float vec[4];

    vec[0] = path[0].x;
    vec[1] = path[0].y;
    vec[2] = path[range].x;
    vec[3] = path[range].y;

    float dx = vec[2] - vec[0];
    float dy = vec[3] - vec[1];
    float duration = sqrtf(dx*dx + dy*dy) /
                float(xu4.settings->screenAnimationFramesPerSecond);
    AnimId move = anim_startLinearF2(&xu4.eventHandler->fxAnim, duration, 0,
                                     vec, vec + 2);

    int fx = xu4.game->mapArea.showEffect(path[0], tid, move);
    EventHandler::wait_msecs(duration * 1000.0);
    xu4.game->mapArea.removeEffect(fx);
}

enum AttackResult {
    AR_None,
    AR_NoTarget,
    AR_Miss,
    AR_Hit
};

static int attackAt2(CombatMap* map, const Coords& coords,
                     const PartyMember* attacker,
                     const Weapon* weapon, Creature** cptr) {
    Creature* creature = map->creatureAt(coords);
    if (! creature)
        return AR_NoTarget; // No target found.
    *cptr = creature;

    if (c->location->prev->map->id == MAP_ABYSS && ! weapon->isMagic())
        return AR_Miss;     // Non-magical weapons in the Abyss miss.

    if (CombatController::attackHit(attacker, creature))
        return AR_Hit;      // The weapon hit!

    return AR_Miss;         // Player naturally missed.
}
#endif

bool CombatController::attackAt(const Coords &coords, PartyMember *attacker, int dir, int range, int distance) {
    const Weapon *weapon = attacker->getWeapon();
    bool wrongRange = weapon->rangeAbsolute() && (distance != range);

    MapTile hittile  = map->tileset->getByName(weapon->hitTile)->getId();
    MapTile misstile = map->tileset->getByName(weapon->missTile)->getId();

    // Check to see if something hit
    Creature *creature = map->creatureAt(coords);

    /* If we haven't hit a creature, or the weapon's range is absolute
       and we're testing the wrong range, stop now! */
    if (!creature || wrongRange) {

        /* If the weapon is shown as it travels, show it now */
        if (weapon->showTravel()) {
            GameController::flashTile(coords, misstile, 1);
        }

        // no target found
        return false;
    }

    /* Did the weapon miss? */
    if ((c->location->prev->map->id == MAP_ABYSS && !weapon->isMagic()) || /* non-magical weapon in the Abyss */
        !attackHit(attacker, creature)) { /* player naturally missed */
        screenMessage("Missed!\n");

        /* show the 'miss' tile */
        GameController::flashTile(coords, misstile, 1);
    } else { /* The weapon hit! */

        /* show the 'hit' tile */
        GameController::flashTile(coords, misstile, 1);
        soundPlay(SOUND_NPC_STRUCK, false,-1);                                    // NPC_STRUCK, melee hit
        GameController::flashTile(coords, hittile, 3);

        /* apply the damage to the creature */
        if (!attacker->dealDamage(map, creature, attacker->getDamage()))
        {
            creature = NULL;
            GameController::flashTile(coords, hittile, 1);
        }
    }

    return true;
}

static bool rangedAttack(const Coords &coords, CombatMap* map,
                         Creature *attacker,
                         const MapTile& hittile, const MapTile& misstile) {

    Creature *target = isCreature(attacker) ? map->partyMemberAt(coords)
                                            : map->creatureAt(coords);

    /* If we haven't hit something valid, stop now */
    if (!target) {
        GameController::flashTile(coords, misstile, 1);
        return false;
    }

    /* Get the effects of the tile the creature is using */
    TileEffect effect = hittile.getTileType()->getEffect();

    /* Monster's ranged attacks never miss */

    GameController::flashTile(coords, misstile, 1);
    /* show the 'hit' tile */
    GameController::flashTile(coords, hittile, 3);

    /* These effects happen whether or not the opponent was hit */
    switch(effect) {

    case EFFECT_ELECTRICITY:
        /* FIXME: are there any special effects here? */
        soundPlay(SOUND_PC_STRUCK, false);
        screenMessage("\n%s %cElectrified%c!\n", target->getName(), FG_BLUE, FG_WHITE);
        attacker->dealDamage(map, target, attacker->getDamage());
        break;

    case EFFECT_POISON:
    case EFFECT_POISONFIELD:
        /* see if the player is poisoned */
        if ((xu4_random(2) == 0) && (target->getStatus() != STAT_POISONED))
        {
            // POISON_EFFECT, ranged hit
            soundPlay(SOUND_POISON_EFFECT, false);
            screenMessage("\n%s %cPoisoned%c!\n", target->getName(), FG_GREEN, FG_WHITE);
            target->addStatus(STAT_POISONED);
        }
        // else screenMessage("Failed.\n");
        break;

    case EFFECT_SLEEP:
        /* see if the player is put to sleep */
        if (xu4_random(2) == 0)
        {
            // SLEEP, ranged hit, plays even if sleep failed or PC already asleep
            soundPlay(SOUND_SLEEP, false);
            screenMessage("\n%s %cSlept%c!\n", target->getName(), FG_PURPLE, FG_WHITE);
            target->putToSleep();
        }
        // else screenMessage("Failed.\n");
        break;

    case EFFECT_LAVA:
    case EFFECT_FIRE:
        /* FIXME: are there any special effects here? */
        soundPlay(SOUND_PC_STRUCK, false);
        screenMessage("\n%s %c%s Hit%c!\n", target->getName(), FG_RED,
                      effect == EFFECT_LAVA ? "Lava" : "Fiery", FG_WHITE);
        attacker->dealDamage(map, target, attacker->getDamage());
        break;

    default:
        /* show the appropriate 'hit' message */
        // soundPlay(SOUND_PC_STRUCK, false);
        if (hittile == Tileset::findTileByName(Tile::sym.magicFlash)->getId())
            screenMessage("\n%s %cMagical Hit%c!\n", target->getName(), FG_BLUE, FG_WHITE);
        else screenMessage("\n%s Hit!\n", target->getName());
        attacker->dealDamage(map, target, attacker->getDamage());
        break;
    }
    GameController::flashTile(coords, hittile, 1);
    return true;
}

bool CombatController::creatureRangedAttack(Creature* attacker, int dir) {
    MapTile hit  = map->tileset->getByName(attacker->getHitTile())->getId();
    MapTile miss = map->tileset->getByName(attacker->getMissTile())->getId();
    vector<Coords> path = gameGetDirectionalActionPath(dir, MASK_DIR_ALL,
                                attacker->coords, 1, 11,
                                &Tile::canAttackOverTile, false);
    vector<Coords>::iterator it;
    for (it = path.begin(); it != path.end(); it++) {
        if (rangedAttack(*it, map, attacker, hit, miss))
            return true;
    }

    // If a miss leaves a tile behind, do it here! (lava lizard, etc)
    if (attacker->leavesTile() && path.size() > 0) {
        const Coords& coords = path[path.size() - 1];
        const Tile *ground = map->tileTypeAt(coords, WITH_GROUND_OBJECTS);
        if (ground->isWalkable())
            map->annotations.add(coords, hit);
    }
    return false;
}

bool CombatController::returnWeaponToOwner(const Coords &coords, int distance, int dir, const Weapon *weapon) {
    Coords new_coords = coords;

    MapTile misstile = map->tileset->getByName(weapon->missTile)->getId();

    /* reverse the direction of the weapon */
    Direction returnDir = dirReverse(dirFromMask(dir));

    for (int i = distance; i > 1; i--) {
        map_move(new_coords, returnDir, map);

        GameController::flashTile(new_coords, misstile, 1);
    }
    gameUpdateScreen();

    return true;
}

void CombatController::finishTurn() {
    PartyMember *player = getCurrentPlayer();
    int quick;
    int active;

    /* return to party overview */
    c->stats->setView(STATS_PARTY_OVERVIEW);
    c->stats->highlightPlayer(-1);

    if (isWon() && winOrLose) {
        endCombat(true);
        return;
    }

    Map* map = c->location->map;

    /* make sure the player with the focus is still in battle (hasn't fled or died) */
    if (player) {
        /* apply effects from tile player is standing on */
        player->applyEffect(map, map->tileTypeAt(player->coords, WITH_GROUND_OBJECTS)->getEffect());
    }

    quick = (c->aura.getType() == Aura::QUICKNESS) && player && (xu4_random(2) == 0) ? 1 : 0;

    /* check to see if the player gets to go again (and is still alive) */
    if (!quick || player->isDisabled()){

        do {
            map->annotations.passTurn();

            /* put a sleeping person in place of the player,
               or restore an awakened member to their original state */
            if (player) {
                if (player->getStatus() == STAT_SLEEPING && (xu4_random(8) == 0))
                    player->wakeUp();

                /* remove focus from the current party member */
                player->focused = false;

                /* eat some food */
                c->party->adjustFood(-1);
            }

            /* put the focus on the next party member */
            focus++;

            /* move creatures and wrap around at end */
            if (focus >= c->party->size()) {

                /* reset the focus to the avatar and start the party's turn over again */
                focus = 0;

                /* give a slight pause in case party members are asleep for awhile */
                gameUpdateScreen();
                EventHandler::wait_msecs(50);

                /* adjust moves */
                c->party->endTurn();

                /* count down our aura (if we have one) */
                c->aura.passTurn();

                /**
                 * ====================
                 * HANDLE CREATURE STUFF
                 * ====================
                 */

                /* first, move all the creatures */
                moveCreatures();

                /* then, apply tile effects to creatures */
                applyCreatureTileEffects();

                /* check to see if combat is over */
                if (isLost()) {
                    endCombat(true);
                    return;
                }

                /* end combat immediately if the enemy has fled */
                else if (isWon() && winOrLose) {
                    endCombat(true);
                    return;
                }
            }

            /* get the next party member */
            player = getCurrentPlayer();
            active = c->party->getActivePlayer();

        } while (! player ||
                 player->isDisabled() || /* dead or sleeping */
                 ((active >= 0) && /* active player is set */
                  troop[active] && /* and the active player is still in combat */
                  ! troop[active]->isDisabled() && /* and the active player is not disabled */
                  (active != focus)));
    }
    else
        map->annotations.passTurn();

#if 0
    if (focus != 0) {
        getCurrentPlayer()->act();
        finishTurn();
    }
    else setActivePlayer(focus);
#else
    /* display info about the current player */
    setActivePlayer(focus);
#endif
}

/**
 * Move a party member during combat and display the appropriate messages
 */
void CombatController::movePartyMember(MoveEvent &event) {
    /* active player left/fled combat */
    if ((event.result & MOVE_EXIT_TO_PARENT) && (c->party->getActivePlayer() == focus)) {
        c->party->setActivePlayer(-1);
        /* assign active player to next available party member */
        for (int i = 0; i < c->party->size(); i++) {
            if (troop[i] && !troop[i]->isDisabled()) {
                c->party->setActivePlayer(i);
                break;
            }
        }
    }

    screenMessage("%s\n", getDirectionName(event.dir));
    if (event.result & MOVE_MUST_USE_SAME_EXIT) {
        // ERROR move, all PCs must use the same exit
        // NOTE: In the DOS version SOUND_BLOCKED is played for this.
        soundPlay(SOUND_ERROR);
        screenMessage("All must use same exit!\n");
    }
    else if (event.result & MOVE_BLOCKED) {
        soundPlay(SOUND_BLOCKED);               // BLOCKED move
        screenMessage("%cBlocked!%c\n", FG_GREY, FG_WHITE);
    }
    else if (event.result & MOVE_SLOWED) {
        soundPlay(SOUND_WALK_SLOWED);           // WALK_SLOWED move
        screenMessage("%cSlow progress!%c\n", FG_GREY, FG_WHITE);
    }
    else if (winOrLose && getCreature()->isEvil() && (event.result & (MOVE_EXIT_TO_PARENT | MOVE_MAP_CHANGE))) {
        soundPlay(SOUND_FLEE);                  // FLEE move
    }
    else {
        soundPlay(SOUND_WALK_COMBAT);           // WALK_COMBAT move
    }
}

// Key handlers
bool CombatController::keyPressed(int key) {
    Settings& settings = *xu4.settings;
    bool valid = true;
    bool endTurn = true;

    switch (key) {
    case U4_UP:
    case U4_DOWN:
    case U4_LEFT:
    case U4_RIGHT:
        c->location->move(keyToDirection(key), true);
        break;

    case 5:         /* ctrl-E */
        if (settings.debug)
            endCombat(false);   /* don't adjust karma */
        break;

    case U4_SPACE:
        screenMessage("Pass\n");
        break;

    case U4_FKEY:
        if (settings.debug)
            gameDestroyAllCreatures();
        else
            valid = false;
        break;

    // Change the speed of battle
    case '+':
    case '-':
    case U4_KEYPAD_ENTER:
        {
            int old_speed = settings.battleSpeed;
            if (key == '+' && ++settings.battleSpeed > MAX_BATTLE_SPEED)
                settings.battleSpeed = MAX_BATTLE_SPEED;
            else if (key == '-' && --settings.battleSpeed == 0)
                settings.battleSpeed = 1;
            else if (key == U4_KEYPAD_ENTER)
                settings.battleSpeed = DEFAULT_BATTLE_SPEED;

            if (old_speed != settings.battleSpeed) {
                if (settings.battleSpeed == DEFAULT_BATTLE_SPEED)
                    screenMessage("Battle Speed:\nNormal\n");
                else if (key == '+')
                    screenMessage("Battle Speed:\nUp (%d)\n", settings.battleSpeed);
                else screenMessage("Battle Speed:\nDown (%d)\n", settings.battleSpeed);
            }
            else if (settings.battleSpeed == DEFAULT_BATTLE_SPEED)
                screenMessage("Battle Speed:\nNormal\n");
        }

        valid = false;
        break;

    /* handle music volume adjustments */
    case ',':
        // decrease the volume if possible
        screenMessage("Music: %d%s\n", musicVolumeDec(), "%");
        endTurn = false;
        break;
    case '.':
        // increase the volume if possible
        screenMessage("Music: %d%s\n", musicVolumeInc(), "%");
        endTurn = false;
        break;

    /* handle sound volume adjustments */
    case '<':
        // decrease the volume if possible
        screenMessage("Sound: %d%s\n", soundVolumeDec(), "%");
        soundPlay(SOUND_FLEE);
        endTurn = false;
        break;
    case '>':
        // increase the volume if possible
        screenMessage("Sound: %d%s\n", soundVolumeInc(), "%");
        soundPlay(SOUND_FLEE);
        endTurn = false;
        break;

    case 'a':
        attack();
        break;

    case 'c':
        screenMessage("Cast Spell!\n");
        castSpell(focus);
        break;

#ifdef IOS
    case U4_ENTER: // Fall through and get the chest.
#endif
    case 'g':
        screenMessage("Get Chest!\n");
        getChest(focus);
        break;

    case 'l':
        if (settings.debug) {
            const Coords& coords = getCurrentPlayer()->coords;
            screenMessage("\nLocation:\nx:%d\ny:%d\nz:%d\n", coords.x, coords.y, coords.z);
            screenPrompt();
            valid = false;
        }
        else
            screenMessage("Not here!\n");
        break;

    case 'r':
        readyWeapon(getFocus());
        break;

    case 't':
        if (settings.debug && map->isDungeonRoom()) {
            Dungeon *dungeon = dynamic_cast<Dungeon*>(c->location->prev->map);
            Trigger *triggers = dungeon->rooms[dungeon->currentRoom].triggers;
            int i;

            screenMessage("Triggers!\n");

            for (i = 0; i < 4; i++) {
                screenMessage("%.1d)xy tile xy xy\n", i+1);
                screenMessage("  %.1X%.1X  %.3d %.1X%.1X %.1X%.1X\n",
                    triggers[i].x, triggers[i].y,
                    triggers[i].tile,
                    triggers[i].change_x1, triggers[i].change_y1,
                    triggers[i].change_x2, triggers[i].change_y2);
            }
            screenPrompt();
            valid = false;

        }
        else
            screenMessage("Not here!\n");
        break;

    case 'u':
        screenMessage("Use which item:\n");
        c->stats->setView(STATS_ITEMS);
#ifdef IOS
        U4IOS::IOSConversationHelper::setIntroString("Use which item?");
#endif
        itemUse(gameGetInput());
        break;

    case 'v':
        if (musicToggle())
            screenMessage("Volume On!\n");
        else
            screenMessage("Volume Off!\n");
        endTurn = false;
        break;

    case 'v' + U4_ALT:
        screenMessage("XU4 %s\n", VERSION);
        endTurn = false;
        break;

    case 'z':
        c->stats->setView(StatsView(STATS_CHAR1 + getFocus()));

        /* reset the spell mix menu and un-highlight the current item,
           and hide reagents that you don't have */
        c->stats->resetReagentsMenu();

        screenMessage("Ztats\n");
        stage_runG(STAGE_STATS, NULL);
        return true;

    case 'b':
    case 'e':
    case 'd':
    case 'f':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 's':
    case 'w':
    case 'x':
    case 'y':
        screenMessage("Not here!\n");
        break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        if (settings.enhancements && settings.enhancementsOptions.activePlayer)
            gameSetActivePlayer(key - '1');
        else {
            gameBadCommand();
            announceActivePlayer();
            endTurn = false;
        }
        break;

    default:
        return false;   //EventHandler::defaultKeyHandler(key);
    }

    if (valid) {
        gameStampCommandTime();
        if (endTurn /*&& (xu4.eventHandler->getController() == this)*/)
            finishTurn();
    }

    return valid;
}

/**
 * Key handler for choosing an attack direction
 */
void CombatController::attack() {
    screenMessage("Dir: ");

    Direction dir = EventHandler::readDir();
    if (dir == DIR_NONE)
        return;
    screenMessage("%s\n", getDirectionName(dir));

    PartyMember *attacker = getCurrentPlayer();

    const Weapon *weapon = attacker->getWeapon();
    int range = weapon->range;
    if (weapon->canChooseDistance()) {
        screenMessage("Range: ");
        range = EventHandler::readChoice("123456789") - '0';
        if (range < 1 || range > weapon->range)
            return;
        screenMessage("%d\n", range);
    }

    // the attack was already made, even if there is no valid target
    // so play the attack sound
    soundPlay(SOUND_PC_ATTACK, false);


    vector<Coords> path =
        gameGetDirectionalActionPath(MASK_DIR(dir), MASK_DIR_ALL,
                                attacker->coords, 1, range,
                                weapon->canAttackThroughObjects() ? NULL
                                                    : &Tile::canAttackOverTile,
                                false);

    int targetDistance = path.size();
    Coords targetCoords;
    if (targetDistance > 0)
        targetCoords = path.back();
    else
        targetCoords = attacker->coords;

    bool foundTarget = false;
#ifdef GPU_RENDER
    Creature* target = NULL;
    MapTile missTile = map->tileset->getByName(weapon->missTile)->id;
    int result = AR_None;
    if (weapon->rangeAbsolute()) {
        if (range == targetDistance) {
            result = attackAt2(map, path[range], attacker, weapon, &target);
        }
    } else {
        for (int di = 0; di < targetDistance; ++di) {
            result = attackAt2(map, path[di], attacker, weapon, &target);
            if (result != AR_NoTarget) {
                foundTarget = true;
                targetDistance = di + 1;
                targetCoords = path[di];
                break;
            }
        }
    }

    if (weapon->showTravel() && targetDistance > 1)
        animateAttack(path, targetDistance - 1, missTile.id);

    switch (result) {
        case AR_None:
        case AR_NoTarget:
            break;
        case AR_Miss:
            screenMessage("Missed!\n");
            GameController::flashTile(targetCoords, missTile, 1);
            break;
        case AR_Hit:
            GameController::flashTile(targetCoords, missTile, 1);
            soundPlay(SOUND_NPC_STRUCK, false, -1);

            MapTile hitTile = map->tileset->getByName(weapon->hitTile)->getId();
            GameController::flashTile(targetCoords, hitTile, 3);

            /* apply the damage to the creature */
            if (! attacker->dealDamage(map, target, attacker->getDamage())) {
                GameController::flashTile(targetCoords, hitTile, 1);
            }
            break;
    }
#else
    int distance = 1;
    for (vector<Coords>::iterator i = path.begin(); i != path.end(); i++) {
        if (attackAt(*i, attacker, MASK_DIR(dir), range, distance)) {
            foundTarget = true;
            targetDistance = distance;
            targetCoords = *i;
            break;
        }
        distance++;
    }
#endif

    // is weapon lost? (e.g. dagger)
    if (weapon->loseWhenUsed() ||
        (weapon->loseWhenRanged() && (!foundTarget || targetDistance > 1))) {
        if (!attacker->loseWeapon())
            screenMessage("Last One!\n");
    }

    // does weapon leave a tile behind? (e.g. flaming oil)
    if (weapon->leaveTile) {
        const Tile *ground = map->tileTypeAt(targetCoords, WITHOUT_OBJECTS);
        if (ground->isWalkable())
            map->annotations.add(targetCoords,
                    map->tileset->getByName(weapon->leaveTile)->getId());
    }

    /* show the 'miss' tile */
    if (!foundTarget) {
        GameController::flashTile(targetCoords, weapon->missTile, 1);
        /* This goes here so messages are shown in the original order */
        screenMessage("Missed!\n");
    }

    // does weapon returns to its owner? (e.g. magic axe)
    if (weapon->returns())
        returnWeaponToOwner(targetCoords, targetDistance, MASK_DIR(dir), weapon);
}

static void combatNotice(int sender, void* eventData, void* user) {
    PartyEvent* event = (PartyEvent*) eventData;
    (void) sender;
    (void) user;
    if (event->type == PartyEvent::PLAYER_KILLED)
        screenMessage("\n%c%s is Killed!%c\n",
                      FG_RED, event->player->getName(), FG_WHITE);
}

/**
 * CombatMap class implementation
 */
CombatMap::CombatMap() : Map(), dungeonRoom(false), altarRoom(VIRT_NONE), contextual(false) {}

/**
 * Returns a vector containing all of the creatures on the map
 */
CreatureVector CombatMap::getCreatures() {
    ObjectDeque::iterator i;
    CreatureVector creatures;
    for (i = objects.begin(); i != objects.end(); i++) {
        if (isCreature(*i) && !isPartyMember(*i))
            creatures.push_back(dynamic_cast<Creature*>(*i));
    }
    return creatures;
}

/**
 * Returns a vector containing all of the party members on the map
 */
PartyMemberVector CombatMap::getPartyMembers() {
    ObjectDeque::iterator i;
    PartyMemberVector party;
    for (i = objects.begin(); i != objects.end(); i++) {
        if (isPartyMember(*i))
            party.push_back(dynamic_cast<PartyMember*>(*i));
    }
    return party;
}

/**
 * Returns the party member at the given coords, if there is one,
 * NULL if otherwise.
 */
PartyMember *CombatMap::partyMemberAt(Coords coords) {
    PartyMemberVector party = getPartyMembers();
    PartyMemberVector::iterator i;

    for (i = party.begin(); i != party.end(); i++) {
        if ((*i)->coords == coords)
            return *i;
    }
    return NULL;
}

/**
 * Returns the creature at the given coords, if there is one,
 * NULL if otherwise.
 */
Creature *CombatMap::creatureAt(Coords coords) {
    CreatureVector creatures = getCreatures();
    CreatureVector::iterator i;

    for (i = creatures.begin(); i != creatures.end(); i++) {
        if ((*i)->coords == coords)
            return *i;
    }
    return NULL;
}

// These coincide with Tile::sym.dungeonMaps[]
static const uint8_t dungeonMapId[6] = {
    MAP_DNG0_CON,   // brick_floor
    MAP_DNG1_CON,   // up_ladder
    MAP_DNG2_CON,   // down_ladder
    MAP_DNG3_CON,   // up_down_ladder
 // MAP_DNG4_CON,   // chest          // chest tile doesn't work that well
    MAP_DNG5_CON,   // dungeon_door
    MAP_DNG6_CON,   // secret_door
};

// These coincide with Tile::sym.combatMaps[]
static const uint8_t combatMapId[21] = {
    MAP_GRASS_CON,  // horse
    MAP_MARSH_CON,  // swamp
    MAP_GRASS_CON,  // grass
    MAP_BRUSH_CON,  // brush
    MAP_FOREST_CON, // forest
    MAP_HILL_CON,   // hills
    MAP_DUNGEON_CON,// dungeon
    MAP_GRASS_CON,  // city
    MAP_GRASS_CON,  // castle
    MAP_GRASS_CON,  // town
    MAP_GRASS_CON,  // lcb_entrance
    MAP_BRIDGE_CON, // bridge
    MAP_GRASS_CON,  // balloon
    MAP_BRIDGE_CON, // bridge_n
    MAP_BRIDGE_CON, // bridge_s
    MAP_GRASS_CON,  // shrine
    MAP_GRASS_CON,  // chest
    MAP_BRICK_CON,  // brick_floor
    MAP_GRASS_CON,  // moongate
    MAP_GRASS_CON,  // moongate_opening
    MAP_GRASS_CON,  // dungeon_floor
};

static void _relateMapsToTiles(std::map<const Tile*, MapId>& tmap,
                               const uint8_t* mapIds, int count,
                               const Symbol* tileNames)
{
    const Tileset* ts = xu4.config->tileset();
    for (int i = 0; i < count; ++i)
        tmap[ ts->getByName(tileNames[i]) ] = mapIds[i];
}

/**
 * Returns a valid combat map given the provided information
 */
MapId GameController::combatMapForTile(const Tile *groundTile, Object *obj) {
    bool fromShip, toShip;
    Location* loc = c->location;

    if (loc->context & CTX_DUNGEON) {
        if (dungeontileMap.empty())
            _relateMapsToTiles(dungeontileMap,
                               dungeonMapId, sizeof(dungeonMapId),
                               Tile::sym.dungeonMaps);

        auto it = dungeontileMap.find(groundTile);
        if (it != dungeontileMap.end())
            return it->second;
        return MAP_DNG0_CON;
    }

    fromShip = toShip = false;
    if (c->transportContext == TRANSPORT_SHIP)
        fromShip = true;
    else {
        const Object* objUnder = loc->map->objectAt(loc->coords);
        if (objUnder && objUnder->tile.getTileType()->isShip())
            fromShip = true;
    }
    if (obj->tile.getTileType()->isPirateShip())
        toShip = true;

    if (fromShip && toShip)
        return MAP_SHIPSHIP_CON;

    /* We can fight creatures and townsfolk */
    if (obj->objType != Object::UNKNOWN) {
        const Tile *tileUnderneath = loc->map->tileTypeAt(obj->coords, WITHOUT_OBJECTS);

        if (toShip)
            return MAP_SHORSHIP_CON;
        else if (fromShip && tileUnderneath->isWater())
            return MAP_SHIPSEA_CON;
        else if (tileUnderneath->isWater())
            return MAP_SHORE_CON;
        else if (fromShip && !tileUnderneath->isWater())
            return MAP_SHIPSHOR_CON;
    }

    if (tileMap.empty())
        _relateMapsToTiles(tileMap, combatMapId, sizeof(combatMapId),
                           Tile::sym.combatMaps);

    auto it = tileMap.find(groundTile);
    if (it != tileMap.end())
        return it->second;
    return MAP_BRICK_CON;
}

/*
 * STAGE_COMBAT
 */
void* combat_enter(Stage* st, void* args) {
    CombatController* cc = (CombatController*) args;
    cc->listenerId = gs_listen(1<<SENDER_PARTY, combatNotice, cc);
    cc->beginCombat(st);
    return cc;
}

void combat_leave(Stage* st) {
    CombatController* cc = (CombatController*) st->data;
    gs_unplug(cc->listenerId);
}

int combat_dispatch(Stage* st, const InputEvent* ev) {
    if (ev->type == IE_KEY_PRESS) {
        CombatController* cc = (CombatController*) st->data;
        return cc->keyPressed(ev->n);
    }
    return 0;
}
