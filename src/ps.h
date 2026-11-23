/* Challenger Deep: a ship-fighting survival game for the Shanling Q2 under Q2 Pod.
 *
 * The game is its own code. Its art and sound are read at run time from the player's own copy of
 * the 2008 click-wheel iPod game (the .ipg file put in the card folder): nothing from it is in
 * this source or the card folder as shipped.
 *
 * Shared declarations for every file in src/. */
#ifndef PS_H
#define PS_H
#include <stdint.h>

#define VIEW_W 375 /* the UI's landscape view: the portrait panel turned */
#define VIEW_H 320
#define CORNER 80 /* the glass's rounded corners, px */

/* Q2 Pod's AWTK key codes */
#define K_RETURN 170
#define K_PLAY 171
#define K_CENTRE 218
#define K_PREV 222
#define K_NEXT 223

#define PI 3.14159265f
#define TAU 6.28318531f

/* ---------------------------------------------------------------------------------------------
 * q2.c: the platform */
uint64_t now_us(void);
void sleep_us(unsigned us);
void present(const uint32_t *frame);      /* a VIEW_W x VIEW_H XRGB frame onto the panel */
int quit_asked(void);
int next_event(int *kind, int *a, int *b, int *c); /* 'k' (a 1 down/0 up, b code), 't' (a 'd'/'m'/'u', b x, c y), 'w' (a travel) */
void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void message(const char *title, const char *body); /* a full-screen note, with the platform's own font */
const char *app_dir(void);

/* ---------------------------------------------------------------------------------------------
 * sound.c: 8-bit unsigned sounds mixed onto Q2 Pod's output */
typedef struct {
    const uint8_t *data; /* mono, 8-bit unsigned */
    unsigned len, rate;
} Sound;
void snd_start(const char *device, const char *volume);
void snd_shutdown(void);
int snd_play(const Sound *s, int vol, int pan, int loop); /* vol 0-256, pan -256..256; a channel or -1 */
void snd_set(int ch, int vol, int pan);
void snd_stop(int ch);
void snd_stop_all(void);
int snd_on(void);

/* ---------------------------------------------------------------------------------------------
 * zip.c: the .ipg (a zip) */
typedef struct Zip Zip;
Zip *zip_open(const char *path);
void zip_close(Zip *z);
uint8_t *zip_read(Zip *z, const char *name, unsigned *size); /* malloc'd, 0 if missing or bad */
int zip_has(Zip *z, const char *name);
int inflate_raw(const uint8_t *in, unsigned inlen, uint8_t *out, unsigned outlen); /* 0 ok */

/* ---------------------------------------------------------------------------------------------
 * assets.c */
typedef struct {
    int w, h;
    const uint8_t *px;  /* w * h palette indices, or for a font w * h grey levels */
    uint32_t pal[256];  /* ARGB, alpha in the top byte */
    uint8_t opaque[256]; /* 0 clear, 1 partly, 2 opaque */
} Tex;

typedef struct {
    const Tex *t;
    short x, y, w, h; /* the frame in the atlas */
    short ax, ay;     /* where it is drawn from: the centre of what is painted in it */
} Spr;

typedef struct {
    int w, h;
    uint32_t *px; /* ARGB */
} Img;

typedef struct Bank Bank;
Bank *bank_load(Zip *z, const char *member);
void bank_free(Bank *b);
const Tex *bank_tex(const Bank *b, const char *name); /* by entry name without extension */
int bank_count(const Bank *b);
const char *bank_name(const Bank *b, int i);
const Tex *bank_tex_at(const Bank *b, int i);

typedef struct SoundBank SoundBank;
SoundBank *sbank_load(Zip *z, const char *member);
void sbank_free(SoundBank *b);
const Sound *sbank_get(const SoundBank *b, const char *cue); /* by cue name, e.g. ShootCannon1 */

