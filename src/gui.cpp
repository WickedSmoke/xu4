/*
  XU4 GUI Layout
  Copyright (C) 2022  Karl Robillard

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
#include <cstring>
#include "image32.h"
#include "gpu.h"
#include "gui.h"
#include "screen.h"
#include "xu4.h"

#define ATTR_COUNT  7
#define LO_DEPTH    6
#define MAX_SIZECON 80

enum AlignBits {
    ALIGN_L = 1,
    ALIGN_R = 2,
    ALIGN_T = 4,
    ALIGN_B = 8,
    BACKWARDS = 0x10,
    H_MASK = ALIGN_L | ALIGN_R | 0xf0,
    V_MASK = ALIGN_T | ALIGN_B | 0xf0
};

/*
enum SizePolicy {
    POL_FIXED,      // Use minimum.
    POL_MINIMAL,    // Prefer minimum.
    POL_EXPAND      // Prefer maximum.
};
*/

typedef struct {
    int16_t minW, minH;
    int16_t prefW, prefH;
} SizeCon;

//----------------------------------------------------------------------------

static float* gui_drawRect(float* attr, const int16_t* wbox, float colorIndex)
{
    float rect[4];
    float uvs[4];

    rect[0] = (float) wbox[0];
    rect[1] = (float) wbox[1];
    rect[2] = (float) wbox[2];
    rect[3] = (float) wbox[3];

    gpu_guiClutUV(xu4.gpu, uvs, colorIndex);
    uvs[2] = uvs[0];
    uvs[3] = uvs[1];

    return gpu_emitQuad(attr, rect, uvs);
}

static void button_size(SizeCon* size, TxfDrawState* ds, const uint8_t* text)
{
    float fsize[2];
    txf_emSize(ds->tf, text, strlen((const char*) text), fsize);

    size->minW =
    size->prefW = (int16_t) (fsize[0] * ds->psize + 1.2f * ds->psize);

    size->minH =
    size->prefH = (int16_t) (fsize[1] * ds->psize * 1.6f);
}

static float* widget_button(float* attr, const GuiRect* wbox,
                            const SizeCon* scon, TxfDrawState* ds,
                            const uint8_t* text)
{
    int textW;
    int quadCount;

    attr = gui_drawRect(attr, &wbox->x, 40.0f);

    textW = scon->prefW - (int16_t) (1.2f * ds->psize);
    ds->x = (float) (wbox->x + ((wbox->w - textW) / 2));
    ds->y = (float) wbox->y - ds->tf->descender * ds->psize + 0.3f * ds->psize;
    quadCount = txf_genText(ds, attr + 3, attr, ATTR_COUNT,
                            text, strlen((const char*) text));
    return attr + (quadCount * 6 * ATTR_COUNT);
}

static void label_size(SizeCon* size, TxfDrawState* ds, const uint8_t* text)
{
    float fsize[2];
    txf_emSize(ds->tf, text, strlen((const char*) text), fsize);
    size->minW = size->prefW = (int16_t) (fsize[0] * ds->psize);
    size->minH = size->prefH = (int16_t) (fsize[1] * ds->psize);
}

static float* widget_label(float* attr, const GuiRect* wbox, TxfDrawState* ds,
                           const uint8_t* text)
{
    int quadCount;

    ds->x = (float) wbox->x;
    ds->y = (float) wbox->y - ds->tf->descender * ds->psize;
    quadCount = txf_genText(ds, attr + 3, attr, ATTR_COUNT,
                            text, strlen((const char*) text));
    return attr + (quadCount * 6 * ATTR_COUNT);
}

static void list_size(SizeCon* size, TxfDrawState* ds, StringTable* st)
{
    float fsize[2];
    int rows;
    const char* longText = "<empty>";
    int maxLen = 0;

    if (st->used) {
        const char* text;
        int len;

        for (uint32_t i = 0; i < st->used; ++i) {
            text = sst_stringL(st, i, &len);
            if (len > maxLen) {
                maxLen = len;
                longText = text;
            }
        }
    }

    if (! maxLen)
        maxLen = 7;
    txf_emSize(ds->tf, (const uint8_t*) longText, maxLen, fsize);

    fsize[0] *= ds->psize;
    fsize[1] *= ds->psize;

    rows = (st->used < 3) ? 3 : st->used;

    size->minW = (int16_t) (ds->psize * 4.0f);
    size->minH = 3 * (int16_t) fsize[1];
    size->prefW = (int16_t) fsize[0];
    size->prefH = (int16_t) (fsize[1] * rows);
}

