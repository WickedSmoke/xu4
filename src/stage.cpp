/*
  XU4 Stage State Machine
  Copyright 2022 Karl Robillard

  This file is part of XU4.

  XU4 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  XU4 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with XU4.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "event.h"      // InputEvent
//#include "stage.h"
#include "xu4.h"        // rclock

#define STAGE_DEPTH 6
#define STAGE_UNSET -1
#define assigned(id)    (id >= 0)

#if 1
#define S_TRACE(...)    printf(__VA_ARGS__)
#else
#define S_TRACE()
#endif

// Default stage methods.
void* nop_enter(Stage*, void* args) { return args; }
void  nop_leave(Stage*) {}
int   nop_dispatch(Stage*, const InputEvent*) { return 0; }

static const StageDef removedDef = {
    NULL, nop_enter, nop_leave, nop_dispatch, 0, 0
};

static void* nextStageArgs;
static int   nextStage;
static int   stageTop;
static Stage stageStack[STAGE_DEPTH];
static const StageDef* stageTable;


/**
  \file stage.h
  The Stage state machine (stack automaton) system.

  \def stage_done
  Leave stage and remove it from the run stack at the next stage_transition().

  \def stage_abort
  This is the same as stage_done(), but processing of the remaining stages is
  halted.

  \def stage_exit
  Leave all running stages and pop them from the run stack at the next
  stage_transition().

  \def stage_run
  Signal tranfer to another stage.

  \def stage_runArgs
  Signal tranfer to another stage with arguments.

  \var StageDef::enter
  Called by stage_transition() when a stage is added to the run stack.

  \var StageDef::leave
  Called by stage_transition() when a stage is removed from the run stack.

  NOTE: This method must not call any stage run or exception functions
  (stage_run(), stage_done(), etc)!

  \var StageDef::dispatch
  The event handler callback for a Stage.

  All stages on the run stack recieve timer events.  Only the top stage
  (the most recently run) is sent input events.
*/


/**
 * Set the stage table and initialize the stack.
 */
void stage_init(const StageDef* stages)
{
    stageTable = stages;
    nextStage = STAGE_UNSET;
    stageTop = -1;
}

/*
int stage_id(const Stage* st)
{
    if (! st) {
        assert(stageTop >= 0);
        st = stageStack[ stageTop ];
    }
    return st - stageTable;
}
*/

void stage_startMsecTimer(Stage* st, uint32_t millisec)
{
    st->msecInterval = millisec;
    st->msecTimeout  = xu4.rclock + millisec;
}

void stage_stopMsecTimer(Stage* st)
{
    st->msecInterval = 0;
}

/**
 * Run a stage from a global context (outside of a stage method).
 * This is used to kickstart the first stage or throw an exit exception.
 */
void stage_runG(int id, void* args)
{
    // Ignore command if an exit is in progress.
    if (nextStage != STAGE_EX_EXIT) {
        nextStageArgs = args;
        nextStage = id;
    }
}

/**
 * Signal tranfer to another stage or throw a done/abort exception.
 *
 * Normally the stage_run(), stage_done(), & stage_abort() macros are used to
 * invoke this function.
 *
 * If other stage_run* calls are done before stage_transition() then only the
 * last one will take effect (the prior ones are ignored).
 */
void stage_runS(Stage* st, int id)
{
    stage_runSArgs(st, id, (id >= STAGE_EX_DONE) ? st : NULL);
}

/**
 * Signal tranfer to another stage with arguments.
 *
 * Normally the stage_runArgs() macro is used to invoke this function.
 */
void stage_runSArgs(Stage* st, int id, void* args)
{
    // Ignore command if an exit is in progress.
    if (nextStage == STAGE_EX_EXIT)
        return;

    nextStageArgs = args;
    nextStage = id;

    /*
      If id is not for a child then immediately disable event handling for
      the current stage.
      NOTE: If run is called inside an application event handler loop then
            all queued input events will be lost.  New events will not be
            seen until the next stage_transition() occurs.
    */

    if (id < STAGE_EX_DONE) {
        if (stageTable[id].flags & STAGE_FLAG_CHILD)
            return;
    }
    st->dispatch = nop_dispatch;
}

/*
void stage_loadResources(ResourceList* prev, ResourceList* next)
{
}
*/

#define LEAVE(st)    st->def->leave(st)

// Leave all stages on the stack lower than or equal to top.
static void stage_unwind(int top)
{
    while (top >= 0) {
        Stage* st = stageStack + top;
        S_TRACE("Stage leave (unwind) %d\n", top);
        LEAVE(st);
        --top;
    }
    stageTop = -1;
}

