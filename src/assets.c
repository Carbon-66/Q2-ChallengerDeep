/* Reading the iPod game's files out of the .ipg:
 *
 *   texture banks (gametb*.dat, maintbui.dat, plyrship_*.dat, watertexbank.dat): a table of 64
 *       entries of 110 bytes (the file name, 30 bytes; the atlas name, 60; then five little-endian
 *       words: the data's size, its OpenGL ES format, its type, width, height), then each entry's
 *       data in turn from byte 7040. The formats are the paletted ones of OES_compressed_paletted_
 *       texture: 0x8b96 PALETTE8_RGBA8 (256 RGBA colours, then a byte a pixel) and 0x8b97
 *       PALETTE8_R5_G6_B5 (256 16-bit colours, then a byte a pixel), rows from the top.
 *   sound banks (sb*.dat): 64 entries of 113 bytes (the file name, 30; the cue name, 60; the
 *       size; five bytes, the first 1 for a loop; two words; bits, channels, a byte each; the
 *       rate, a word), then each sound's samples from byte 7232, unsigned 8-bit, each padded to
 *       an even length.
 *   maps (Caribbean.flat and the other seas): text lines; after the line naming the tileset
 *       ("CaribbeanTileset") come the width and height in 32 px tiles and the tiles, comma
 *       separated, "N*T" for N of tile T, 0 for open water and T for the atlas's cell T - 1.
 *   fonts (LargeText.LargeText.tga, SmallText.SmallText.tga): uncompressed grey TGAs, the glyphs
 *       in rows with blank columns between them. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps.h"

static unsigned u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* ---------------------------------------------------------------------------------------------
 * Texture banks */

#define TB_ENTRIES 64
#define TB_ENTRY 110

struct Bank {
    uint8_t *data;
    int n;
    char name[TB_ENTRIES][32];
    Tex tex[TB_ENTRIES];
};

static void classify(Tex *t) {
    for (int i = 0; i < 256; ++i) {
        unsigned a = t->pal[i] >> 24;
        t->opaque[i] = a < 8 ? 0 : a > 247 ? 2 : 1;
    }
}

Bank *bank_load(Zip *z, const char *member) {
    unsigned size = 0;
    uint8_t *d = zip_read(z, member, &size);
    if (!d) return 0;
    Bank *b = calloc(1, sizeof *b);
    b->data = d;
    unsigned off = TB_ENTRIES * TB_ENTRY;
    for (int i = 0; i < TB_ENTRIES && (unsigned)(i + 1) * TB_ENTRY <= size; ++i) {
        const uint8_t *e = d + i * TB_ENTRY;
        if (!e[0]) break;
        unsigned len = u32(e + 90), fmt = u32(e + 94), w = u32(e + 102), h = u32(e + 106);
        if (off + len > size || w > 2048 || h > 2048) break;
        int k = b->n++;
        memcpy(b->name[k], e, 30);
        b->name[k][30] = 0;
        char *dot = strrchr(b->name[k], '.');
        if (dot) *dot = 0;
        Tex *t = &b->tex[k];
        const uint8_t *p = d + off;
        if (fmt == 0x8b96 && len >= 1024 + w * h) {
            for (int c = 0; c < 256; ++c)
                t->pal[c] = (uint32_t)p[4 * c + 3] << 24 | (uint32_t)p[4 * c] << 16 | (uint32_t)p[4 * c + 1] << 8 | p[4 * c + 2];
            t->px = p + 1024;
        } else if (fmt == 0x8b97 && len >= 512 + w * h) {
            for (int c = 0; c < 256; ++c) {
                unsigned v = p[2 * c] | p[2 * c + 1] << 8;
                unsigned r = (v >> 11) * 255 / 31, g = (v >> 5 & 63) * 255 / 63, bl = (v & 31) * 255 / 31;
                t->pal[c] = 0xff000000u | r << 16 | g << 8 | bl;
            }
            t->px = p + 512;
        } else {
            logf_("bank %s: %s has format %#x, skipped", member, b->name[k], fmt);
            b->n--;
            off += len;
            continue;
        }
        t->w = (int)w, t->h = (int)h;
        classify(t);
        off += len;
    }
    logf_("bank %s: %d textures", member, b->n);
    return b;
}

void bank_free(Bank *b) {
    if (!b) return;
    free(b->data);
    free(b);
}

const Tex *bank_tex(const Bank *b, const char *name) {
    for (int i = 0; b && i < b->n; ++i)
        if (!strcmp(b->name[i], name)) return &b->tex[i];
    return 0;
}

int bank_count(const Bank *b) { return b ? b->n : 0; }
const char *bank_name(const Bank *b, int i) { return b->name[i]; }
const Tex *bank_tex_at(const Bank *b, int i) { return &b->tex[i]; }

