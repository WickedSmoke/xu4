#include <stddef.h>
//#include <stdio.h>
#include <math.h>
#include "txf_draw.h"

/**
  Returns the TxfGlyph for a glyph or zero if it is not available.
  Automatically substitutes uppercase letters with lowercase if
  uppercase not available (and vice versa).
*/
const TxfGlyph* txf_glyph(const TxfHeader* tf, int c)
{
#if 1
    const TxfGlyph* glyph = (TxfGlyph*) (tf + 1);
    c -= glyph->code;
    if (c >= 0 && c < tf->glyphCount)
        return glyph + c;
#else
    uint16_t n;
    uint16_t* table = TABLE(tf);
    int min_glyph = tf->min_glyph;
    int max_glyph = min_glyph + tf->table_size;

    if ((c >= min_glyph) && (c < max_glyph))
    {
        n = table[ c - min_glyph ];
        if( n < 0xffff )
            return GLYPHS(tf) + n;

        if ((c >= 'a') && (c <= 'z'))
        {
            c -= 'a' - 'A';     // toupper
            if ((c >= min_glyph) && (c < max_glyph))
                return GLYPHS(tf) + table[ c - min_glyph ];
        }
        else if ((c >= 'A') && (c <= 'Z'))
        {
            c += 'a' - 'A';     // tolower
            if ((c >= min_glyph) && (c < max_glyph))
                return GLYPHS(tf) + table[ c - min_glyph ];
        }
    }
#endif
    return NULL;
}


float txf_kerning(const TxfHeader* tf, const TxfGlyph* left,
                                       const TxfGlyph* right )
{
#if 1
    (void) tf;
    (void) left;
    (void) right;
#else
    const int16_t* table;
    if( left->kern_index > -1 ) {
        table = KERN(tf) + left->kern_index;
        while (*table) {
            /*
            // If table sorted...
            if (right->c < *table)
                break;
            */
            if (right->c == *table)
                return table[1];
            table += 2;
        }
    }
#endif
    return 0.0f;
}

static const uint8_t* drawTextReturn(TxfDrawState* ds, const uint8_t* it,
                                     const uint8_t* end)
{
    if (*it == '\n')
    {
        ds->x = ds->marginL;
        ds->y -= ds->lineSpacing;
        ds->prev = 0;
    }
    else if (*it == '\t')
    {
        const TxfGlyph* tgi = txf_glyph(ds->tf, ' ');
        if (tgi) {
            float tabWidth = tgi->advance * 4 * ds->psize;
            ds->x += tabWidth - fmodf(ds->x - ds->marginL, tabWidth);
        }
        ds->prev = 0;
    }
    else if (*it == TC_Font)
    {
        if (++it == end)
            return end;
        ds->tf = ds->fontTable[ *it ];
    }
    else if (*it == TC_Color)
    {
        if (++it == end)
            return end;
        ds->colorIndex = (float) *it;
    }
    return it + 1;
}

void txf_begin(TxfDrawState* ds, int fontN, float pointSize, float x, float y)
{
    const TxfHeader* tf = ds->fontTable[fontN];

    ds->tf      = tf;
    ds->lowChar = drawTextReturn;
    ds->prev    = NULL;
    ds->prScale = tf->pixelRange / tf->fontSize;
    ds->x       = x;
    ds->y       = y;
    ds->psize   = pointSize;
    ds->lineSpacing = tf->lineHeight * pointSize;
    ds->marginL = x;
    ds->marginR = 90000.0f;
    ds->colorIndex = 0.0f;
    ds->emitTris = 1;
}

void txf_setFontSize(TxfDrawState* ds, float pointSize)
{
    ds->psize = pointSize;
    ds->lineSpacing = ds->tf->lineHeight * pointSize;
}

