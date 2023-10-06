#include <cstring>
#include <algorithm>

#include "config.h"
#include "event.h"
#include "image32.h"
#include "gpu.h"
#include "module.h"
#include "settings.h"
#include "screen.h"
#include "u4file.h"
#include "xu4.h"

#include "gamebrowser.h"

extern "C" {
#include "processDir.c"
}


#define ATTR_COUNT  7

void GameBrowser::renderBrowser(ScreenState* ss, void* data)
{
    GameBrowser* gb = (GameBrowser*) data;

    gpu_drawGui(xu4.gpu, GPU_DLIST_GUI);

    if (gb->modFormat.used) {
        const GuiArea* area = gb->gbox + WI_LIST;
        int box[4];
        float selY = gb->lineHeight * gb->psizeList * (gb->sel + 1.0f);

        box[0] = area->x;
        box[1] = area->y2 - 1 - int(selY);
        box[0] += ss->aspectX;
        box[1] += ss->aspectY;

        box[2] = area->x2 - area->x;
        box[3] = gb->psizeList + 2;
        gpu_setScissor(box);
        gpu_invertColors(xu4.gpu);
        gpu_setScissor(NULL);
    }
}

GameBrowser::GameBrowser()
{
    sel = selMusic = 0;
    psizeList = 20.0f;
    atree = NULL;
}

#define NO_PARENT   255

struct ModuleSortContext {
    const StringTable* st;

    bool operator()(const ModuleInfo& a, const ModuleInfo& b) const
    {
        if (a.modFileI == b.parent)
            return true;

        const char* nameA;
        const char* nameB;
        const char* files = sst_strings(st);
        int fi;

        if (a.parent == b.parent) {
            if (a.category != b.category)
                return a.category < b.category;

            // Compare module names.
            nameA = files + sst_start(st, a.modFileI);
            nameB = files + sst_start(st, b.modFileI);
        } else {
            // Compare names of parent modules.
            fi = (a.parent == NO_PARENT) ? a.modFileI : a.parent;
            nameA = files + sst_start(st, fi);
            fi = (b.parent == NO_PARENT) ? b.modFileI : b.parent;
            nameB = files + sst_start(st, fi);
        }
        /*
        printf("KR name %d (%d %d) %s  %d (%d %d) %s\n",
                a.modFileI, a.parent, a.category, nameA,
                b.modFileI, b.parent, b.category, nameB);
        */
        return strcmp(nameA, nameB) < 0;
    }
};

static int collectModFiles(const char* name, int type, void* user)
{
    if (type == PDIR_FILE || type == PDIR_LINK) {
        int len;
        if (mod_extension(name, &len))
            sst_append((StringTable*) user, name, len);
    }
    return PDIR_CONTINUE;
}

static bool isExtensionOf(const char* name, const char* /*version*/,
                          const StringTable* childModi)
{
    int len;
    const char* rules = sst_stringL(childModi, MI_RULES, &len);
    const char* pver = (const char*) memchr(rules, '/', len);
    return (pver && memcmp(name, rules, pver - rules) == 0);
}

/*
 * Fill modFiles with module names and infoList with sorted information.
 * The modFormat strings match the infoList order and are edited for display
 * in the list widget.
 */
