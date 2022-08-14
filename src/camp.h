/*
 * $Id$
 */

#ifndef CAMP_H
#define CAMP_H

#include "combat.h"

class CampController : public CombatController {
public:
    CampController();
    virtual void beginCombat(Stage*);
    virtual void endCombat(bool adjustKarma);

    void wakeUp();
};

class InnController : public CombatController {
public:
    InnController();
    virtual void awardLoot();

    void maybeMeetIsaac();
    bool maybeAmbush();
};

#endif
