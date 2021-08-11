/*
 * map.h
 */

#ifndef MAP_H
#define MAP_H

#include <map>
#include <vector>

#include "coords.h"
#include "direction.h"
#include "object.h"
#include "savegame.h"
#include "types.h"
#include "u4file.h"

#define MAP_IS_OOB(mapptr, c) (((c).x) < 0 || ((c).x) >= (static_cast<int>((mapptr)->width)) || ((c).y) < 0 || ((c).y) >= (static_cast<int>((mapptr)->height)) || ((c).z) < 0 || ((c).z) >= (static_cast<int>((mapptr)->levels)))

class AnnotationMgr;
class Object;
class Creature;
class Tileset;
struct Portal;

typedef std::vector<Portal *> PortalList;
//typedef std::list<int> CompressedChunkList;
typedef std::vector<TileId> MapData;

/* flags */
#define SHOW_AVATAR (1 << 0)
#define NO_LINE_OF_SIGHT (1 << 1)
#define FIRST_PERSON (1 << 2)

/* mapTileAt flags */
#define WITHOUT_OBJECTS     0
#define WITH_GROUND_OBJECTS 1
#define WITH_OBJECTS        2

/**
 * Map class
 */
class Map {
public:
    enum Type {
        WORLD,
        CITY,
        SHRINE,
        COMBAT,
        DUNGEON
    };

    enum BorderBehavior {
        BORDER_WRAP,
        BORDER_EXIT2PARENT,
        BORDER_FIXED
    };

    Map();
    virtual ~Map();

    // Member functions
    virtual const char* getName() const;

    void queryBlocking(int x, int y, uint8_t* blocking, int bw, int bh) const;
    void queryVisible(const Coords &coords, int radius,
                      void (*func)(const Coords*, VisualId, void*),
                      void* user) const;
    const Object* objectAt(const Coords &coords) const;
    Object* objectAt(const Coords &coords) {
        return (Object*) static_cast<const Map*>(this)->objectAt(coords);
    }
    const Portal *portalAt(const Coords &coords, int actionFlags);
    TileId getTileFromData(const Coords &coords) const;
    const Tile* tileTypeAt(const Coords &coords, int withObjects) const;
    void setTileAt(const Coords &coords, TileId tid);
    bool isWorldMap() const;
    bool isEnclosed(const Coords &party);
    class Creature *addCreature(const class Creature *m, const Coords& coords);
    class Object *addObject(MapTile tile, MapTile prevTile, const Coords& coords);
    class Object *addObject(Object *obj, Coords coords);
    void removeObject(const class Object *rem, bool deleteObject = true);
    ObjectDeque::iterator removeObject(ObjectDeque::iterator rem, bool deleteObject = true);
    void clearObjects();
    class Creature *moveObjects(const Coords& avatar);
    int getNumberOfCreatures();
    int getValidMoves(const Coords& from, MapTile transport);
    bool move(Object *obj, Direction d);
    void alertGuards();
    const Coords* getLabel(Symbol name) const;
    const char* labelAt(const Coords&) const;
    void putInBounds(Coords&) const;

    // u4dos compatibility
    void fillMonsterTable(SaveGameMonsterRecord* table) const;
    void fillMonsterTableDungeon(SaveGameMonsterRecord* table) const;

public:
    StringId        fname;
    MapId           id;
    uint8_t         type;
    uint8_t         border_behavior;    // BorderBehavior
    uint8_t         _pad;
    uint16_t        width,
                    height,
                    levels;
    uint16_t        chunk_width,
                    chunk_height;
    uint16_t        flags;
    uint16_t        music;
    unsigned int    offset;

    //CompressedChunkList compressed_chunks;
    PortalList      portals;
    AnnotationMgr  *annotations;
    MapData         data;
#ifdef USE_GL
    uint8_t* chunks;
#endif
    ObjectDeque     objects;
    std::map<Symbol, Coords> labels;
    const Tileset  *tileset;

private:
    // disallow map copying: all maps should be created and accessed
    // through the MapMgr
    Map(const Map &map);
    Map &operator=(const Map &map);

    void findWalkability(Coords coords, int *path_data);
};

inline bool isCity(const Map* map)      { return map->type == Map::CITY; }
inline bool isCombatMap(const Map* map) { return map->type == Map::COMBAT; }
inline bool isDungeon(const Map* map)   { return map->type == Map::DUNGEON; }

Direction map_pathTo(const Coords &a, const Coords &b,
                     int valid_dirs = MASK_DIR_ALL, bool towards = true,
                     const Map *map = NULL);
Direction map_pathAway(const Coords &a, const Coords &b,
                       int valid_dirs = MASK_DIR_ALL);
void map_wrap(Coords&, const Map *map);
void map_move(Coords&, Direction d, const Map *map = NULL);
void map_move(Coords&, int dx, int dy, const Map *map = NULL);
int  map_getRelativeDirection(const Coords& a, const Coords& b, const Map* map = NULL);
int  map_movementDistance(const Coords& a, const Coords &b, const Map *map = NULL);
int  map_distance(const Coords& a, const Coords& b, const Map* map = NULL);

#endif
