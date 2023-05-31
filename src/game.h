/*
 * game.h
 */

#ifndef GAME_H
#define GAME_H

#include <vector>

#include "discourse.h"
#include "event.h"
#include "map.h"
#include "sound.h"
#include "tileview.h"
#include "types.h"

using std::vector;

struct Portal;
class Creature;
class MoveEvent;

typedef enum {
    VIEW_NORMAL,
    VIEW_GEM,
    VIEW_DUNGEON,
    VIEW_CUTSCENE,
    VIEW_CUTSCENE_MAP,
    VIEW_MIXTURES
} ViewMode;

class TurnController {
public:
    virtual void finishTurn() = 0;
    virtual bool isCombatController() const { return false; }
};

struct MouseArea;
struct ScreenState;
struct Stage;
class CombatController;

/**
 * The main game controller that handles basic game flow and keypresses.
 *
 * @todo
 *  <ul>
 *      <li>separate the dungeon specific stuff into another class (subclass?)</li>
 *  </ul>
 */
class GameController : public TurnController {
public:
    GameController();
    virtual ~GameController();

    /* controller functions */
    bool present();
    void conclude();
    bool keyPressed(Stage* st, int key);
    void timerFired();

    /* main game functions */
    void setMap(Map *map, bool saveLocation, const Portal *portal,
                TurnController *turnCompleter = NULL);
    int exitToParentMap();
    MapId combatMapForTile(const Tile *groundTile, Object *obj);
    virtual void finishTurn();

    bool initContext();
    void updateMoons(bool showmoongates);

    static void flashTile(const Coords &coords, MapTile tile, int timeFactor);
    static void flashTile(const Coords &coords, Symbol tilename, int timeFactor);

    uintptr_t stageArgs[4];
    TileView mapArea;
    CombatController* combatCon;
    Discourse vendorDisc;
    Discourse castleDisc;
    bool cutScene;
    std::map<const Tile*, MapId> tileMap;
    std::map<const Tile*, MapId> dungeontileMap;

private:
    static void gameNotice(int, void*, void*);
    static void renderHud(ScreenState* ss, void* data);
    void initScreenWithoutReloadingState();
    void initMoons();

    void avatarMoved(MoveEvent &event);
    void avatarMovedInDungeon(MoveEvent &event);

    void creatureCleanup();
    void checkBridgeTrolls();
    void checkRandomCreatures();
    void checkSpecialCreatures(Direction dir);
    bool checkMoongates();

    bool createBalloon(Map *map);

    float* borderAttr;
    int borderAttrLen;
    const MouseArea* activeAreas;

    friend int game_dispatch(Stage*, const InputEvent*);
};

#define GAME_CON    GameController* gc = xu4.game

/* map and screen functions */
void gameSetViewMode(ViewMode newMode);
void gameUpdateScreen();

/* spell functions */
void castSpell(int player = -1);
void gameSpellEffect(int spell, int player, Sound sound);

/* action functions */
void destroy();
void board();
void fire();
void getChest(int player = -1);
void jimmy();
void opendoor();
bool gamePeerCity(int city, void *data);
void peer(bool useGem = true);
bool fireAt(const Coords &coords, bool originAvatar);
void readyWeapon(int player = -1);

/* creature functions */
bool creatureRangeAttack(const Coords &coords, Creature *m);
bool gameSpawnCreature(const class Creature *m);
void gameDestroyAllCreatures();

/* etc */
void gameBadCommand();
const char* gameGetInput(int maxlen = 30);
int gameGetPlayer(bool canBeDisabled, bool canBeActivePlayer);
void gameGetPlayerForCommand(bool (*commandFn)(int player), bool canBeDisabled, bool canBeActivePlayer);
void gameDamageParty(int minDamage, int maxDamage);
bool gameDamageShip(int minDamage, int maxDamage);
void gameSetActivePlayer(int player);
vector<Coords> gameGetDirectionalActionPath(int dirmask, int validDirections, const Coords &origin, int minDistance, int maxDistance, bool (*blockedPredicate)(const Tile *tile), bool includeBlocked);

#define gameStampCommandTime()      c->lastCommandTime = c->commandTimer
#define gameTimeSinceLastCommand()  ((c->commandTimer - c->lastCommandTime)/1000)

#endif