/*
  Returns number of glyphs drawn.
*/
int txf_genText(TxfDrawState* ds, float* uvs, float* vertex, int stride,
                const uint8_t* it, unsigned int len)
{
    const uint8_t* end = it + len;
    float gx, gy;
    float gr, gt;
    float min_s, max_s;
    float min_t, max_t;
    float xkern;
    float scale = ds->psize;
    float pixelRange = scale * ds->prScale;
    int drawn = 0;
    const TxfGlyph* tgi;


    while( it != end )
    {
        /*
        // Automatic wrap.
        if (ds->x > ds->marginR) {
            ds->x = ds->marginL;
            ds->y -= ds->lineSpacing;
            ds->prev = 0;
        }
        */

        if (*it > ' ')
        {
            tgi = txf_glyph(ds->tf, *it++);
            if (tgi)
            {
                xkern = ds->prev ? txf_kerning(ds->tf, ds->prev, tgi) : 0.0f;

                gx = ds->x + /*tgi->xoffset +*/ xkern;
                ds->x += (tgi->advance + xkern) * scale;
                gy = ds->y /*+ tgi->yoffset*/;

                gr = gx + tgi->emRect[2] * scale;
                gt = gy + tgi->emRect[3] * scale;
                gx = gx + tgi->emRect[0] * scale;
                gy = gy + tgi->emRect[1] * scale;

                min_s = tgi->tcRect[0];
                max_s = tgi->tcRect[2];
#if 0   // IMAGE_BOTTOM_AT_0
                min_t = tgi->tcRect[1];
                max_t = tgi->tcRect[3];
#else
                min_t = 1.0f - tgi->tcRect[1];
                max_t = 1.0f - tgi->tcRect[3];
#endif

                //printf("KR (%f %f %f %f) (%f %f %f %f)\n",
                //       gx, gy, gr, gt, min_s, min_t, max_s, max_t);

#define GLYPH_ATTR(s,t,x,y) \
    uvs[0] = s; \
    uvs[1] = t; \
    uvs[2] = pixelRange; \
    uvs[3] = ds->colorIndex; \
    uvs += stride; \
    vertex[0] = x; \
    vertex[1] = y; \
    vertex[2] = 0.0f; \
    vertex += stride

                GLYPH_ATTR( min_s, min_t, gx, gy );
                GLYPH_ATTR( max_s, min_t, gr, gy );
                GLYPH_ATTR( max_s, max_t, gr, gt );

                if (ds->emitTris) {
                    GLYPH_ATTR( max_s, max_t, gr, gt );
                    GLYPH_ATTR( min_s, max_t, gx, gt );
                    GLYPH_ATTR( min_s, min_t, gx, gy );
                } else {
                    GLYPH_ATTR( min_s, max_t, gx, gt );
                }

                ++drawn;
            }
            ds->prev = tgi;
        }
        else if (*it == ' ')
        {
            tgi = txf_glyph(ds->tf, ' ');
            if( tgi ) {
#if 0
                xkern = prev ? txf_kerning(ds->tf, prev, tgi) : 0;
                x += (float) (prev->advance + xkern);
#else
                ds->x += tgi->advance * scale;
#endif
            }
            ds->prev = tgi;
            ++it;
        }
        else
        {
            it = ds->lowChar(ds, it, end);
            if( ! it )
                break;
        }
    }

    return drawn;
}

/*
  Draw a single glyph centered on a position using two triangles.
*/
void txf_genGlyphCT(const TxfGlyph* tgi, float x, float y,
                    float psize, float prScale, float colorIndex,
                    float* uvs, float* vertex, int stride)
{
    float gx, gy;       // left, bottom
    float gr, gt;       // right, top
    float min_s, max_s;
    float min_t, max_t;
    float half;
    float pixelRange = psize * prScale;

    half = (tgi->emRect[2] - tgi->emRect[0]) * 0.5f * psize;
    gx = x - half;
    gr = x + half;

    half = (tgi->emRect[3] - tgi->emRect[1]) * 0.5f * psize;
    gy = y - half;
    gt = y + half;

    min_s = tgi->tcRect[0];
    max_s = tgi->tcRect[2];
#if 0   // IMAGE_BOTTOM_AT_0
    min_t = tgi->tcRect[1];
    max_t = tgi->tcRect[3];
#else
    min_t = 1.0f - tgi->tcRect[1];
    max_t = 1.0f - tgi->tcRect[3];
#endif

#define GLYPH_ATTR_C(s,t,x,y) \
    uvs[0] = s; \
    uvs[1] = t; \
    uvs[2] = pixelRange; \
    uvs[3] = colorIndex; \
    uvs += stride; \
    vertex[0] = x; \
    vertex[1] = y; \
    vertex[2] = 0.0f; \
    vertex += stride

    GLYPH_ATTR_C( min_s, min_t, gx, gy );
    GLYPH_ATTR_C( max_s, min_t, gr, gy );
    GLYPH_ATTR_C( max_s, max_t, gr, gt );

    GLYPH_ATTR_C( max_s, max_t, gr, gt );
    GLYPH_ATTR_C( min_s, max_t, gx, gt );
    GLYPH_ATTR_C( min_s, min_t, gx, gy );
}

static float txf_width2(const TxfHeader* tf, const uint8_t* it,
                        const uint8_t* end, const uint8_t** sol)
{
    const TxfGlyph* prev = NULL;
    const TxfGlyph* tgi;
    float width = 0.0;
    int ch;

    while( it != end ) {
        ch = *it++;
        if (ch == TC_Font || ch == TC_Color) {
            if (++it == end)
                break;
            continue;
        }
        if (ch == '\n')
            break;
        tgi = txf_glyph(tf, ch);
        if (ch == ' ') {
            if (tgi)
                width += tgi->advance;
            prev = 0;
        } else if (tgi) {
            width += tgi->advance;
            if (prev)
                width += txf_kerning(tf, prev, tgi);
            prev = tgi;
        }
    }

    *sol = it;
    return width;
}


float txf_emWidth(const TxfHeader* tf, const uint8_t* it, unsigned int len)
{
    const uint8_t* sol;
    return txf_width2(tf, it, it + len, &sol);
}


/*
   Returns em width & height of text bounding rectangle.
*/
void txf_emSize(const TxfHeader* tf, const uint8_t* it, unsigned int len,
                float* size)
{
    const uint8_t* end = it + len;
    float n;
    float w = 0.0f;
    float h = 0.0f;

    while (it != end) {
        h += tf->lineHeight;
        n = txf_width2(tf, it, end, &it);
        if (w < n)
            w = n;
    }

    size[0] = w;
    size[1] = h;
}