Spr spr_cell(const Tex *t, int cols, int rows, int i); /* cell i of a uniform grid, anchored on its paint */
Spr spr_rect(const Tex *t, int x, int y, int w, int h);
Img img_from_tex(const Tex *t);
Img img_cover(const Tex *t); /* scaled to cover the view, centred (4:3 -> 375 x 320) */
Img img_lcd5(Zip *z, const char *member); /* the iPod's launch pictures: a 16-byte header, then RGB565 */
void img_free(Img *i);

typedef struct Font_ {
    Tex t;          /* grey levels as px; pal unused */
    short x[128], w[128], y[128], dy[128];
    int h, space, gap, base; /* base: rows from a glyph's top to under a capital */
    const struct Font_ *fallback; /* for glyphs this sheet lacks (the large sheet's digits) */
    int fallback_scale;
} Font;
int font_load(Font *f, Zip *z, const char *member, const char *chars, int lower_drop);

typedef struct {
    int w, h;        /* tiles */
    uint16_t *tile;  /* w * h, 0 none, else atlas cell + 1 */
    const Tex *atlas;
    int cols;        /* the atlas's cells a row */
    uint8_t *land;   /* (w * 4) x (h * 4) cells of 8 px: 1 land */
    int lw, lh;
} Map;
int map_load(Map *m, Zip *z, const char *flat, const Tex *atlas);
void map_free(Map *m);
int map_land(const Map *m, float x, float y); /* world px */

/* ---------------------------------------------------------------------------------------------
 * gfx.c: drawing into the frame */
extern uint32_t *fb; /* VIEW_W x VIEW_H */
extern int clip_x0, clip_y0, clip_x1, clip_y1;
void clip_reset(void);
void fill(uint32_t c);
void rect(int x, int y, int w, int h, uint32_t c, int alpha); /* alpha 0-256 */
void rect_frame(int x, int y, int w, int h, uint32_t c, int alpha);
void spr(const Spr *s, int x, int y, int alpha);                    /* anchored at x, y */
void spr_tint(const Spr *s, int x, int y, int alpha, uint32_t tint); /* multiplied by tint */
void spr_scaled(const Spr *s, int x, int y, int scale, int alpha);  /* scale 256 = 1:1 */
void tex_tiled(const Tex *t, int ox, int oy);                      /* the whole view, wrapping */
void img(const Img *i, int x, int y, int alpha);
int text_w(const Font *f, const char *s, int scale);
void text(const Font *f, const char *s, int x, int y, uint32_t c, int scale, int alpha);
void text_c(const Font *f, const char *s, int cx, int y, uint32_t c, int scale, int alpha);
void text_shadow(const Font *f, const char *s, int x, int y, uint32_t c, int scale);
void text_cs(const Font *f, const char *s, int cx, int y, uint32_t c, int scale); /* centred, shadowed */
void text_smooth(const Font *f, const char *s, int cx, int y, uint32_t c, int scale256, int alpha); /* any size, centred */
void disc(int cx, int cy, int r, uint32_t c, int alpha);
void line(int x0, int y0, int x1, int y1, uint32_t c, int alpha);
void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c, int alpha);
void darken(int alpha);

/* ---------------------------------------------------------------------------------------------
 * maths */
float fsin(float a);
float fcos(float a);
float fatan2(float y, float x);
float wrap_pi(float a); /* into -PI..PI */
static inline float fsqrt(float x) { return __builtin_sqrtf(x); }
static inline float fabs_(float x) { return x < 0 ? -x : x; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
unsigned rnd(void);
float rndf(void); /* 0..1 */
void rnd_seed(unsigned s);

/* ---------------------------------------------------------------------------------------------
 * game.c and menu.c */
typedef struct {
    int wheel;      /* travel since the last step */
    int centre, next, prev, play, back; /* pressed since the last step */
    int centre_held;
    int tap, tap_x, tap_y; /* a touch that began and ended without moving far */
    int touch, touch_x, touch_y;
} Input;

int game_main(void);
#endif
