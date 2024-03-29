/*
 * savegame.cpp
 */

#include <assert.h>
#include <stdlib.h>
#include "savegame.h"


static int writeInt(uint32_t i, FILE *f) {
    if (fputc(i & 0xff, f) == EOF ||
        fputc((i >> 8) & 0xff, f) == EOF ||
        fputc((i >> 16) & 0xff, f) == EOF ||
        fputc((i >> 24) & 0xff, f) == EOF)
        return 0;
    return 1;
}

static int writeShort(uint16_t s, FILE *f) {
    if (fputc(s & 0xff, f) == EOF ||
        fputc((s >> 8) & 0xff, f) == EOF)
        return 0;
    return 1;
}

static int writeChar(uint8_t c, FILE *f) {
    if (fputc(c, f) == EOF)
        return 0;
    return 1;
}

static int readInt(uint32_t *i, FILE *f) {
    *i = fgetc(f);
    *i |= (fgetc(f) << 8);
    *i |= (fgetc(f) << 16);
    *i |= (fgetc(f) << 24);
    return 1;
}

static int readShort(uint16_t *s, FILE *f) {
    *s = fgetc(f);
    *s |= (fgetc(f) << 8);
    return 1;
}

static int readChar(uint8_t *c, FILE *f) {
    *c = fgetc(f);
    return 1;
}


int SaveGame::write(FILE *f) const {
    int i;

    if (!writeInt(unknown1, f) ||
        !writeInt(moves, f))
        return 0;

    for (i = 0; i < 8; i++) {
        if (!players[i].write(f))
            return 0;
    }

    if (!writeInt(food, f) ||
        !writeShort(gold, f))
        return 0;

    for (i = 0; i < 8; i++) {
        if (!writeShort(karma[i], f))
            return 0;
    }

    if (!writeShort(torches, f) ||
        !writeShort(gems, f) ||
        !writeShort(keys, f) ||
        !writeShort(sextants, f))
        return 0;

    for (i = 0; i < ARMR_MAX; i++) {
        if (!writeShort(armor[i], f))
            return 0;
    }

    for (i = 0; i < WEAP_MAX; i++) {
        if (!writeShort(weapons[i], f))
            return 0;
    }

    for (i = 0; i < REAG_MAX; i++) {
        if (!writeShort(reagents[i], f))
            return 0;
    }

    for (i = 0; i < SPELL_MAX; i++) {
        if (!writeShort(mixtures[i], f))
            return 0;
    }

    if (!writeShort(items, f) ||
        !writeChar(x, f) ||
        !writeChar(y, f) ||
        !writeChar(stones, f) ||
        !writeChar(runes, f) ||
        !writeShort(members, f) ||
        !writeShort(transport, f) ||
        !writeShort(balloonstate, f) ||
        !writeShort(trammelphase, f) ||
        !writeShort(feluccaphase, f) ||
        !writeShort(shiphull, f) ||
        !writeShort(lbintro, f) ||
        !writeShort(lastcamp, f) ||
        !writeShort(lastreagent, f) ||
        !writeShort(lastmeditation, f) ||
        !writeShort(lastvirtue, f) ||
        !writeChar(dngx, f) ||
        !writeChar(dngy, f) ||
        !writeShort(orientation, f) ||
        !writeShort(dnglevel, f) ||
        !writeShort(location, f))
        return 0;

    return 1;
}

int SaveGame::read(FILE *f) {
    int i;

    if (!readInt(&unknown1, f) ||
        !readInt(&moves, f))
        return 0;

    for (i = 0; i < 8; i++) {
        if (!players[i].read(f))
            return 0;
    }

    if (!readInt((unsigned int*)&food, f) ||
        !readShort((unsigned short*)&gold, f))
        return 0;

    for (i = 0; i < 8; i++) {
        if (!readShort((unsigned short*)&(karma[i]), f))
            return 0;
    }

    if (!readShort((unsigned short*)&torches, f) ||
        !readShort((unsigned short*)&gems, f) ||
        !readShort((unsigned short*)&keys, f) ||
        !readShort((unsigned short*)&sextants, f))
        return 0;

    for (i = 0; i < ARMR_MAX; i++) {
        if (!readShort((unsigned short*)&(armor[i]), f))
            return 0;
    }

    for (i = 0; i < WEAP_MAX; i++) {
        if (!readShort((unsigned short*)&(weapons[i]), f))
            return 0;
    }

    for (i = 0; i < REAG_MAX; i++) {
        if (!readShort((unsigned short*)&(reagents[i]), f))
            return 0;
    }

    for (i = 0; i < SPELL_MAX; i++) {
        if (!readShort((unsigned short*)&(mixtures[i]), f))
            return 0;
    }

    if (!readShort(&items, f) ||
        !readChar(&x, f) ||
        !readChar(&y, f) ||
        !readChar(&stones, f) ||
        !readChar(&runes, f) ||
        !readShort(&members, f) ||
        !readShort(&transport, f) ||
        !readShort(&balloonstate, f) ||
        !readShort(&trammelphase, f) ||
        !readShort(&feluccaphase, f) ||
        !readShort(&shiphull, f) ||
        !readShort(&lbintro, f) ||
        !readShort(&lastcamp, f) ||
        !readShort(&lastreagent, f) ||
        !readShort(&lastmeditation, f) ||
        !readShort(&lastvirtue, f) ||
        !readChar(&dngx, f) ||
        !readChar(&dngy, f) ||
        !readShort(&orientation, f) ||
        !readShort(&dnglevel, f) ||
        !readShort(&location, f))
        return 0;

    /* workaround of U4DOS bug to retain savegame compatibility */
    if (location == 0 && dnglevel == 0)
        dnglevel = 0xFFFF;

    return 1;
}