/**
 * Change to the stage indicated by the most recent stage_run* call.
 * Leave and enter methods are called for all stages involved in the transition.
 *
 * \return Pointer to current stage or NULL if there are no more to run.
 *         NULL is returned if an abort exception occurs, but stages may still
 *         remain on the run stack.
 */
const Stage* stage_transition()
{
    if (assigned(nextStage)) {
        /*
          Multiple stage enter methods can be called here.
          The leaveTop variable ensures that we only call leave methods for
          the stages present on the stack before we begin entering new stages.
        */
        int leaveTop = stageTop;
        int leaveSibling = 1;

transfer:
        if (nextStage >= STAGE_EX_DONE)
        {
            // Handle the done, abort, & exit exceptions.

            if (nextStage == STAGE_EX_EXIT) {
                stage_unwind(leaveTop);
                leaveTop = -1;
            } else {
                Stage* st;
                int stackPos;
                int abort = (nextStage == STAGE_EX_ABORT);

                nextStage = STAGE_UNSET;
                assert(stageTop >= 0);

                st = (Stage*) nextStageArgs;
                stackPos = st - stageStack;

                if (stackPos <= leaveTop) {
                    S_TRACE("Stage leave (%s) %d\n",
                            abort ? "abort" : "done", stackPos);
                    LEAVE(st);
                }

                if (stackPos == stageTop) {
                    do {
                        --stageTop;
                    } while (stageTop >= 0 &&
                             stageStack[stageTop].def == &removedDef);
                } else {
                    // Not popped so replace stage with an inert entry.
                    st->def          = &removedDef;
                    st->dispatch     = removedDef.dispatch;
                    st->msecInterval = 0;
                    st->tickInterval = 0;
                }

                if (abort)
                    return NULL;
            }
        }
        else
        {
            // Transfer control to a child, sibling, or root stage.

            Stage* st;
            const StageDef* def = stageTable + nextStage;
            nextStage = STAGE_UNSET;

            if (def->flags & STAGE_FLAG_CHILD) {
                ++stageTop;
                assert(stageTop < STAGE_DEPTH);
            } else if (def->flags & STAGE_FLAG_ROOT) {
                stage_unwind(leaveTop);
                leaveTop = -1;
                stageTop = 0;
            } else {
                if (stageTop < 0)
                    stageTop = 0;
                else if (leaveSibling) {
                    leaveSibling = 0;
                    --leaveTop;
                    st = stageStack + stageTop;
                    S_TRACE("Stage leave (sibling) %d\n", stageTop);
                    LEAVE(st);
                }
            }

            S_TRACE("Stage enter %d %ld\n", stageTop, def - stageTable);

            st = stageStack + stageTop;
            st->def          = def;
            st->dispatch     = def->dispatch;
            //st->data         = NULL;
            st->msecInterval = 0;
            st->tickInterval = def->tickInterval;
            st->tickCount    = 0;

            st->data = def->enter(st, nextStageArgs);

            // Enter may immediately transfer to another stage.
            if (assigned(nextStage))
                goto transfer;
        }
    }

    return (stageTop < 0) ? NULL : stageStack + stageTop;
}

Stage* stage_current()
{
    return (stageTop < 0) ? NULL : stageStack + stageTop;
}

/**
 * Send IE_CYCLE_TIMER, IE_MSEC_TIMER, & IE_FRAME_UPDATE events to active
 * stages.
 *
 * \param gameCycle     Increment tick counters and send IE_CYCLE_TIMER
 *                      events if this is non-zero.
 */
void stage_tick(int gameCycle)
{
    InputEvent event;
    Stage* st;
    int i;

    /*
      FIXME: Timers can cause transitions from stages below the stack top.
             Exceptions and root transfers should be processed OK, but a
             child or sibling transition will break things.
    */

    if (gameCycle) {
        event.type = IE_CYCLE_TIMER;
        st = stageStack;
        for(i = 0; i <= stageTop; ++st, ++i) {
            if (st->tickInterval) {
                //printf("KR tick %d %d\n", i, st->tickCount);
                if (++st->tickCount >= st->tickInterval) {
                    st->tickCount = 0;
                    st->dispatch(st, &event);
                }
            }
        }
    }

    event.type = IE_MSEC_TIMER;
    st = stageStack;
    for(i = 0; i <= stageTop; ++st, ++i) {
        if (st->msecInterval) {
            if (xu4.rclock >= st->msecTimeout) {
                st->msecTimeout += st->msecInterval;
                st->dispatch(st, &event);
            }
        }
    }

    // Do frame updates as a batch and in stack order to so that the update
    // methods are called in painter's algorithm order.

    event.type = IE_FRAME_UPDATE;
    st = stageStack;
    for(i = 0; i <= stageTop; ++st, ++i) {
        if (st->def->flags & STAGE_FLAG_FRAME)
            st->dispatch(st, &event);
    }
}