/* ---------------------------------------------------------------------------------------------
 * Sound banks */

#define SB_ENTRIES 64
#define SB_ENTRY 113

struct SoundBank {
    uint8_t *data;
    int n;
    char cue[SB_ENTRIES][61];
    Sound s[SB_ENTRIES];
};

SoundBank *sbank_load(Zip *z, const char *member) {
    unsigned size = 0;
    uint8_t *d = zip_read(z, member, &size);
    if (!d) return 0;
    SoundBank *b = calloc(1, sizeof *b);
    b->data = d;
    unsigned off = SB_ENTRIES * SB_ENTRY;
    for (int i = 0; i < SB_ENTRIES && (unsigned)(i + 1) * SB_ENTRY <= size; ++i) {
        const uint8_t *e = d + i * SB_ENTRY;
        if (!e[0]) break;
        unsigned len = u32(e + 90), bitsz = e[107], ch = e[108], rate = u32(e + 109);
        if (off + len > size) break;
        int k = b->n++;
        memcpy(b->cue[k], e + 30, 60);
        b->cue[k][60] = 0;
        uint8_t *p = d + off;
        if (bitsz == 8 && (ch == 1 || ch == 2) && rate >= 4000 && rate <= 48000) {
            unsigned frames = len / ch;
            if (ch == 2) /* to mono, in place */
                for (unsigned f = 0; f < frames; ++f) p[f] = (uint8_t)((p[2 * f] + p[2 * f + 1]) >> 1);
            b->s[k].data = p, b->s[k].len = frames, b->s[k].rate = rate;
        }
        off += len + (len & 1);
    }
    logf_("sounds %s: %d", member, b->n);
    return b;
}

void sbank_free(SoundBank *b) {
    if (!b) return;
    free(b->data);
    free(b);
}

const Sound *sbank_get(const SoundBank *b, const char *cue) {
    for (int i = 0; b && i < b->n; ++i)
        if (!strcmp(b->cue[i], cue) && b->s[i].data) return &b->s[i];
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * Sprites and images */

Spr spr_rect(const Tex *t, int x, int y, int w, int h) {
    Spr s = { t, (short)x, (short)y, (short)w, (short)h, (short)(w / 2), (short)(h / 2) };
    if (!t) return s;
    /* anchored on the middle of what is painted in it */
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            if (t->pal[t->px[(y + yy) * t->w + x + xx]] >> 24 > 48) {
                if (xx < x0) x0 = xx;
                if (xx > x1) x1 = xx;
                if (yy < y0) y0 = yy;
                if (yy > y1) y1 = yy;
            }
    if (x1 >= 0) s.ax = (short)((x0 + x1 + 1) / 2), s.ay = (short)((y0 + y1 + 1) / 2);
    return s;
}

Spr spr_cell(const Tex *t, int cols, int rows, int i) {
    if (!t) return (Spr){ 0 };
    int cw = t->w / cols, ch = t->h / rows;
    return spr_rect(t, i % cols * cw, i / cols * ch, cw, ch);
}

Img img_from_tex(const Tex *t) {
    Img im = { 0 };
    if (!t) return im;
    im.w = t->w, im.h = t->h;
    im.px = malloc((size_t)im.w * im.h * 4);
    for (int i = 0; i < im.w * im.h; ++i) im.px[i] = t->pal[t->px[i]];
    return im;
}

static uint32_t premul(uint32_t c) {
    unsigned a = c >> 24;
    return (uint32_t)((c >> 16 & 255) * a / 255) << 16 | (uint32_t)((c >> 8 & 255) * a / 255) << 8 | (c & 255) * a / 255;
}

/* bilinear, scaled to cover VIEW_W x VIEW_H and centred */
Img img_cover(const Tex *t) {
    Img im = { 0 };
    if (!t) return im;
    im.w = VIEW_W, im.h = VIEW_H;
    im.px = malloc(VIEW_W * VIEW_H * 4);
    float s = (float)VIEW_W / t->w > (float)VIEW_H / t->h ? (float)VIEW_W / t->w : (float)VIEW_H / t->h;
    float ox = (t->w * s - VIEW_W) / 2, oy = (t->h * s - VIEW_H) / 2;
    for (int y = 0; y < VIEW_H; ++y)
        for (int x = 0; x < VIEW_W; ++x) {
            float sx = (x + ox + 0.5f) / s - 0.5f, sy = (y + oy + 0.5f) / s - 0.5f;
            int x0 = (int)sx, y0 = (int)sy;
            if (sx < 0) x0 = 0, sx = 0;
            if (sy < 0) y0 = 0, sy = 0;
            int x1 = x0 + 1 < t->w ? x0 + 1 : x0, y1 = y0 + 1 < t->h ? y0 + 1 : y0;
            int fx = (int)((sx - x0) * 256), fy = (int)((sy - y0) * 256);
            uint32_t c00 = t->pal[t->px[y0 * t->w + x0]], c10 = t->pal[t->px[y0 * t->w + x1]];
            uint32_t c01 = t->pal[t->px[y1 * t->w + x0]], c11 = t->pal[t->px[y1 * t->w + x1]];
            /* weighted by alpha, so what is clear comes out black */
            c00 = premul(c00), c10 = premul(c10), c01 = premul(c01), c11 = premul(c11);
            uint32_t out = 0xff000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                int a = (int)(c00 >> sh & 255), b = (int)(c10 >> sh & 255), c = (int)(c01 >> sh & 255), d = (int)(c11 >> sh & 255);
                int top = a * (256 - fx) + b * fx, bot = c * (256 - fx) + d * fx;
                out |= (uint32_t)((top * (256 - fy) + bot * fy) >> 16 & 255) << sh;
            }
            im.px[y * VIEW_W + x] = out;
        }
    return im;
}