float* widget_list(float* attr, const GuiRect* wbox, TxfDrawState* ds,
                   StringTable* st)
{
    const uint8_t* strings = (const uint8_t*) sst_strings(st);
    const StringEntry* it  = st->table;
    const StringEntry* end = it + st->used;
    float left = (float) wbox->x;
    int quadCount;

    //attr = gui_drawRect(attr, &wbox->x, 3.0f);

    ds->y = (float) (wbox->y + wbox->h) - ds->tf->descender * ds->psize;

    for (; it != end; ++it) {
        ds->x = left;
        ds->y -= ds->lineSpacing;
        quadCount = txf_genText(ds, attr + 3, attr, ATTR_COUNT,
                                strings + it->start, it->len);
        attr += quadCount * 6 * ATTR_COUNT;
        ds->x = left;
    }
    return attr;
}

//----------------------------------------------------------------------------

struct LayoutBox {
    int16_t x, y, w, h;         // Pixel units.  X,Y is the bottom left.
    int16_t nextPos;            // sconStack position (first pass) or
                                // X/Y position for next widget (second pass).
    uint16_t fixedW;            // Assign fixed width to children.
    uint16_t fixedH;            // Assign fixed height to children.
    uint16_t spacing;           // Pixel gap between widgets.
    uint8_t form;               // LAYOUT_H, LAYOUT_V, LAYOUT_GRID
    uint8_t align;              // AlignBits
    uint8_t columns;            // For LAYOUT_GRID.
    uint8_t _pad;
};

#define loCount align

// Accumulate widget size constraints.
static void layout_size(LayoutBox* lo, SizeCon* sc, const SizeCon* widget)
{
    int adv = lo->loCount ? lo->spacing : 0;

    ++lo->loCount;

    if (lo->form == LAYOUT_H) {
        if (lo->fixedW && widget->prefW < lo->fixedW) {
            adv += lo->fixedW;
            sc->minW  += adv;
            sc->prefW += adv;
        } else {
            sc->minW  += adv + widget->minW;
            sc->prefW += adv + widget->prefW;
        }

        if (sc->minH < widget->minH)
            sc->minH = widget->minH;
        if (sc->prefH < widget->prefH)
            sc->prefH = widget->prefH;
    } else {
        if (lo->fixedH && widget->prefH < lo->fixedH) {
            adv += lo->fixedH;
            sc->minH  += adv;
            sc->prefH += adv;
        } else {
            sc->minH  += adv + widget->minH;
            sc->prefH += adv + widget->prefH;
        }

        if (sc->minW < widget->minW)
            sc->minW = widget->minW;
        if (sc->prefW < widget->prefW)
            sc->prefW = widget->prefW;
    }
}

static void gui_align(GuiRect* wbox, LayoutBox* lo, const SizeCon* cons)
{
#if 0
    printf("KR lo:%p %d,%d,%d,%d fix:%d,%d cons:%d,%d,%d,%d\n",
           lo, lo->x, lo->y, lo->w, lo->h,
           lo->fixedW, lo->fixedH,
           cons->minW, cons->minH, cons->prefW, cons->prefH);
#endif

    wbox->w = cons->prefW;
    if (lo->fixedW && wbox->w < lo->fixedW)
        wbox->w = lo->fixedW;

    wbox->h = cons->prefH;
    if (lo->fixedH && wbox->h < lo->fixedH)
        wbox->h = lo->fixedH;

    if (lo->form == LAYOUT_H) {
        if (lo->align & BACKWARDS) {
            // Layout right to left.
            wbox->x = lo->nextPos - wbox->w;
            lo->nextPos = wbox->x - lo->spacing;
        } else {
            // Layout left to right.
            wbox->x = lo->nextPos;
            lo->nextPos += wbox->w + lo->spacing;
        }

        if (lo->align & ALIGN_B)
            wbox->y = lo->y;
        else if (lo->align & ALIGN_T)
            wbox->y = (lo->y + lo->h) - wbox->h;
        else
            wbox->y = (lo->y + lo->h / 2) - (wbox->h / 2);
    } else {
        if (lo->align & BACKWARDS) {
            // Layout bottom to top.
            wbox->y = lo->nextPos;
            lo->nextPos += wbox->h + lo->spacing;
        } else {
            // Layout top to bottom.
            wbox->y = lo->nextPos - wbox->h;
            lo->nextPos = wbox->y - lo->spacing;
        }

        if (lo->align & ALIGN_L)
            wbox->x = lo->x;
        else if (lo->align & ALIGN_R)
            wbox->x = (lo->x + lo->w) - wbox->w;
        else
            wbox->x = (lo->x + lo->w / 2) - (wbox->w / 2);
    }
}

