/* Drawing into the frame: paletted sprites with their alpha, tiled water, scaled images, the
 * game's fonts, and a few shapes for the HUD. Everything clips to clip_x0..clip_x1. */
#include <stdint.h>
#include <string.h>

#include "ps.h"

static uint32_t frame_px[VIEW_W * VIEW_H];
uint32_t *fb = frame_px;
int clip_x0, clip_y0, clip_x1 = VIEW_W, clip_y1 = VIEW_H;

void clip_reset(void) { clip_x0 = 0, clip_y0 = 0, clip_x1 = VIEW_W, clip_y1 = VIEW_H; }

static inline uint32_t blend(uint32_t dst, uint32_t src, unsigned a) {
    /* a 0-256 */
    uint32_t rb = ((src & 0xff00ffu) * a + (dst & 0xff00ffu) * (256 - a)) >> 8 & 0xff00ffu;
    uint32_t g = ((src & 0x00ff00u) * a + (dst & 0x00ff00u) * (256 - a)) >> 8 & 0x00ff00u;
    return rb | g;
}

void fill(uint32_t c) {
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) fb[i] = c;
}

void rect(int x, int y, int w, int h, uint32_t c, int alpha) {
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    if (alpha <= 0) return;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t *o = fb + yy * VIEW_W;
        for (int xx = x0; xx < x1; ++xx) o[xx] = alpha >= 256 ? c : blend(o[xx], c, (unsigned)alpha);
    }
}

void rect_frame(int x, int y, int w, int h, uint32_t c, int alpha) {
    rect(x, y, w, 1, c, alpha);
    rect(x, y + h - 1, w, 1, c, alpha);
    rect(x, y + 1, 1, h - 2, c, alpha);
    rect(x + w - 1, y + 1, 1, h - 2, c, alpha);
}

void darken(int alpha) { rect(0, 0, VIEW_W, VIEW_H, 0, alpha); }

/* a sprite at 1:1, its anchor at x, y; alpha 0-256 over the sprite's own */
static void spr_draw(const Spr *s, int x, int y, int alpha, uint32_t tint) {
    if (!s || !s->t || alpha <= 0) return;
    const Tex *t = s->t;
    int dx = x - s->ax, dy = y - s->ay;
    int sx0 = 0, sy0 = 0, sx1 = s->w, sy1 = s->h;
    if (dx + sx0 < clip_x0) sx0 = clip_x0 - dx;
    if (dy + sy0 < clip_y0) sy0 = clip_y0 - dy;
    if (dx + sx1 > clip_x1) sx1 = clip_x1 - dx;
    if (dy + sy1 > clip_y1) sy1 = clip_y1 - dy;
    if (sx0 >= sx1 || sy0 >= sy1) return;
    int tr = tint >> 16 & 255, tg = tint >> 8 & 255, tb = tint & 255, tinted = (tint & 0xffffff) != 0xffffff;
    for (int sy = sy0; sy < sy1; ++sy) {
        const uint8_t *in = t->px + (s->y + sy) * t->w + s->x;
        uint32_t *o = fb + (dy + sy) * VIEW_W + dx;
        for (int sx = sx0; sx < sx1; ++sx) {
            uint8_t i = in[sx];
            uint8_t op = t->opaque[i];
            if (!op) continue;
            uint32_t c = t->pal[i];
            if (tinted)
                c = (uint32_t)((c >> 16 & 255) * tr / 255) << 16 | (uint32_t)((c >> 8 & 255) * tg / 255) << 8 | (c & 255) * tb / 255;
            if (op == 2 && alpha >= 256) o[sx] = c;
            else o[sx] = blend(o[sx], c, (((c >> 24) + 1) * (unsigned)alpha) >> 8);
        }
    }
}

void spr(const Spr *s, int x, int y, int alpha) { spr_draw(s, x, y, alpha, 0xffffff); }
void spr_tint(const Spr *s, int x, int y, int alpha, uint32_t tint) { spr_draw(s, x, y, alpha, tint); }

