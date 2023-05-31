/*
 * combat.h
 */

#ifndef COMBAT_H
#define COMBAT_H

#include "game.h"
#include "party.h"

#define AREA_CREATURES   16
#define AREA_PLAYERS    8

class CombatMap;
class Creature;
class Dungeon;
class MoveEvent;
class Weapon;

typedef enum {
    CA_ATTACK,
    CA_CAST_SLEEP,
    CA_ADVANCE,
    CA_RANGED,
    CA_FLEE,
    CA_TELEPORT
} CombatAction;

/**
 * CombatController class
 */
class CombatController : public TurnController {
public:
    static bool attackHit(const Creature *attacker, const Creature *defender);
    static void engage(MapId mid, const Creature* creatures);
    static void engageDungeon(Dungeon* dng, int room, Direction from);
    static CombatController* init(CombatMap*);

    CombatController() {}
    virtual ~CombatController() {}

    // TurnController Method
    virtual void finishTurn();
    virtual bool isCombatController() const { return true; }

    // Accessor Methods
    bool isCamping()         const { return camping; }
    bool isWinOrLose()       const { return winOrLose; }
    Direction getExitDir()   const { return exitDir; }
    unsigned char getFocus() const { return focus; }
    CombatMap* getMap()      const { return map; }
    const Creature* getCreature() const { return creature; }
    PartyMember** getTroop() { return troop; }
    PartyMember* getCurrentPlayer() { return troop[focus]; }

    void setExitDir(Direction d) { exitDir = d; }
    void setCreature(const Creature* m) { creature = m; }

    // Methods
    virtual bool keyPressed(int key);
    virtual void beginCombat(Stage*);
    virtual void endCombat(bool adjustKarma);

    bool creatureRangedAttack(Creature* attacker, int dir);
    void movePartyMember(MoveEvent &event);

protected:
    virtual void awardLoot();

    void initAltarRoom();
    void initCreature(const Creature *m);
    void fillCreatureTable(const Creature *creature);
    void placeCreatures();
    void placePartyMembers();
    void attack();

    // Properties
    Stage* stage;
    CombatMap *map;
    PartyMember* troop[AREA_PLAYERS];
    unsigned char focus;

    const Creature *creatureTable[AREA_CREATURES];
    const Creature *creature;

    bool camping;
    bool forceStandardEncounterSize;
    bool placeCreaturesOnMap;
    bool winOrLose;
    bool showMessage;
    Direction exitDir;
public:
    int listenerId;

private:
    CombatController(const CombatController&);
    const CombatController &operator=(const CombatController&);

    void initDungeonRoom(int room, Direction from);
    void applyCreatureTileEffects();
    int  initialNumberOfCreatures(const Creature *creature) const;
    bool isWon() const;
    bool isLost() const;
    void moveCreatures();
    void announceActivePlayer();
    bool setActivePlayer(int player);
    bool attackAt(const Coords &coords, PartyMember *attacker, int dir, int range, int distance);
    bool returnWeaponToOwner(const Coords &coords, int distance, int dir,
                             const Weapon *weapon);
};

typedef std::vector<Creature *> CreatureVector;

/**
 * CombatMap class
 */
class CombatMap : public Map {
public:
    CombatMap();

    CreatureVector getCreatures();
    PartyMemberVector getPartyMembers();
    PartyMember* partyMemberAt(Coords coords);
    Creature* creatureAt(Coords coords);

    static MapId mapForTile(const Tile *ground, const Tile *transport, Object *obj);

    // Getters
    bool isDungeonRoom() const      {return dungeonRoom;}
    bool isAltarRoom() const        {return altarRoom != VIRT_NONE;}
    bool isContextual() const       {return contextual;}
    BaseVirtue getAltarRoom() const {return altarRoom;}

    // Setters
    void setAltarRoom(BaseVirtue ar){altarRoom = ar;}
    void setDungeonRoom(bool d)     {dungeonRoom = d;}
    void setContextual(bool c)      {contextual = c;}

    // Properties
protected:
    bool dungeonRoom;
    BaseVirtue altarRoom;
    bool contextual;

public:
    Coords creature_start[AREA_CREATURES];
    Coords player_start[AREA_PLAYERS];
};

CombatMap *getCombatMap(Map *punknown = NULL);

#endif
