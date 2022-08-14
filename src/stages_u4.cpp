//#include "stage.h"
//#include "event.h"


static int anykey_dispatch(Stage* st, const InputEvent* ev)
{
    switch (ev->type) {
        case IE_KEY_PRESS:
            stage_done();
            return 1;

        case IE_MOUSE_PRESS:
            if (ev->n == CMOUSE_LEFT) {
                stage_done();
                return 1;
            }
            break;
    }
    return 0;
}


#define EXTERN_ELD(name) \
    extern void* name ## _enter(Stage*, void*); \
    extern void name ## _leave(Stage*); \
    extern int name ## _dispatch(Stage*, const InputEvent*);

#define EXTERN_EL_(name) \
    extern void* name ## _enter(Stage*, void*); \
    extern void name ## _leave(Stage*);

#define EXTERN_E_D(name) \
    extern void* name ## _enter(Stage*, void*); \
    extern int name ## _dispatch(Stage*, const InputEvent*);

#define EXTERN_E__(name) \
    extern void* name ## _enter(Stage*, void*); \

#define EXTERN___D(name) \
    extern int name ## _dispatch(Stage*, const InputEvent*);

EXTERN_ELD(intro)
EXTERN_E_D(newgame)
EXTERN_ELD(story)
EXTERN_E_D(gate)
EXTERN_E_D(gypsy)
EXTERN_E_D(begin)
EXTERN_E_D(error)
EXTERN_EL_(about)
EXTERN_ELD(game)
EXTERN_ELD(browser)
EXTERN_ELD(setmenu)
EXTERN___D(readc)
EXTERN___D(reads)
EXTERN___D(readdir)
EXTERN___D(readaa)
EXTERN_E_D(wait)
EXTERN_E_D(anykey)
EXTERN_E_D(death)
EXTERN_ELD(combat)
EXTERN_ELD(camp)
EXTERN_ELD(innrest)
EXTERN___D(stats)
EXTERN___D(cheat)
EXTERN_E__(disc)
EXTERN_E__(discB)
EXTERN___D(mixin5)


#define CHILD       STAGE_FLAG_CHILD
#define ROOT        STAGE_FLAG_ROOT
#define FR          STAGE_FLAG_FRAME

const StageDef stagesU4[STAGE_COUNT] = {
  {NULL,
     intro_enter,    intro_leave,    intro_dispatch, ROOT, 1},  // STAGE_INTRO
  {NULL,
   newgame_enter,      nop_leave,  newgame_dispatch, CHILD, 0}, // STAGE_NEWGAME
  {NULL,
     story_enter,    story_leave,    story_dispatch, 0, 0},     // STAGE_STORY
  {NULL,
      gate_enter,      nop_leave,     gate_dispatch, CHILD, 0}, // STAGE_GATE
  {NULL,
     gypsy_enter,      nop_leave,    gypsy_dispatch, FR, 0},    // STAGE_GYPSY
  {NULL,
     begin_enter,      nop_leave,    begin_dispatch, 0, 0},     // STAGE_BEGIN
  {NULL,
     error_enter,      nop_leave,    error_dispatch, CHILD, 12},// STAGE_IERROR
  {NULL,
     about_enter,    about_leave,   anykey_dispatch, CHILD, 0}, // STAGE_ABOUT
  {NULL,
      game_enter,     game_leave,     game_dispatch, ROOT, 1},  // STAGE_PLAY
  {NULL,
   browser_enter,  browser_leave,  browser_dispatch, CHILD, 0}, // STAGE_BROWSER
  {NULL,
   setmenu_enter,  setmenu_leave,  setmenu_dispatch, CHILD, 0}, // STAGE_SETMENU
  {NULL,
       nop_enter,      nop_leave,    readc_dispatch, CHILD, 0}, // STAGE_READC
  {NULL,
       nop_enter,      nop_leave,    reads_dispatch, CHILD, 0}, // STAGE_READS
  {NULL,
       nop_enter,      nop_leave,  readdir_dispatch, CHILD, 0}, // STAGE_READDIR
  {NULL,
       nop_enter,      nop_leave,   readaa_dispatch, CHILD, 0}, // STAGE_READAA
  {NULL,
      wait_enter,      nop_leave,     wait_dispatch, CHILD, 0}, // STAGE_WAIT
  {NULL,
    anykey_enter,      nop_leave,   anykey_dispatch, CHILD, 0}, // STAGE_ANYKEY
  {NULL,
     death_enter,      nop_leave,    death_dispatch, CHILD, 0}, // STAGE_DEATH
  {NULL,
    combat_enter,   combat_leave,   combat_dispatch, CHILD, 0}, // STAGE_COMBAT
  {NULL,
      camp_enter,     camp_leave,     camp_dispatch, CHILD, 0}, // STAGE_CAMP
  {NULL,
   innrest_enter,  innrest_leave,  innrest_dispatch, CHILD, 0}, // STAGE_INNREST
  {NULL,
       nop_enter,      nop_leave,    stats_dispatch, CHILD, 0}, // STAGE_STATS
  {NULL,
       nop_enter,      nop_leave,    cheat_dispatch, CHILD, 0}, // STAGE_CHEAT
  {NULL,
      disc_enter,      nop_leave,      nop_dispatch, CHILD, 0}, // STAGE_DISC
  {NULL,
     discB_enter,      nop_leave,      nop_dispatch, 0, 0},     // STAGE_DISC_B
  {NULL,
       nop_enter,      nop_leave,   mixin5_dispatch, CHILD, 0}, // STAGE_MIXIN5
  {NULL,
       nop_enter,      nop_leave,      nop_dispatch, 0, 0},     // STAGE_FIN
};