void spr_scaled(const Spr *s, int x, int y, int scale, int alpha) {
    if (!s || !s->t || alpha <= 0 || scale <= 0) return;
    if (scale == 256) {
        spr(s, x, y, alpha);
        return;
    }
    const Tex *t = s->t;
    int w = s->w * scale >> 8, h = s->h * scale >> 8;
    int dx = x - (s->ax * scale >> 8), dy = y - (s->ay * scale >> 8);
    for (int yy = 0; yy < h; ++yy) {
        int oy = dy + yy;
        if (oy < clip_y0 || oy >= clip_y1) continue;
        const uint8_t *in = t->px + (s->y + (yy << 8) / scale) * t->w + s->x;
        uint32_t *o = fb + oy * VIEW_W;
        for (int xx = 0; xx < w; ++xx) {
            int ox = dx + xx;
            if (ox < clip_x0 || ox >= clip_x1) continue;
            uint8_t i = in[(xx << 8) / scale];
            uint8_t op = t->opaque[i];
            if (!op) continue;
            uint32_t c = t->pal[i];
            if (op == 2 && alpha >= 256) o[ox] = c;
            else o[ox] = blend(o[ox], c, (((c >> 24) + 1) * (unsigned)alpha) >> 8);
        }
    }
}

/* the whole clip area, the texture repeating; texture pixel (ox, oy) at the view's corner */
void tex_tiled(const Tex *t, int ox, int oy) {
    if (!t) return;
    ox %= t->w, oy %= t->h;
    if (ox < 0) ox += t->w;
    if (oy < 0) oy += t->h;
    for (int y = clip_y0; y < clip_y1; ++y) {
        const uint8_t *row = t->px + ((y + oy) % t->h) * t->w;
        uint32_t *o = fb + y * VIEW_W;
        int sx = (clip_x0 + ox) % t->w;
        for (int x = clip_x0; x < clip_x1; ++x) {
            o[x] = t->pal[row[sx]];
            if (++sx == t->w) sx = 0;
        }
    }
}

