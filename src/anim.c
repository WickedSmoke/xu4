#include <stdio.h>
#include <stdlib.h>
#include "anim.h"

extern int xu4_random(int);

#define dprint  printf

enum AnimType {
    ANIM_CYCLE_I,
    ANIM_CYCLE_RANDOM_I,
    ANIM_LINEAR_F2,
};

typedef struct {
    uint16_t animType;
    uint16_t state;
    uint16_t loops;
    uint16_t _pad;
    uint32_t finishId;
    float duration;
    float ctime;
    union {
        struct {
            int start;
            int end;
            int current;
            int chance;
        } i;
        struct {
            float start[2];
            float end[2];
            float current[2];
        } f2;
    } var;
}
AnimatedValue;

#define BANK(an)        ((AnimatedValue*) an->bank)
#define ANIM_FINISHED   (ANIM_PAUSED+1)

#define FREE_TERM   0xffff
#define NEXT_FREE   finishId

static void anim_nopFinish(void* fdata, uint32_t fid)
{
    (void) fdata;
    (void) fid;
}

/*
 * Allocate memory for the specified maximum number of animated values.
 *
 * Return non-zero if memory allocation is successful.
 */
int anim_init(Animator* an, int count, void (*finishFunc)(void*, uint32_t),
              void* fdata)
{
    an->finish = finishFunc ? finishFunc : anim_nopFinish;
    an->finishData = fdata;
    an->bank = malloc(sizeof(AnimatedValue) * count);
    an->avail = an->bank ? count : 0;
    anim_clear(an);
    return an->avail;
}

/*
 * Free memory for all animated values.
 */
void anim_free(Animator* an)
{
    free(an->bank);
    an->bank = NULL;
    an->avail = an->used = 0;
}

/*
 * Stop all animations and return them to the free pool.
 */
void anim_clear(Animator* an)
{
    an->used = an->firstFree = 0;
    if (an->avail) {
        AnimatedValue* it = BANK(an);
        int end = an->avail - 1;
        int i;

        for (i = 1; i < end; ++i) {
            it->NEXT_FREE = i;      // Point to next free value.
            it->state = ANIM_FREE;
            ++it;
        }
        it->NEXT_FREE = FREE_TERM;
        it->state = ANIM_FREE;
    }
}

static AnimId anim_alloc(Animator* an)
{
    AnimId id = an->firstFree;
    if (id != FREE_TERM) {
        an->firstFree = BANK(an)[id].NEXT_FREE;
        if (id >= an->used)
            an->used = id + 1;
    }
    return id;
}

static void anim_release(Animator* an, AnimatedValue* it)
{
    AnimatedValue* bank = BANK(an);

    //dprint("anim_release %ld\n", it - bank);

    it->state = ANIM_FREE;

    // Link into the free list.
    it->NEXT_FREE = an->firstFree;
    an->firstFree = it - bank;

    // Adjust used downward to the next active value.
    if (it == (bank + an->used - 1)) {
        while (it != bank && it->state == ANIM_FREE)
            --it;
        an->used = it - bank;
    }
}

/*
 * Advance all playing animations by the given time.
 */
void anim_advance(Animator* an, float seconds)
{
    AnimatedValue* it  = BANK(an);
    AnimatedValue* end = it + an->used;
    AnimatedValue* firstDone = NULL;
    AnimatedValue* lastDone;

    while (it != end) {
        if (it->state == ANIM_PLAYING) {
            // Advance time.
            it->ctime += seconds;
            if (it->ctime >= it->duration) {
                if (it->loops == ANIM_FOREVER) {
                    it->ctime -= it->duration;
                } else {
                    --it->loops;
                    if (it->loops) {
                        it->ctime -= it->duration;
                    } else {
                        it->state = ANIM_FINISHED;
                        if (! firstDone)
                            firstDone = it;
                        lastDone = it + 1;
                    }
                }
            }

            // Update value.
            switch (it->animType) {
                case ANIM_CYCLE_RANDOM_I:
                    if (it->var.i.chance < xu4_random(100)) {
                        int n = it->var.i.current + 1;
                        it->var.i.current =
                            (n < it->var.i.end) ? n : it->var.i.start;
                    }
                    break;
            }
        }
        ++it;
    }

    // Invoke the finish handler and release the slot for completed animations.
    if (firstDone) {
        for (it = firstDone; it != lastDone; ++it) {
            if (it->state == ANIM_FINISHED) {
                if (it->finishId)
                    an->finish(an->finishData, it->finishId);
                anim_release(an, it);
            }
        }
    }
}

static void anim_stdInit(AnimatedValue* it, float dur, int loops, uint32_t fid)
{
    it->state    = ANIM_PLAYING;
    it->loops    = loops;
    it->_pad     = 0;
    it->finishId = fid;
    it->duration = dur;
    it->ctime    = 0.0f;
}

AnimId anim_startCycleRandomI(Animator* an, float dur, int loops, uint32_t fid,
                              int start, int end, int chance)
{
    AnimId id = anim_alloc(an);
    if (id != FREE_TERM) {
        AnimatedValue* it = BANK(an) + id;
        it->animType = ANIM_CYCLE_RANDOM_I;
        anim_stdInit(it, dur, loops, fid);
        it->var.i.start  = start;
        it->var.i.end    = end;
        it->var.i.current = start;
        it->var.i.chance = chance;

        //dprint("anim_start %d dur:%f chance:%d\n", id, dur, chance);
    }
    return id;
}

/*
 * Pause, unpause, or stop an animation.
 */
void anim_setState(Animator* an, AnimId id, int animState)
{
    AnimatedValue* it = BANK(an) + id;
    if (animState == ANIM_FREE) {
        if (it->state != ANIM_FREE)
            anim_release(an, it);
    } else
        it->state = animState;
}

int anim_valueI(const Animator* an, AnimId id)
{
    return BANK(an)[id].var.i.current;
}

float* anim_valueF2(const Animator* an, AnimId id)
{
    return BANK(an)[id].var.f2.current;
}
