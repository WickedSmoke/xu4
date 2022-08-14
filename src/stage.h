#ifndef STAGE_H
#define STAGE_H

#include <stdint.h>

#define STAGE_FLAG_CHILD    1
#define STAGE_FLAG_ROOT     2
#define STAGE_FLAG_FRAME    4
#define STAGE_FLAG_USER     0xff00

#define STAGE_EX_DONE   0x7ffd
#define STAGE_EX_ABORT  0x7ffe
#define STAGE_EX_EXIT   0x7fff

#define stage_done()    stage_runS(st, STAGE_EX_DONE)
#define stage_abort()   stage_runS(st, STAGE_EX_ABORT)
#define stage_exit()    stage_runG(STAGE_EX_EXIT, NULL)
#define stage_run(id)   stage_runS(st, id)
#define stage_runArgs(id,arg)   stage_runSArgs(st, id, arg)

/*
struct ResourceDest {
    uint16_t system;
    uint16_t slot;
};

struct ResourceList {
    const char** path;
    const struct ResourceDest* dest;
    uint32_t count;
};
*/

struct InputEvent;
typedef struct Stage Stage;

struct StageDef {
    //const struct ResourceList* resources;
    void*   resources;
    void*   (*enter) (Stage*, void*);
    void    (*leave) (Stage*);
    int     (*dispatch) (Stage*, const InputEvent*);
    uint16_t flags;
    uint16_t tickInterval;
};

struct Stage {
    const StageDef* def;
    int (*dispatch) (Stage*, const InputEvent*);
    void* data;
    uint32_t msecInterval;
    uint32_t msecTimeout;
    uint16_t tickInterval;
    uint16_t tickCount;
    uint16_t dstate;        // User defined (dispatch state).
    uint16_t _pad;
};

void         stage_init(const StageDef* stages);
//int          stage_id(const Stage*);
void         stage_runG(int id, void* args);
void         stage_runS(Stage*, int id);
void         stage_runSArgs(Stage*, int id, void* args);
const Stage* stage_transition();
Stage*       stage_current();
void         stage_startMsecTimer(Stage*, uint32_t millisec);
void         stage_stopMsecTimer(Stage*);
void         stage_tick(int gameCycle);

void* nop_enter(Stage*, void*);
void  nop_leave(Stage*);
int   nop_dispatch(Stage*, const InputEvent*);

#endif  // STAGE_H