void img(const Img *im, int x, int y, int alpha) {
    if (!im || !im->px) return;
    for (int yy = 0; yy < im->h; ++yy) {
        int oy = y + yy;
        if (oy < clip_y0 || oy >= clip_y1) continue;
        const uint32_t *in = im->px + yy * im->w;
        uint32_t *o = fb + oy * VIEW_W;
        for (int xx = 0; xx < im->w; ++xx) {
            int ox = x + xx;
            if (ox < clip_x0 || ox >= clip_x1) continue;
            uint32_t c = in[xx];
            unsigned a = ((c >> 24) + 1) * (unsigned)alpha >> 8;
            o[ox] = a >= 256 ? c : blend(o[ox], c, a);
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Text. The sheets have no full stop, comma or hyphen: those are drawn as small marks. */

static int adv(const Font *f, unsigned char ch, int scale) {
    if (ch == ' ') return f->space * scale;
    if (ch < 128 && !f->w[ch] && !(ch >= 'a' && ch <= 'z' && f->w[ch - 32]) && f->fallback &&
        (f->fallback->w[ch] || ch == '.' || ch == '-' || ch == ','))
        return adv(f->fallback, ch, scale * f->fallback_scale) + (f->gap - f->fallback->gap * f->fallback_scale) * scale;
    if (ch == '.' || ch == ',') return (2 + f->gap + 1) * scale;
    if (ch == '-') return (f->space + f->gap) * scale;
    if (ch >= 128 || !f->w[ch]) {
        if (ch >= 'a' && ch <= 'z' && f->w[ch - 32]) return (f->w[ch - 32] + f->gap) * scale;
        return f->space * scale;
    }
    return (f->w[ch] + f->gap) * scale;
}

int text_w(const Font *f, const char *s, int scale) {
    int w = 0;
    for (; *s; ++s) w += adv(f, (unsigned char)*s, scale);
    return w ? w - f->gap * scale : 0;
}

static void glyph(const Font *f, unsigned char ch, int x, int y, uint32_t c, int scale, int alpha) {
    int dy = f->dy[ch], gw = f->w[ch], gh = f->h;
    if (gh > f->t.h - f->y[ch]) gh = f->t.h - f->y[ch];
    for (int yy = 0; yy < gh * scale; ++yy) {
        int oy = y + dy * scale + yy;
        if (oy < clip_y0 || oy >= clip_y1) continue;
        const uint8_t *in = f->t.px + (f->y[ch] + yy / scale) * f->t.w + f->x[ch];
        uint32_t *o = fb + oy * VIEW_W;
        for (int xx = 0; xx < gw * scale; ++xx) {
            int ox = x + xx;
            if (ox < clip_x0 || ox >= clip_x1) continue;
            unsigned v = in[xx / scale];
            if (v < 16) continue;
            o[ox] = blend(o[ox], c, (v + 1) * (unsigned)alpha >> 8);
        }
    }
}

void text(const Font *f, const char *s, int x, int y, uint32_t c, int scale, int alpha) {
    if (!f->t.px) return;
    for (; *s; ++s) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 128 && ch != ' ' && !f->w[ch] && !(ch >= 'a' && ch <= 'z' && f->w[ch - 32]) && f->fallback &&
            (f->fallback->w[ch] || ch == '.' || ch == '-' || ch == ',')) {
            const Font *b = f->fallback;
            int bs = scale * f->fallback_scale;
            char one[2] = { (char)ch, 0 };
            text(b, one, x, y + f->base * scale - b->base * bs, c, bs, alpha);
            x += adv(f, ch, scale);
            continue;
        }
        if (ch == '.' || ch == ',') {
            rect(x + scale, y + (f->base - 2) * scale, 2 * scale, 2 * scale, c, alpha);
            if (ch == ',') rect(x + scale, y + f->base * scale, scale, scale, c, alpha);
        } else if (ch == '-') {
            rect(x + scale, y + (f->base * 5 / 9) * scale, (f->space - 1) * scale, scale > 1 ? scale : 1, c, alpha);
        } else if (ch < 128 && f->w[ch]) {
            glyph(f, ch, x, y, c, scale, alpha);
        } else if (ch >= 'a' && ch <= 'z' && f->w[ch - 32]) {
            glyph(f, (unsigned char)(ch - 32), x, y, c, scale, alpha);
        }
        x += adv(f, ch, scale);
    }
}

/* A line at any size (scale256 / 256), smoothed: each glyph's greys sampled bilinear. Only the
 * sheet's own glyphs and spaces. */
void text_smooth(const Font *f, const char *s, int cx, int y, uint32_t c, int scale256, int alpha) {
    if (!f->t.px) return;
    int w = 0;
    for (const char *p = s; *p; ++p) w += adv(f, (unsigned char)*p, 1) * scale256;
    int x = cx * 256 - w / 2; /* 1/256 px */
    for (; *s; ++s) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z' && !f->w[ch]) ch = (unsigned char)(ch - 32);
        if (ch < 128 && f->w[ch]) {
            int gw = f->w[ch], gh = f->h < f->t.h - f->y[ch] ? f->h : f->t.h - f->y[ch];
            int ox = x >> 8, ow = gw * scale256 / 256 + 2, oh = gh * scale256 / 256 + 2;
            for (int yy = 0; yy < oh; ++yy) {
                int py = y + yy;
                if (py < clip_y0 || py >= clip_y1) continue;
                int sy = yy * 256 * 256 / scale256 - 128, y0 = sy >> 8, fy = sy & 255;
                for (int xx = 0; xx < ow; ++xx) {
                    int px = ox + xx;
                    if (px < clip_x0 || px >= clip_x1) continue;
                    int sx = (xx * 256 + (ox * 256 - x)) * 256 / scale256 - 128, x0 = sx >> 8, fx = sx & 255;
                    int v[4];
                    for (int k = 0; k < 4; ++k) {
                        int gx = x0 + (k & 1), gy = y0 + (k >> 1);
                        v[k] = gx < 0 || gy < 0 || gx >= gw || gy >= gh ? 0 : f->t.px[(f->y[ch] + gy) * f->t.w + f->x[ch] + gx];
                    }
                    int g = ((v[0] * (256 - fx) + v[1] * fx) * (256 - fy) + (v[2] * (256 - fx) + v[3] * fx) * fy) >> 16;
                    if (g < 12) continue;
                    fb[py * VIEW_W + px] = blend(fb[py * VIEW_W + px], c, (unsigned)((g + 1) * alpha) >> 8);
                }
            }
        }
        x += adv(f, ch, 1) * scale256;
    }
}

void text_c(const Font *f, const char *s, int cx, int y, uint32_t c, int scale, int alpha) {
    text(f, s, cx - text_w(f, s, scale) / 2, y, c, scale, alpha);
}