static void readModuleList(StringTable* modFiles, StringTable* modFormat,
                           std::vector<ModuleInfo>& infoList)
{
    char modulePath[256];
    ModuleInfo info;
    const StringTable* rp = &xu4.resourcePaths;
    const char* rpath;
    const char* files;
    uint32_t i, m;
    int len;

    // Collect .mod files from resourcePaths.

    for (i = 0; i < rp->used; ++i) {
        m = modFiles->used;
        rpath = sst_stringL(rp, i, &len);
        processDir(rpath, collectModFiles, modFiles);

        memcpy(modulePath, rpath, len);
        modulePath[len] = '/';
        files = sst_strings(modFiles);
        for (; m < modFiles->used; ++m) {
            strcpy(modulePath + len + 1, files + sst_start(modFiles, m));

            sst_init(&info.modi, 4, 80);
            info.resPathI = i;
            info.modFileI = m;
            info.category = mod_query(modulePath, &info.modi);
            info.parent   = NO_PARENT;

            if (info.category == MOD_UNKNOWN)
                sst_free(&info.modi);
            else
                infoList.push_back(info);
        }
    }

    // Assign parents.

    files = sst_strings(modFiles);
    for (const auto& it : infoList) {
        if (it.category == MOD_BASE) {
            const char* name = files + sst_start(modFiles, it.modFileI);
            //const char* version = sst_stringL(it.modi, MI_VERSION, &len);

            for (auto& child : infoList) {
                if (child.category == MOD_BASE)
                    continue;
                if (isExtensionOf(name, NULL, &child.modi))
                    child.parent = it.modFileI;
            }
        }
    }

    // Build infoList with children sorted in alphabetical order under their
    // parents.

    {
    ModuleSortContext sortCtx;
    sortCtx.st = modFiles;
    sort(infoList.begin(), infoList.end(), sortCtx);
    }

    // Create modFormat strings from infoList.  The .mod suffixes are removed
    // and child names are indented.

    const int indentLen = 4;
    strcpy(modulePath, "    ");

    for (const auto& it : infoList) {
        //printf("KR module %d %d/%d %s\n", it.category, it.modFileI, it.parent,
        //       files + sst_start(modFiles, it.modFileI));

        rpath = files + sst_start(modFiles, it.modFileI);

        // Strip .mod suffix.
        len = sst_len(modFiles, it.modFileI) - 4;

        // Indent child modules.
        if (it.category != MOD_BASE) {
            memcpy(modulePath + indentLen, rpath, len);
            rpath = modulePath;
            len += indentLen;
            if (it.category == MOD_SOUNDTRACK) {
#if 1
                // Blue musical note symbol.
                memcpy(modulePath + len,
                       " \x12\x02\x13\x2cN\x12\x00\x13\x00", 10);
                len += 10;
#else
                memcpy(modulePath + len, " (music)", 8);
                len += 8;
#endif
            }
        }
        sst_append(modFormat, rpath, len);
    }
}

void GameBrowser::layout()
{
    static uint8_t browserGui[] = {
        LAYOUT_V, BG_COLOR_CI, 128,
        MARGIN_V_PER, 10, MARGIN_H_PER, 16, SPACING_PER, 12,
        BG_COLOR_CI, 17,
        ARRAY_DT_AREA, WI_LIST,
        MARGIN_V_PER, 6,
            LAYOUT_H,
                FONT_VSIZE, 38, LABEL_DT_S,
                FONT_N, 1,      LABEL_DT_S,
            LAYOUT_END,
            FONT_N, 0, FONT_VSIZE, 20, LIST_DT_ST, STORE_AREA,
            FROM_BOTTOM,
            FONT_N, 1, FONT_VSIZE, 22,
            LAYOUT_H, SPACING_PER, 10, FIX_WIDTH_EM, 50,
                BUTTON_DT_S, STORE_AREA,
                BUTTON_DT_S, STORE_AREA,
                BUTTON_DT_S, STORE_AREA,
            LAYOUT_END,
        LAYOUT_END
    };
    const void* guiData[7];
    const void** data = guiData;

    // Set psizeList to match list FONT_VSIZE.
    psizeList = 20.0f * screenState()->aspectH / 480.0f;

    *data++ = gbox;
    *data++ = "xu4 | ";
    *data++ = "Game Modules";
    *data++ = &modFormat;
    *data++ = "Play";
    *data++ = "Quit";
    *data   = "Cancel";

    TxfDrawState ds;
    ds.fontTable = screenState()->fontTable;
    float* attr = gui_layout(GPU_DLIST_GUI, NULL, &ds, browserGui, guiData);
    if (attr) {
        if (selMusic) {
            // Draw green checkmark.
            const GuiArea* area = gbox + WI_LIST;

            ds.tf = ds.fontTable[2];
            ds.colorIndex = 33.0f;
            ds.x = area->x;
            ds.y = area->y2 -
                   lineHeight * psizeList * (selMusic + 1.0f) -
                   ds.tf->descender * psizeList;
            txf_setFontSize(&ds, psizeList);

            int quads = txf_genText(&ds, attr + 3, attr, ATTR_COUNT,
                                    (const uint8_t*) "c", 1);
            attr += quads * 6 * ATTR_COUNT;
        }

        gpu_endTris(xu4.gpu, GPU_DLIST_GUI, attr);

        free(atree);
        atree = gui_areaTree(gbox, WI_COUNT);
    }
}

bool GameBrowser::present()
{
    lineHeight = screenState()->fontTable[0]->lineHeight;
    screenSetMouseCursor(MC_DEFAULT);

    sst_init(&modFiles, 8, 128);
    sst_init(&modFormat, 8, 50);
    readModuleList(&modFiles, &modFormat, infoList);

    // Select the current modules.
    {
    int len;
    sel = selMusic = 0;

    for (size_t n = 0; n < infoList.size(); ++n) {
        const char* mod = sst_stringL(&modFiles, infoList[n].modFileI, &len);
        if (infoList[n].category == MOD_SOUNDTRACK) {
            if (mod_namesEqual(xu4.settings->soundtrack, mod))
                selMusic = n;
        } else if (mod_namesEqual(xu4.settings->game, mod)) {
            sel = n;
        }
    }
    }

    layout();
    screenSetLayer(LAYER_TOP_MENU, renderBrowser, this);
    return true;
}

