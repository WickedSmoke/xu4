/*
 * object.cpp
 */

#include <algorithm>

#include "object.h"
#include "event.h"
#include "map.h"
#include "tileset.h"
#include "xu4.h"

Object::~Object() {
#ifdef GPU_RENDER
    if (animId != ANIM_UNUSED)
        anim_setState(&xu4.eventHandler->flourishAnim, animId, ANIM_FREE);
#endif
}

bool Object::setDirection(Direction d) {
    return tile.setDirection(d);
}

/*
 * NOTE: This does not set prevCoords.
 */
void Object::placeOnMap(Map* map, const Coords& coords) {
    if (find(maps.begin(), maps.end(), map) == maps.end())
        maps.push_back(map);

    setCoords(coords);

#ifdef GPU_RENDER
    /* Start frame animation */
    if (animId == ANIM_UNUSED) {
        const Tile* tileDef = map->tileset->get(tile.id);
        if (tileDef)
            animId = tileDef->startFrameAnim();
    }
#endif
}

Map *Object::getMap() {
    if (maps.empty())
        return NULL;
    return maps.back();
}

/*
 * Remove object from any maps that it is a part of.
 *
 * If the object is not a PartyMember then it is also deleted.
 */
void Object::removeFromMaps() {
    size_t size = maps.size();
    for (size_t i = 0; i < size; i++) {
        bool lastMap = (i == size - 1);
        maps[i]->removeObject(this, lastMap);
    }
}


#include "screen.h"
#include "game.h"
void Object::animateMovement()
{
    //TODO abstract movement - also make screen.h and game.h not required
    screenTileUpdate(&xu4.game->mapArea, prevCoords);
    if (screenTileUpdate(&xu4.game->mapArea, coords))
        screenWait(1);
}