static void gui_setRootArea(LayoutBox* lo, const GuiArea* root)
{
    if (root) {
        lo->x = root->x;
        lo->y = root->y;
        lo->w = root->x2 - root->x;
        lo->h = root->y2 - root->y;
    } else {
        const ScreenState* ss = screenState();
        lo->x = lo->y = 0;
        lo->w = ss->aspectW;
        lo->h = ss->aspectH;
    }
}

/*
  Create a GPU draw list for widgets using a bytecode language and a
  two-pass layout algorithm.

  The layout program must begin with a LAYOUT_* instruction and ends with a
  paired LAYOUT_END instruction.

  \param primList   The list identifier used with gpu_beginTris()/gpu_endTris().
  \param root       A rectangular pixel area for this layout, or NULL to
                    use screen size.
  \param txfArr     Font list.
  \param bytecode   A program of GuiOpcode instructions.
  \param data       A pointer array of data referenced by bytecode program.

  \return End primitive attribute pointer which caller must pass to
          gpu_endTris().
*/
float* gui_layout(float* attr, const GuiArea* root, TxfDrawState* ds,
                  const uint8_t* bytecode, const void** data)
{
    SizeCon sconStack[MAX_SIZECON];
    LayoutBox loStack[LO_DEPTH];
    LayoutBox* lo;
    SizeCon* scon;
    GuiRect wbox;
    int arg;
    //int itemN;
    int areaWid;
    GuiArea* areaArr = NULL;
    StringTable* itemStr = NULL;
    const uint8_t* pc;
    const void** dp;
    float vgaScale = screenState()->aspectH / 480.0f;

#define RESET_LAYOUT \
    lo = NULL; \
    scon = sconStack; \
    pc = bytecode; \
    dp = data; \
    txf_begin(ds, 0, ds->fontTable[0]->fontSize, 0.0f, 0.0f)


    // First pass to gather widget size information.
    RESET_LAYOUT;

    for(;;) {
        switch (*pc++) {
        default:
        case GUI_END:
            goto layout_done;

        // Position Pen
        case PEN_PER:           // percent x, percent y
            pc += 2;
            break;

        // Layout
        case LAYOUT_V:
        case LAYOUT_H:
            if (lo == NULL) {
                lo = loStack;
                gui_setRootArea(lo, root);
            } else {
                assert(lo != loStack+LO_DEPTH-1);
                // Bootstrap size so *_PER instructions work.
                lo[1].w = loStack[0].w;
                lo[1].h = loStack[0].h;
                ++lo;
            }
            lo->form = pc[-1];
            lo->nextPos = scon - sconStack;
            lo->loCount = 0;
            lo->fixedW = lo->fixedH = lo->spacing = 0;

            scon->minW = scon->minH = 0;
            scon->prefW = scon->prefH = 0;
            ++scon;
            break;

        case LAYOUT_GRID:    // columns
            pc++;
            break;

        case LAYOUT_END:
            if (lo == loStack)
                goto layout_done;
            --lo;
            break;

        case MARGIN_PER:     // percent
        case MARGIN_V_PER:   // percent
            arg = *pc++;
            if (lo == loStack) {
                arg = arg * lo->h / 100;
                lo->h -= arg + arg;
            }
            if (pc[-2] == MARGIN_V_PER)
                break;
            // Fall through...

        case MARGIN_H_PER:   // percent
            arg = *pc++;
            if (lo == loStack) {
                arg = arg * lo->w / 100;
                lo->w -= arg + arg;
            }
            break;

        case SPACING_PER:    // percent
            arg = *pc++;
            lo->spacing = arg *
                ((lo->form == LAYOUT_H) ? loStack[0].w : loStack[0].h) / 100;
            break;

        case SPACING_EM:     // font-em-tenth
            arg = *pc++;
            lo->spacing = arg * ds->psize / 10;
            break;

        case FIX_WIDTH_PER:  // percent
            arg = *pc++;
            lo->fixedW = arg * loStack[0].w / 100;
            break;

        case FIX_HEIGHT_PER: // percent
            arg = *pc++;
            lo->fixedH = arg * loStack[0].h / 100;
            break;

        case FIX_WIDTH_EM:   // font-em-tenth
            arg = *pc++;
            lo->fixedW = arg * ds->psize / 10;
            break;

        case FIX_HEIGHT_EM:  // font-em-tenth
            arg = *pc++;
            lo->fixedH = arg * ds->psize / 10;
            break;

        case FROM_BOTTOM:
        case FROM_RIGHT:
        case ALIGN_LEFT:
        case ALIGN_RIGHT:
        case ALIGN_TOP:
        case ALIGN_BOTTOM:
        case ALIGN_H_CENTER:
        case ALIGN_V_CENTER:
        case ALIGN_CENTER:
            break;

        case GAP_PER:        // percent
            arg = *pc++;
        {
            SizeCon* sc = sconStack + lo->nextPos;
            if (lo->form == LAYOUT_H) {
                arg = arg * loStack[0].w / 100;
                sc->minW  += arg;
                sc->prefW += arg;
            } else {
                arg = arg * loStack[0].h / 100;
                sc->minH  += arg;
                sc->prefH += arg;
            }
        }
            break;

        // Drawing
        case FONT_N:         // font-index
            arg = *pc++;
            ds->tf = ds->fontTable[arg];
            break;

        case FONT_SIZE:      // point-size
            txf_setFontSize(ds, (float) *pc++);
            break;

        case FONT_VSIZE:     // scaled-vga-height
            txf_setFontSize(ds, vgaScale * (float) *pc++);
            break;

        case BG_COLOR_CI:   // color-index
            pc++;
            break;

        case ITEMS_DT_ST:
            dp++;
            break;

        case ITEMS_TEXT:
            break;

        // Widgets
        case ARRAY_DT_AREA: // initial-wid
            dp++;
            pc++;
            break;

        case BUTTON_DT_S:
            button_size(scon, ds, (const uint8_t*) *dp++);
layout_inc:
            layout_size(lo, sconStack + lo->nextPos, scon);
            scon++;
            break;

        case LABEL_DT_S:
            label_size(scon, ds, (const uint8_t*) *dp++);
            goto layout_inc;

        case LIST_DT_ST:
            list_size(scon, ds, (StringTable*) *dp++);
            goto layout_inc;

        case STORE_DT_AREA:
            dp++;
            break;

        case STORE_AREA:
            break;
        }
    }
layout_done:


    // Second pass to create widget draw list.
    RESET_LAYOUT;

    //attr = gpu_beginTris(xu4.gpu, primList);

    for(;;) {
        switch (*pc++) {
        default:
        case GUI_END:
            goto done;

        // Position Pen
        case PEN_PER:           // percent x, percent y
            arg = *pc++;
            ds->x = arg * lo->w / 100;
            arg = *pc++;
            ds->y = arg * lo->h / 100;
            break;

        // Layout
        case LAYOUT_V:
        case LAYOUT_H:
            if (lo == NULL) {
                lo = loStack;
                gui_setRootArea(lo, root);
            } else {
                gui_align((GuiRect*) &lo[1].x, lo, scon);
                ++lo;
            }
            ++scon;

            lo->form = pc[-1];
            lo->nextPos = (lo->form == LAYOUT_H) ? lo->x : lo->y + lo->h;
            lo->align = 0;      // Place forwards & centered.
            lo->fixedW = lo->fixedH = lo->spacing = 0;

            ds->y = (float) lo->h;
            break;

        case LAYOUT_GRID:    // columns
            // TODO: Implement this.
            lo->form = pc[-1];
            lo->columns = *pc++;
            break;

        case LAYOUT_END:
            if (lo == loStack)
                goto done;
            --lo;
            break;

        case MARGIN_PER:     // percent
        case MARGIN_V_PER:   // percent
            arg = *pc++;
            arg = arg * lo->h / 100;
            lo->y += arg;
            lo->h -= arg + arg;
            if (lo->form == LAYOUT_V) {
                if (lo->align & BACKWARDS)
                    lo->nextPos = lo->y;
                else
                    lo->nextPos = lo->y + lo->h;
            }
            if (pc[-2] == MARGIN_V_PER)
                break;
            // Fall through...

        case MARGIN_H_PER:   // percent
            arg = *pc++;
            arg = arg * lo->w / 100;
            lo->x += arg;
            lo->w -= arg + arg;
            if (lo->form == LAYOUT_H) {
                lo->nextPos = lo->x;
                if (lo->align & BACKWARDS)
                    lo->nextPos += lo->w;
            }
            break;

        /*
        case MARGIN_EM:      // font-em-tenth
        case MARGIN_V_EM:    // font-em-tenth
        case MARGIN_H_EM:    // font-em-tenth
            arg = *pc++;
            break;
        */

        case SPACING_PER:    // percent
            arg = *pc++;
            lo->spacing = arg *
                ((lo->form == LAYOUT_H) ? loStack[0].w : loStack[0].h) / 100;
            break;

        case SPACING_EM:     // font-em-tenth
            arg = *pc++;
            lo->spacing = arg * ds->psize / 10;
            break;

        case FIX_WIDTH_PER:  // percent
            arg = *pc++;
            lo->fixedW = arg * loStack[0].w / 100;
            break;

        case FIX_HEIGHT_PER: // percent
            arg = *pc++;
            lo->fixedH = arg * loStack[0].h / 100;
            break;

        case FIX_WIDTH_EM:   // font-em-tenth
            arg = *pc++;
            lo->fixedW = arg * ds->psize / 10;
            break;

        case FIX_HEIGHT_EM:  // font-em-tenth
            arg = *pc++;
            lo->fixedH = arg * ds->psize / 10;
            break;

        case FROM_BOTTOM:
            lo->align |= BACKWARDS;
            lo->nextPos = lo->y;
            break;

        case FROM_RIGHT:
            lo->align |= BACKWARDS;
            lo->nextPos = lo->x + lo->w;
            break;

        case ALIGN_LEFT:
            lo->align = (lo->align & V_MASK) | ALIGN_L;
            break;

        case ALIGN_RIGHT:
            lo->align = (lo->align & V_MASK) | ALIGN_R;
            break;

        case ALIGN_TOP:
            lo->align = (lo->align & H_MASK) | ALIGN_T;
            break;

        case ALIGN_BOTTOM:
            lo->align = (lo->align & H_MASK) | ALIGN_B;
            break;

        case ALIGN_H_CENTER:
            lo->align &= V_MASK;
            break;

        case ALIGN_V_CENTER:
            lo->align &= H_MASK;
            break;

        case ALIGN_CENTER:
            lo->align = 0;
            break;

        case GAP_PER:        // percent
            arg = *pc++;
            arg = arg *
                ((lo->form == LAYOUT_H) ? loStack[0].w : loStack[0].h) / 100;
            if (lo->align & BACKWARDS)
                lo->nextPos -= arg;
            else
                lo->nextPos += arg;
            break;

        // Drawing
        case FONT_N:         // font-index
            arg = *pc++;
            ds->tf = ds->fontTable[arg];
            break;

        case FONT_SIZE:      // point-size
            txf_setFontSize(ds, (float) *pc++);
            break;

        case FONT_VSIZE:     // scaled-vga-height
            txf_setFontSize(ds, vgaScale * (float) *pc++);
            break;

        case BG_COLOR_CI:   // color-index
            arg = *pc++;
            attr = gui_drawRect(attr, &lo->x, (float) arg);
            break;

        case ITEMS_DT_ST:
            itemStr = (StringTable*) *dp++;
            //itemN = 0;
            break;

        case ITEMS_TEXT:
            gui_align(&wbox, lo, scon);
            attr = widget_list(attr, &wbox, ds, itemStr);
            ++scon;
            break;

        // Widgets
        case ARRAY_DT_AREA: // initial-wid
            areaArr = (GuiArea*) *dp++;
            areaWid = *pc++;
            break;

        case BUTTON_DT_S:
            gui_align(&wbox, lo, scon);
            attr = widget_button(attr, &wbox, scon, ds, (const uint8_t*) *dp++);
            ++scon;
            break;

        case LABEL_DT_S:
            gui_align(&wbox, lo, scon);
            attr = widget_label(attr, &wbox, ds, (const uint8_t*) *dp++);
            ++scon;
            break;

        case LIST_DT_ST:
            gui_align(&wbox, lo, scon);
#if 1
            dp++;
#else
            attr = widget_list(attr, &wbox, ds, (StringTable*) *dp++);
#endif
            ++scon;
            break;

        case STORE_DT_AREA:
            {
            GuiArea* dst = (GuiArea*) *dp++;
            dst->x  = wbox.x;
            dst->y  = wbox.y;
            dst->x2 = wbox.x + wbox.w;
            dst->y2 = wbox.y + wbox.h;
            }
            break;

        case STORE_AREA:
            if (areaArr) {
                areaArr->x  = wbox.x;
                areaArr->y  = wbox.y;
                areaArr->x2 = wbox.x + wbox.w;
                areaArr->y2 = wbox.y + wbox.h;
                areaArr->wid = areaWid++;
                ++areaArr;
            }
            break;
        }
    }

done:
    return attr;
}

//----------------------------------------------------------------------------

#include "btree2.c"

/**
 * Return opaque tree pointer which caller must free().
 */
void* gui_areaTree(const GuiArea* areas, int count)
{
    BTree2Gen gen;
    return btree2_generate(&gen, (const BTree2Box*) areas, count);
}

const GuiArea* gui_pick(const void* tree, const GuiArea* areas,
                        uint16_t x, uint16_t y)
{
    return (const GuiArea*)
        btree2_pick((const BTree2*) tree, (const BTree2Box*) areas, x, y);
}