GameBrowser::~GameBrowser()
{
    free(atree);
    atree = NULL;

    screenSetLayer(LAYER_TOP_MENU, NULL, NULL);
    sst_free(&modFiles);
    sst_free(&modFormat);

    for (auto& it : infoList)
        sst_free(&it.modi);
    infoList.clear();
}

bool GameBrowser::keyPressed(Stage* st, int key)
{
    switch (key) {
        case U4_ENTER:
        {
            int len;
            const char* game;
            const char* music = "";

            game = sst_stringL(&modFiles, infoList[sel].modFileI, &len);
            if (infoList[sel].category == MOD_SOUNDTRACK) {
                music = game;
                game = sst_stringL(&modFiles, infoList[sel].parent, &len);
            } else if (selMusic) {
                int par = infoList[selMusic].parent;
                if (par == infoList[sel].modFileI ||
                    par == infoList[sel].parent) {
                    music = sst_stringL(&modFiles,
                                        infoList[selMusic].modFileI, &len);
                }
            }
            //printf( "KR Game '%s' '%s'\n", game, music);

            if (mod_namesEqual(xu4.settings->game, game) &&
                mod_namesEqual(xu4.settings->soundtrack, music)) {
                stage_done();
            } else {
                xu4.settings->setGame(game);
                xu4.settings->setSoundtrack(music);
                xu4.settings->write();
                xu4.gameReset = 1;
                stage_exit();
            }
        }
            return true;

        case U4_ESC:
            stage_done();
            return true;

        case U4_SPACE:
            if (infoList[sel].category == MOD_SOUNDTRACK) {
                selMusic = (selMusic == sel) ? 0 : sel;
                layout();
            }
            return true;

        case U4_UP:
            if (sel > 0)
                --sel;
            return true;

        case U4_DOWN:
            if (sel < modFormat.used - 1)
                ++sel;
            return true;
    }
    return false;
}

void GameBrowser::selectModule(const GuiArea* area, int y)
{
    float row = (float) (area->y2 - y) / (lineHeight * psizeList);
    int n = (int) row;
    if (n >= 0 && n < (int) modFormat.used) {
        if (infoList[n].category == MOD_SOUNDTRACK) {
            // Toggle selected soundrack.
            if (selMusic == n) {
                selMusic = 0;
            } else {
                selMusic = n;
                /*
                do {
                    --n;
                } while (n && n != sel && infoList[n].category != MOD_BASE);
                sel = n;
                */
            }
            layout();
        } else {
            sel = n;
        }
    }
}

void* browser_enter(Stage* st, void*)
{
    GameBrowser* gb = new GameBrowser;
    gb->present();
    return gb;
}

void browser_leave(Stage* st)
{
    delete (GameBrowser*) st->data;
}

int browser_dispatch(Stage* st, const InputEvent* ev)
{
    GameBrowser* gb = (GameBrowser*) st->data;
    switch (ev->type) {
        case IE_KEY_PRESS:
            return gb->keyPressed(st, ev->n);

        case IE_MOUSE_PRESS:
            if (ev->n == CMOUSE_LEFT) {
                const ScreenState* ss = screenState();
                int x = ev->x - ss->aspectX;
                int y = (ss->displayH - ev->y) - ss->aspectY;

                const GuiArea* hit = gui_pick(gb->atree, gb->gbox, x, y);
                if (hit) {
                    switch (hit->wid) {
                        case GameBrowser::WI_LIST:
                            gb->selectModule((const GuiArea*) hit, y);
                            break;
                        case GameBrowser::WI_OK:
                            gb->keyPressed(st, U4_ENTER);
                            break;
                        case GameBrowser::WI_CANCEL:
                            gb->keyPressed(st, U4_ESC);
                            break;
                        case GameBrowser::WI_QUIT:
                            stage_exit();
                            break;
                    }
                }
            }
            break;

        case IE_MOUSE_WHEEL:
            if (ev->y < 0)
                gb->keyPressed(st, U4_DOWN);
            else if (ev->y > 0)
                gb->keyPressed(st, U4_UP);
            break;
    }
    return 1;
}