void text_shadow(const Font *f, const char *s, int x, int y, uint32_t c, int scale) {
    text(f, s, x + 1, y + 1, 0x000000, scale, 200);
    text(f, s, x, y, c, scale, 256);
}

void text_cs(const Font *f, const char *s, int cx, int y, uint32_t c, int scale) {
    text_shadow(f, s, cx - text_w(f, s, scale) / 2, y, c, scale);
}

/* ---------------------------------------------------------------------------------------------
 * Shapes */

void disc(int cx, int cy, int r, uint32_t c, int alpha) {
    for (int y = -r; y <= r; ++y) {
        int oy = cy + y;
        if (oy < clip_y0 || oy >= clip_y1) continue;
        int w = (int)fsqrt((float)(r * r - y * y) + 0.5f);
        for (int x = -w; x <= w; ++x) {
            int ox = cx + x;
            if (ox < clip_x0 || ox >= clip_x1) continue;
            fb[oy * VIEW_W + ox] = alpha >= 256 ? c : blend(fb[oy * VIEW_W + ox], c, (unsigned)alpha);
        }
    }
}

void line(int x0, int y0, int x1, int y1, uint32_t c, int alpha) {
    int dx = x1 - x0, dy = y1 - y0, n = dx < 0 ? -dx : dx;
    if ((dy < 0 ? -dy : dy) > n) n = dy < 0 ? -dy : dy;
    for (int i = 0; i <= n; ++i) {
        int x = x0 + (n ? dx * i / n : 0), y = y0 + (n ? dy * i / n : 0);
        if (x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1)
            fb[y * VIEW_W + x] = alpha >= 256 ? c : blend(fb[y * VIEW_W + x], c, (unsigned)alpha);
    }
}

void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c, int alpha) {
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    if (miny < clip_y0) miny = clip_y0;
    if (maxy >= clip_y1) maxy = clip_y1 - 1;
    if (minx < clip_x0) minx = clip_x0;
    if (maxx >= clip_x1) maxx = clip_x1 - 1;
    int area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (!area) return;
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            int w0 = (x1 - x) * (y2 - y) - (x2 - x) * (y1 - y);
            int w1 = (x2 - x) * (y0 - y) - (x0 - x) * (y2 - y);
            int w2 = (x0 - x) * (y1 - y) - (x1 - x) * (y0 - y);
            if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0))
                fb[y * VIEW_W + x] = alpha >= 256 ? c : blend(fb[y * VIEW_W + x], c, (unsigned)alpha);
        }
}

/* ---------------------------------------------------------------------------------------------
 * Maths: a sine table, an arctangent good to a few hundredths of a degree, xorshift */

#define SINN 4096
static float sintab[SINN + 1];
static int sin_ready;

static void sin_init(void) {
    /* a Taylor series about each table point's nearest quarter, plenty for a game */
    for (int i = 0; i <= SINN; ++i) {
        double a = (double)i / SINN * 6.283185307179586;
        /* reduce to -pi..pi */
        if (a > 3.141592653589793) a -= 6.283185307179586;
        double x = a, x2 = x * x, term = x, sum = x;
        for (int k = 1; k < 12; ++k) {
            term *= -x2 / ((2 * k) * (2 * k + 1));
            sum += term;
        }
        sintab[i] = (float)sum;
    }
    sin_ready = 1;
}

float fsin(float a) {
    if (!sin_ready) sin_init();
    float t = a * (SINN / TAU);
    int i = (int)(t < 0 ? t - 1 : t);
    float f = t - i;
    i &= SINN - 1;
    return sintab[i] + (sintab[i + 1] - sintab[i]) * f;
}

float fcos(float a) { return fsin(a + PI / 2); }

float fatan2(float y, float x) {
    float ax = fabs_(x), ay = fabs_(y);
    if (ax == 0 && ay == 0) return 0;
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = PI / 2 - r;
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}

float wrap_pi(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}

static unsigned rs = 2463534242u;
void rnd_seed(unsigned s) { rs = s ? s : 2463534242u; }
unsigned rnd(void) {
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}
float rndf(void) { return (rnd() >> 8) * (1.0f / 16777216.0f); }