/* width, height and bytes a row as little-endian words, "565L", 4 bytes more, then the rows */
Img img_lcd5(Zip *z, const char *member) {
    Img im = { 0 };
    unsigned size = 0;
    uint8_t *d = zip_read(z, member, &size);
    if (!d || size < 16 || memcmp(d + 12, "565L", 4)) return free(d), im;
    unsigned w = u32(d), h = u32(d + 4), stride = u32(d + 8);
    if (!w || !h || w > 1024 || h > 1024 || stride < 2 * w || 16 + stride * h > size) return free(d), im;
    im.w = (int)w, im.h = (int)h;
    im.px = malloc((size_t)w * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            unsigned v = d[16 + y * stride + 2 * x] | d[17 + y * stride + 2 * x] << 8;
            im.px[y * w + x] = 0xff000000u | ((v >> 11) * 255 / 31) << 16 | ((v >> 5 & 63) * 255 / 63) << 8 | (v & 31) * 255 / 31;
        }
    free(d);
    return im;
}

void img_free(Img *i) {
    free(i->px);
    i->px = 0;
}

/* ---------------------------------------------------------------------------------------------
 * Fonts. chars names the glyphs in the sheet's order: '\1' joins a glyph to the one before it
 * (the two strokes of "), '\x7f' skips one. Glyphs keep their row's height, so each sits as the
 * sheet has it; lower case (the sheet's small capitals) drops lower_drop px to share the line. */

int font_load(Font *f, Zip *z, const char *member, const char *chars, int lower_drop) {
    memset(f, 0, sizeof *f);
    unsigned size = 0;
    uint8_t *d = zip_read(z, member, &size);
    if (!d || size < 18) return free(d), 0;
    int idlen = d[0], cmap = d[1], type = d[2], w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, bpp = d[16];
    int top = d[17] & 0x20;
    unsigned at = 18 + (unsigned)idlen + (cmap ? (unsigned)(d[5] | d[6] << 8) * ((d[7] + 7) / 8) : 0);
    if (type != 3 || bpp != 8 || at + (unsigned)(w * h) > size) {
        logf_("font %s: type %d, %d bpp: not a grey TGA", member, type, bpp);
        free(d);
        return 0;
    }
    uint8_t *px = malloc((size_t)w * h);
    for (int y = 0; y < h; ++y) memcpy(px + (top ? y : h - 1 - y) * w, d + at + y * w, (size_t)w);
    free(d);
    f->t.w = w, f->t.h = h, f->t.px = px;
    /* rows of ink, then columns of ink in each */
    int bands[16][2], nb = 0, in = 0;
    for (int y = 0; y <= h && nb < 16; ++y) {
        int ink = 0;
        for (int x = 0; y < h && x < w && !ink; ++x) ink = px[y * w + x] > 40;
        if (ink && !in) bands[nb][0] = y, in = 1;
        if (!ink && in) bands[nb++][1] = y, in = 0;
    }
    const char *c = chars;
    int last = -1;
    for (int b = 0; b < nb && *c; ++b) {
        int y0 = bands[b][0], y1 = bands[b][1];
        if (y1 - y0 > f->h) f->h = y1 - y0;
        int x0 = -1;
        for (int x = 0; x <= w && *c; ++x) {
            int ink = 0;
            for (int y = y0; x < w && y < y1 && !ink; ++y) ink = px[y * w + x] > 40;
            if (ink && x0 < 0) x0 = x;
            if (!ink && x0 >= 0) {
                unsigned char ch = (unsigned char)*c++;
                if (ch == 1 && last >= 0) f->w[last] = (short)(x - f->x[last]);
                else if (ch < 128 && ch != 0x7f && ch != 1) {
                    f->x[ch] = (short)x0, f->w[ch] = (short)(x - x0), f->y[ch] = (short)y0;
                    f->dy[ch] = (short)(ch >= 'a' && ch <= 'z' ? lower_drop : 0);
                    last = ch;
                }
                x0 = -1;
            }
        }
    }
    f->space = f->w['N'] ? f->w['N'] * 2 / 3 : 4;
    f->base = f->h;
    if (f->w['A'])
        for (int y = f->h - 1; y >= 0; --y) {
            int ink = 0;
            for (int x = 0; x < f->w['A'] && !ink; ++x) ink = px[(f->y['A'] + y) * w + f->x['A'] + x] > 40;
            if (ink) {
                f->base = y + 1;
                break;
            }
        }
    f->gap = 1;
    logf_("font %s: %d x %d, %d rows, line %d", member, w, h, nb, f->h);
    return 1;
}