void SaveGame::init(const SaveGamePlayerRecord *avatarInfo) {
    int i;

    unknown1 = 0;
    moves = 0;

    players[0] = *avatarInfo;
    for (i = 1; i < 8; i++)
        players[i].init();

    food = 0;
    gold = 0;

    for (i = 0; i < 8; i++)
        karma[i] = 20;

    torches = 0;
    gems = 0;
    keys = 0;
    sextants = 0;

    for (i = 0; i < ARMR_MAX; i++)
        armor[i] = 0;

    for (i = 0; i < WEAP_MAX; i++)
        weapons[i] = 0;

    for (i = 0; i < REAG_MAX; i++)
        reagents[i] = 0;

    for (i = 0; i < SPELL_MAX; i++)
        mixtures[i] = 0;

    items = 0;
    x = 0;
    y = 0;
    stones = 0;
    runes = 0;
    members = 1;
    transport = 0x1f;
    balloonstate = 0;
    trammelphase = 0;
    feluccaphase = 0;
    shiphull = 50;
    lbintro = 0;
    lastcamp = 0;
    lastreagent = 0;
    lastmeditation = 0;
    lastvirtue = 0;
    dngx = 0;
    dngy = 0;
    orientation = 0;
    dnglevel = 0xFFFF;
    location = 0;
}

int SaveGamePlayerRecord::write(FILE *f) const {
    int i;

    if (!writeShort(hp, f) ||
        !writeShort(hpMax, f) ||
        !writeShort(xp, f) ||
        !writeShort(str, f) ||
        !writeShort(dex, f) ||
        !writeShort(intel, f) ||
        !writeShort(mp, f) ||
        !writeShort(unknown, f) ||
        !writeShort((unsigned short)weapon, f) ||
        !writeShort((unsigned short)armor, f))
        return 0;

    for (i = 0; i < 16; i++) {
        if (!writeChar(name[i], f))
            return 0;
    }

    if (!writeChar((unsigned char)sex, f) ||
        !writeChar((unsigned char)klass, f) ||
        !writeChar((unsigned char)status, f))
        return 0;

    return 1;
}

int SaveGamePlayerRecord::read(FILE *f) {
    int i;
    unsigned char ch;
    unsigned short s;

    if (!readShort(&hp, f) ||
        !readShort(&hpMax, f) ||
        !readShort(&xp, f) ||
        !readShort(&str, f) ||
        !readShort(&dex, f) ||
        !readShort(&intel, f) ||
        !readShort(&mp, f) ||
        !readShort(&unknown, f))
        return 0;

    if (!readShort(&s, f))
        return 0;
    weapon = (WeaponType) s;
    if (!readShort(&s, f))
        return 0;
    armor = (ArmorType) s;

    for (i = 0; i < 16; i++) {
        if (!readChar((unsigned char *) &(name[i]), f))
            return 0;
    }

    if (!readChar(&ch, f))
        return 0;
    sex = (SexType) ch;
    if (!readChar(&ch, f))
      return 0;
    klass = (ClassType) ch;
    if (!readChar(&ch, f))
        return 0;
    status = (StatusType) ch;

    return 1;
}

void SaveGamePlayerRecord::init() {
    int i;

    hp = 0;
    hpMax = 0;
    xp = 0;
    str = 0;
    dex = 0;
    intel = 0;
    mp = 0;
    unknown = 0;
    weapon = WEAP_HANDS;
    armor = ARMR_NONE;

    for (i = 0; i < 16; i++)
      name[i] = '\0';

    sex = SEX_MALE;
    klass = CLASS_MAGE;
    status = STAT_GOOD;
}

int saveGameMonstersWrite(const SaveGameMonsterRecord *monsterTable, FILE *f) {
    int i, max;

    if (monsterTable) {
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].tile, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].x, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].y, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].prevTile, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].prevx, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].prevy, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].level, f)) return 0;
        for (i = 0; i < MONSTERTABLE_SIZE; i++)
            if (!writeChar(monsterTable[i].unused, f)) return 0;
    }
    else {
        max = MONSTERTABLE_SIZE * 8;
        for (i = 0; i < max; i++)
            if (!writeChar((unsigned char)0, f)) return 0;
    }
    return 1;
}