/* ---------------------------------------------------------------------------------------------
 * Maps */

static int is_land_px(uint32_t c) {
    unsigned a = c >> 24, r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    return a > 128 && (r + g) / 2 > b + 8;
}

int map_load(Map *m, Zip *z, const char *flat, const Tex *atlas) {
    memset(m, 0, sizeof *m);
    unsigned size = 0;
    char *d = (char *)zip_read(z, flat, &size);
    if (!d || !atlas) return free(d), 0;
    char *text = realloc(d, size + 1);
    text[size] = 0;
    /* the line ending "Tileset" */
    char *p = text, *line = 0;
    while (*p) {
        char *e = p;
        while (*e && *e != '\n') ++e;
        int n = (int)(e - p);
        while (n && (p[n - 1] == '\r' || p[n - 1] == ' ')) --n;
        if (n > 7 && !memcmp(p + n - 7, "Tileset", 7)) {
            line = *e ? e + 1 : e;
            break;
        }
        p = *e ? e + 1 : e;
    }
    if (!line) {
        free(text);
        return 0;
    }
    m->w = atoi(line);
    while (*line && *line != '\n') ++line;
    if (*line) ++line;
    m->h = atoi(line);
    while (*line && *line != '\n') ++line;
    if (*line) ++line;
    if (m->w <= 0 || m->h <= 0 || m->w > 512 || m->h > 512) {
        free(text);
        return 0;
    }
    m->tile = calloc((size_t)m->w * m->h, 2);
    int k = 0, total = m->w * m->h;
    p = line;
    while (*p && *p != '\n' && *p != '\r' && k < total) {
        long a = strtol(p, &p, 10), v = a, count = 1;
        if (*p == '*') {
            count = a;
            v = strtol(p + 1, &p, 10);
        }
        for (long i = 0; i < count && k < total; ++i) m->tile[k++] = (uint16_t)v;
        if (*p == ',') ++p;
        else break;
    }
    free(text);
    m->atlas = atlas;
    m->cols = atlas->w / 32;
    int cells = m->cols * (atlas->h / 32);
    /* each atlas cell's 4 x 4 land flags, then the map's */
    uint16_t *flags = calloc((size_t)cells + 1, 2);
    for (int c = 0; c < cells; ++c)
        for (int sy = 0; sy < 4; ++sy)
            for (int sx = 0; sx < 4; ++sx) {
                int n = 0;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        n += is_land_px(atlas->pal[atlas->px[(c / m->cols * 32 + sy * 8 + y) * atlas->w + c % m->cols * 32 + sx * 8 + x]]);
                if (n >= 24) flags[c + 1] |= (uint16_t)(1 << (sy * 4 + sx));
            }
    m->lw = m->w * 4, m->lh = m->h * 4;
    m->land = calloc((size_t)m->lw * m->lh, 1);
    int land = 0;
    for (int i = 0; i < total; ++i) {
        int t = m->tile[i];
        if (t <= 0 || t > cells) continue;
        for (int b = 0; b < 16; ++b)
            if (flags[t] >> b & 1) m->land[(i / m->w * 4 + b / 4) * m->lw + i % m->w * 4 + b % 4] = 1, ++land;
    }
    free(flags);
    logf_("map %s: %d x %d tiles, %d of %d cells land", flat, m->w, m->h, land, m->lw * m->lh);
    return k == total;
}

void map_free(Map *m) {
    free(m->tile);
    free(m->land);
    memset(m, 0, sizeof *m);
}

int map_land(const Map *m, float x, float y) {
    if (x < 0 || y < 0) return 1;
    int cx = (int)x >> 3, cy = (int)y >> 3;
    if (cx >= m->lw || cy >= m->lh) return 1;
    return m->land[cy * m->lw + cx];
}