int saveGameMonstersRead(SaveGameMonsterRecord *monsterTable, FILE *f) {
    int i;

    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].tile, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].x, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].y, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].prevTile, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].prevx, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].prevy, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].level, f)) return 0;
    for (i = 0; i < MONSTERTABLE_SIZE; i++)
        if (!readChar(&monsterTable[i].unused, f)) return 0;

    return 1;
}

#ifndef SAVE_UTIL
#include "settings.h"
#include "xu4.h"

/*
 * Set xu4.saveGame to a new loaded game.  If loading fails or there are no
 * players defined then set xu4.errorMessage and return NULL.
 */
SaveGame* saveGameLoad() {
    SaveGame* sg = NULL;
    FILE* fp = fopen((xu4.settings->getUserPath() + PARTY_SAV).c_str(), "rb");
    if (fp) {
        sg = new SaveGame;
        sg->read(fp);
        fclose(fp);

        // Make sure there are players in party.sav --
        // In the Ultima Collection CD, party.sav exists, but does
        // not contain valid info to journey onward

        if (sg->members < 1) {
            delete sg;
            sg = NULL;
        }
    }

    if (sg) {
        delete xu4.saveGame;
        xu4.saveGame = sg;
    } else {
        xu4.errorMessage = "Initiate a new game first!";
    }
    return sg;
}
#endif

//--------------------------------------

#ifndef SAVE_UTIL
#include "config.h"
#include "tileset.h"

static const int dngTableLen = 16+4;    // DungeonToken + Magic fields

void UltimaSaveIds::alloc(int ucount, int mcount,
                          Config* cfg, const Tileset* tiles) {
    // Use one block of memory for all tables.
    size_t msize = (ucount + dngTableLen) * sizeof(TileId);
    moduleIdTable = (TileId*) malloc( msize + mcount );
    moduleIdDngTable = moduleIdTable + ucount;
    ultimaIdTable = ((uint8_t*) moduleIdTable) + msize;

    uiCount = ucount;
    miCount = mcount;

    // Fill moduleIdDngTable to match DNGMAP.SAV values.
    Symbol dngMapSymbols[dngTableLen];
    cfg->internSymbols(dngMapSymbols, dngTableLen,
        "brick_floor up_ladder down_ladder up_down_ladder\n"
        "chest ceiling_hole floor_hole magic_orb\n"
        "ceiling_hole fountain poison_field dungeon_altar\n"
        "dungeon_door dungeon_room secret_door brick_wall\n"
        "poison_field energy_field fire_field sleep_field\n");
    for (int i = 0; i < dngTableLen; ++i) {
        const Tile* tile = tiles->getByName(dngMapSymbols[i]);
        moduleIdDngTable[i] = tile ? tile->id : 0;
    }
}

void UltimaSaveIds::free() {
    ::free(moduleIdTable);
}

void UltimaSaveIds::addId(uint8_t uid, int frames, TileId mid) {
    assert(mid < miCount);
    assert(uid < uiCount);
    ultimaIdTable[mid] = uid;
    for (int i = 0; i < frames; ++i)
        moduleIdTable[uid+i] = mid;
}

MapTile UltimaSaveIds::moduleId(uint8_t uid) const {
    const TileId* entry = moduleIdTable + uid;
    TileId mid = *entry;
    int frame = 0;
    if (uid > 15) {     // We know the early entries are single frames.
        while (entry[-1] == mid) {
            ++frame;
            --entry;
        }
    }
    return MapTile(mid, frame);
}

uint8_t UltimaSaveIds::ultimaId(const MapTile& tile) const {
    int uid, uidF;
    TileId mid = tile.id;

    assert(mid < uiCount);
    if (mid >= uiCount)
        return 0;
    uid = ultimaIdTable[ mid ];

    // Check if frame is more than what the original game supports.
    // If so, treat it as frame 0.

    if (tile.frame > 4)
        return uid;
    uidF = uid + tile.frame;
    if (moduleIdTable[ uidF ] != mid)
        return uid;

    return uidF;
}

/*
 * Convert DNGMAP.SAV encoded map values to their matching module id.
 */
TileId UltimaSaveIds::dngMapToModule(uint8_t u4DngId) const {
    int i = u4DngId >> 4;
    if (i == 0xA)
        i = 16 + (u4DngId & 3);         // Magic fields at end of table.
    return moduleIdDngTable[i];
}

/*
 * Convert module TileIds to DNGMAP.SAV encoded map values.
 * NOTE: This does not handle traps (0x8?), fountains (0x9?), & rooms (0xD?)!
 * Return zero for any non-dungeon or unhandled tiles.
 */
uint8_t UltimaSaveIds::moduleToDngMap(TileId modId) const {
    int i;
    for (i = 0; i < dngTableLen; ++i) {
        if (modId == moduleIdDngTable[i]) {
            if (i < 16)
                return i << 4;
            else
                return 0xA0 + (i - 16); // Magic fields at end of table.
        }
    }
    return 0;
}
#endif
