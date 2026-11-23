/* Challenger Deep: the game. Sail one of nine ships on one of five seas and sink the waves of
 * pirate and navy ships that come for you, for as long as your hull holds.
 *
 *   the wheel     turns the ship (a full turn of the wheel is three quarters of a turn at sea)
 *   Centre        fires both broadsides
 *   Next / Prev   more sail / less sail
 *   a tap         fires the broadside on the side of the ship you tap
 *   Play/Pause    pauses (Return too)
 *
 * World coordinates are pixels of the sea's map (188 x 188 tiles of 32 px); angles are radians
 * anticlockwise from east, and the screen's y runs down, so a heading a moves (cos a, -sin a). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#include "ps.h"

#define STEP (1.0f / 60)
#define MAX_SHIPS 16
#define MAX_BALLS 160
#define MAX_FX 256
#define MAX_PICKUPS 32
#define BALL_SPEED 240.0f
#define BALL_RANGE 230.0f
#define SAVE_FILE "challengerdeep.sav"
#define OLD_DIR "../.pirates" /* 1.0's folder: its .ipg and save are used until moved */

#define GOLD 0xf2d48a
#define PARCH 0x3b2410
#define WHITE 0xf4efe0
#define RED 0xd84030

/* ---------------------------------------------------------------------------------------------
 * What the .ipg gives */

static Zip *zip;
static Bank *ui, *mainui, *fxb, *seab, *shipb;
static SoundBank *sfx, *menusfx;
static Font small, large;
static Map map;
static Img bg_title, bg_ship, bg_sea, bg_over, bg_load;

static const struct {
    const char *name, *flat, *bank, *tiles, *sounds;
} seas[] = {
    { "Caribbean", "Caribbean.flat", "gametbcaribbean.dat", "CaribbeanTilesetAtlas", "sbcaribbean.dat" },
    { "Europe", "Europe.flat", "gametbeurope.dat", "EuropeTilesetAtlas", "sbeurope.dat" },
    { "Africa", "Africa.flat", "gametbafrica.dat", "AfricaTilesetAtlas", "sbafrica.dat" },
    { "Hawaii", "Hawaii.flat", "gametbhawaii.dat", "HawaiiTilesetAtlas", "sbhawaii.dat" },
    { "Maldives", "Maldives.flat", "gametbmaldives.dat", "MaldivesTilesetAtlas", "sbmaldives.dat" },
};
#define NSEAS (int)(sizeof seas / sizeof *seas)
static uint8_t sea_preview[NSEAS][188 * 188]; /* each sea's tiles: 1 where something is */

/* the player's ships, weakest first; the stats are this game's own */
static const struct {
    const char *file;
    float speed, turn, hp, reload, radius;
    int guns;
} hulls[] = {
    { "plyrship_merchant_sloop.dat", 96, 2.0f, 60, 1.25f, 14, 2 },
    { "plyrship_junk.dat", 92, 1.8f, 70, 1.25f, 15, 2 },
    { "plyrship_empress.dat", 88, 1.6f, 90, 1.35f, 17, 3 },
    { "plyrship_interceptor.dat", 112, 1.7f, 80, 1.3f, 17, 3 },
    { "plyrship_merchant_ship.dat", 74, 1.2f, 135, 1.6f, 20, 4 },
    { "plyrship_pirate_warship.dat", 78, 1.25f, 150, 1.7f, 20, 5 },
    { "plyrship_ghost_ship.dat", 96, 1.5f, 125, 1.3f, 19, 4 },
    { "plyrship_black_pearl.dat", 104, 1.45f, 135, 1.35f, 19, 4 },
    { "plyrship_flying_dutchman.dat", 92, 1.35f, 170, 1.55f, 21, 5 },
};
#define NHULLS (int)(sizeof hulls / sizeof *hulls)
static char hull_name[NHULLS][32];

/* the enemies, as each sea's bank has them */
static const struct {
    const char *tex;
    float speed, turn, hp, reload, radius;
    int guns, value, big;
} foes[] = {
    { "EnemyPirateSloop", 72, 1.5f, 40, 2.4f, 14, 2, 40, 0 },
    { "EnemyNavySloop", 76, 1.5f, 48, 2.2f, 14, 2, 50, 0 },
    { "EnemyPirateGalleon", 56, 1.0f, 110, 2.6f, 20, 4, 110, 1 },
    { "EnemyNavyGalleon", 54, 1.0f, 130, 2.5f, 20, 4, 130, 1 },
    { "EnemyGhostShipAtlas", 66, 1.2f, 150, 2.2f, 19, 4, 160, 1 },
};
#define NFOES (int)(sizeof foes / sizeof *foes)

typedef struct {
    Spr frame[24];
    int frames, ok;
    float speed, turn, hp, reload, radius;
    int guns, value, big;
} Kind;

static Kind player_kind, foe_kind[NFOES];

/* effects */
static Spr spr_splash[6], spr_boom[7], spr_fire[5], spr_ball, spr_coin, spr_chest, spr_plank, spr_gem[12], spr_powder;
static Spr spr_plaque, spr_cursor, spr_banner;
static const Tex *water;
static const Sound *s_fire[3], *s_hit[2], *s_splash, *s_sink, *s_coin, *s_ammo, *s_sand, *s_bump, *s_wind,
    *s_gulls, *s_land, *s_knob, *s_burn;

/* ---------------------------------------------------------------------------------------------
 * The game's state */

typedef struct {
    int alive, player;
    Kind *k;
    float x, y, ang, speed;
    float hp, maxhp;
    float reload[2]; /* left (port), right (starboard) */
    float turn;      /* the player's turn still to make */
    int sail;        /* the player's 0-3 */
    float sink;      /* > 0: sinking, seconds */
    float flash, bump, wake;
    float think, want; /* AI */
    int side, value;
    float dmg_mul;
} Ship;

typedef struct {
    int alive, mine;
    float x, y, vx, vy, t, life, dmg;
} Ball;

enum { FX_SPLASH, FX_BOOM, FX_SMOKE, FX_FOAM, FX_SPARK };
typedef struct {
    int kind;
    float x, y, t, dur, vx, vy, size;
} Fx;

enum { P_GOLD, P_CHEST, P_REPAIR, P_GEM, P_POWDER };
typedef struct {
    int alive, kind, gem, value;
    float x, y, t;
} Pickup;

static Ship ships[MAX_SHIPS];
static Ball balls[MAX_BALLS];
static Fx fxs[MAX_FX];
static Pickup pickups[MAX_PICKUPS];
static Ship *me;
static float cam_x, cam_y, shake, clock_s;
static int wave, to_spawn, sunk, gold, score;
static float wave_t, spawn_t, powder_t, banner_t;
static char banner[32], banner2[48];
static int wind_ch = -1;

static struct {
    int best_score, best_wave, hull, sea;
} save;

enum { S_TITLE, S_SHIPS, S_SEAS, S_PLAY, S_PAUSE, S_OVER };
static int state = S_TITLE, menu_sel, pause_sel;
static int over_new_best;

/* ---------------------------------------------------------------------------------------------
 * Save */

static void save_read(void) {
    FILE *f = fopen(SAVE_FILE, "r");
    if (!f && (f = fopen(OLD_DIR "/pirates.sav", "r"))) logf_("Best score taken from 1.0's .pirates folder");
    if (!f) return;
    if (fscanf(f, "%d %d %d %d", &save.best_score, &save.best_wave, &save.hull, &save.sea) != 4) memset(&save, 0, sizeof save);
    fclose(f);
    save.hull = clampi(save.hull, 0, NHULLS - 1);
    save.sea = clampi(save.sea, 0, NSEAS - 1);
}

static void save_write(void) {
    FILE *f = fopen(SAVE_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d\n", save.best_score, save.best_wave, save.hull, save.sea);
    fclose(f);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

/* ---------------------------------------------------------------------------------------------
 * Sound, placed by where it happens */

static void play_at(const Sound *s, float x, float y, int vol) {
    float dx = x - cam_x, dy = y - cam_y, d = fsqrt(dx * dx + dy * dy);
    int v = (int)(vol * (d < 160 ? 1.0f : 160 / d));
    if (v < 12) return;
    snd_play(s, v, clampi((int)(dx * 256 / 260), -200, 200), 0);
}

/* ---------------------------------------------------------------------------------------------
 * Loading */

static void kind_from(Kind *k, const Tex *t, int n) {
    memset(k, 0, sizeof *k);
    if (!t) return;
    int grid = n == 24 ? 5 : 4;
    for (int i = 0; i < n; ++i) k->frame[i] = spr_cell(t, grid, grid, i);
    k->frames = n;
    k->ok = 1;
}

/* "PlayerMerchantSloop" -> "Merchant Sloop" */
static void label_from(char *out, int size, const char *entry) {
    if (!strncmp(entry, "Player", 6)) entry += 6;
    int n = 0;
    for (int i = 0; entry[i] && n < size - 2; ++i) {
        if (i && entry[i] >= 'A' && entry[i] <= 'Z' && entry[i - 1] >= 'a' && entry[i - 1] <= 'z') out[n++] = ' ';
        out[n++] = entry[i];
    }
    out[n] = 0;
}

static int load_hull(int i) {
    bank_free(shipb);
    shipb = bank_load(zip, hulls[i].file);
    const Tex *t = shipb && bank_count(shipb) ? bank_tex_at(shipb, 0) : 0;
    kind_from(&player_kind, t, 24);
    Kind *k = &player_kind;
    k->speed = hulls[i].speed, k->turn = hulls[i].turn, k->hp = hulls[i].hp, k->reload = hulls[i].reload;
    k->radius = hulls[i].radius, k->guns = hulls[i].guns;
    return k->ok;
}

static void sea_previews(void) {
    for (int s = 0; s < NSEAS; ++s) {
        unsigned size = 0;
        char *d = (char *)zip_read(zip, seas[s].flat, &size);
        if (!d) continue;
        /* the map line: the longest line */
        char *best = 0;
        int bestn = 0;
        for (char *p = d; p < d + size;) {
            char *e = p;
            while (e < d + size && *e != '\n') ++e;
            if (e - p > bestn) bestn = (int)(e - p), best = p;
            p = e + 1;
        }
        int k = 0;
        for (char *p = best; p && p < best + bestn && k < 188 * 188;) {
            long a = strtol(p, &p, 10), v = a, count = 1;
            if (*p == '*') count = a, v = strtol(p + 1, &p, 10);
            for (long i = 0; i < count && k < 188 * 188; ++i, ++k)
                if (v) sea_preview[s][k] = 1;
            if (*p == ',') ++p;
            else break;
        }
        free(d);
    }
}

static int load_sea(int s) {
    snd_stop_all();
    wind_ch = -1;
    map_free(&map);
    bank_free(seab);
    sbank_free(sfx);
    seab = bank_load(zip, seas[s].bank);
    sfx = sbank_load(zip, seas[s].sounds);
    if (!seab) return 0;
    if (!map_load(&map, zip, seas[s].flat, bank_tex(seab, seas[s].tiles))) return 0;
    water = bank_tex(seab, "Water_resized");
    for (int i = 0; i < NFOES; ++i) {
        kind_from(&foe_kind[i], bank_tex(seab, foes[i].tex), 16);
        Kind *k = &foe_kind[i];
        k->speed = foes[i].speed, k->turn = foes[i].turn, k->hp = foes[i].hp, k->reload = foes[i].reload;
        k->radius = foes[i].radius, k->guns = foes[i].guns, k->value = foes[i].value, k->big = foes[i].big;
    }
    static const char *fire[3] = { "ShootCannon1", "ShootCannon2", "ShootCannon3" };
    for (int i = 0; i < 3; ++i) s_fire[i] = sbank_get(sfx, fire[i]);
    s_hit[0] = sbank_get(sfx, "CannonBallHitShip1");
    s_hit[1] = sbank_get(sfx, "CannonBallHitShip2");
    s_splash = sbank_get(sfx, "WaterSplash");
    s_sink = sbank_get(sfx, "ShipDestroyed");
    s_coin = sbank_get(sfx, "TreasurePickup");
    s_ammo = sbank_get(sfx, "PickUpAmmo");
    s_sand = sbank_get(sfx, "ShipHitSandbar");
    s_bump = sbank_get(sfx, "ShipHitShip");
    s_wind = sbank_get(sfx, "Wind");
    s_gulls = sbank_get(sfx, "Seagulls");
    s_land = sbank_get(sfx, "CannonballHitLand");
    s_burn = sbank_get(sfx, "BurningShip");
    return 1;
}

/* the .ipg: the first file ending .ipg in the app's folder, else in 1.0's .pirates folder */
static int find_ipg_in(const char *dir, char *out, int size) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    while (d && (e = readdir(d))) {
        int n = (int)strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".ipg")) {
            snprintf(out, (unsigned)size, "%s/%s", dir, e->d_name);
            found = 1;
            break;
        }
    }
    if (d) closedir(d);
    return found;
}

static int find_ipg(char *out, int size) { return find_ipg_in(".", out, size) || find_ipg_in(OLD_DIR, out, size); }

/* The loading picture: the flaming skull from the top of the iPod game's own launch picture (the
 * rest of it is that game's title), scaled up bilinear and faded into black at its edges. */
static void make_loading(void) {
    Img src = img_lcd5(zip, "piratesLaunchm25.raw.lcd5");
    if (!src.px || src.w < 230 || src.h < 76) {
        img_free(&src);
        return;
    }
    enum { SX = 101, SY = 0, SW = 100, SH = 76 }; /* the skull, centred */
    /* the logo and the title's first letter reach into the crop's lower left: faded out softly */
    for (int y = 30; y < SH; ++y)
        for (int x = SX; x < 140; ++x) {
            float keep = clampf((x - 118) / 20.0f, 0, 1), up = clampf((42 - y) / 12.0f, 0, 1);
            if (y > 66) keep *= clampf((x - 126) / 14.0f, 0, 1);
            if (up > keep) keep = up;
            uint32_t c = src.px[y * src.w + x];
            int k = (int)(keep * keep * 256);
            src.px[y * src.w + x] = 0xff000000u | ((c >> 16 & 255) * k >> 8) << 16 | ((c >> 8 & 255) * k >> 8) << 8 | ((c & 255) * k >> 8);
        }
    float s = 2.7f;
    int dw = (int)(SW * s), dh = (int)(SH * s), dx = (VIEW_W - dw) / 2, dy = (VIEW_H - dh) / 2 - 8;
    bg_load.w = VIEW_W, bg_load.h = VIEW_H;
    bg_load.px = malloc(VIEW_W * VIEW_H * 4);
    for (int i = 0; i < VIEW_W * VIEW_H; ++i) bg_load.px[i] = 0xff000000u;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            float fx = x / s - 0.5f, fy = y / s - 0.5f;
            if (fx < 0) fx = 0;
            if (fy < 0) fy = 0;
            int x0 = (int)fx, y0 = (int)fy, x1 = x0 + 1 < SW ? x0 + 1 : x0, y1 = y0 + 1 < SH ? y0 + 1 : y0;
            int ax = (int)((fx - x0) * 256), ay = (int)((fy - y0) * 256);
            uint32_t c00 = src.px[(SY + y0) * src.w + SX + x0], c10 = src.px[(SY + y0) * src.w + SX + x1];
            uint32_t c01 = src.px[(SY + y1) * src.w + SX + x0], c11 = src.px[(SY + y1) * src.w + SX + x1];
            /* fade: the edges of the crop go to black (the top is the picture's own edge, so less) */
            float ex = (float)x / dw, ey = (float)y / dh;
            float f = clampf(ex / 0.24f, 0, 1) * clampf((1 - ex) / 0.24f, 0, 1) * clampf(ey / 0.2f, 0, 1) * clampf((1 - ey) / 0.3f, 0, 1);
            int k = (int)(f * f * 256);
            uint32_t out = 0xff000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                int a = (int)(c00 >> sh & 255), b = (int)(c10 >> sh & 255), c = (int)(c01 >> sh & 255), d = (int)(c11 >> sh & 255);
                int v = ((a * (256 - ax) + b * ax) * (256 - ay) + (c * (256 - ax) + d * ax) * ay) >> 16;
                out |= (uint32_t)(v * k >> 8) << sh;
            }
            bg_load.px[(dy + y) * VIEW_W + dx + x] = out;
        }
    img_free(&src);
}

static void show_loading(const char *caption) {
    fill(0);
    if (bg_load.px) img(&bg_load, 0, 0, 256);
    if (caption && small.t.px) text_cs(&small, caption, VIEW_W / 2, VIEW_H - 64, GOLD, 1);
    present(fb);
}

static const char small_chars[] = "!\"\1%&'()+\x7f\x7f\x7f/0123456789:;?ABCDEFGHIJKLMNOPQRSTUVWXYZ\x7f"
                                  "abcdefghijklmnopqrstuvwxyz";

static int load_common(void) {
    char path[300];
    if (!find_ipg(path, sizeof path)) {
        message("CHALLENGER DEEP", "Copy your Pirates of the Caribbean .ipg (the iPod game file iTunes kept) into "
                                   "the .challengerdeep folder on the card, then start the game again.\n\n"
                                   "Hold Return to leave.");
        return 0;
    }
    logf_("Reading %s", path);
    zip = zip_open(path);
    if (zip) make_loading(), show_loading(0);
    if (getenv("PS_HOLDLOAD")) sleep_us(1500000); /* host tests: time to see the picture */
    ui = bank_load(zip, "gametbui.dat");
    mainui = bank_load(zip, "maintbui.dat");
    fxb = bank_load(zip, "gametbfx.dat");
    menusfx = sbank_load(zip, "sbmenu.dat");
    if (!zip || !ui || !mainui || !fxb || !font_load(&small, zip, "SmallText.SmallText.tga", small_chars, 1) ||
        !font_load(&large, zip, "LargeText.LargeText.tga", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0)) {
        message("CHALLENGER DEEP", "This .ipg isn't the one the game knows: its texture banks or fonts are "
                                   "missing. Check challengerdeep.log in the .challengerdeep folder.\n\nHold Return to leave.");
        return 0;
    }
    large.fallback = &small, large.fallback_scale = 2;
    s_knob = sbank_get(menusfx, "TurnKnob");
    bg_title = img_cover(bank_tex(mainui, "profile_background"));
    bg_ship = img_cover(bank_tex(ui, "ship_background"));
    bg_sea = img_cover(bank_tex(mainui, "map_background"));
    bg_over = img_cover(bank_tex(ui, "Pirates_Loose_screen_final_fla"));

    const Tex *t;
    if ((t = bank_tex(fxb, "SplashAtlas")))
        for (int i = 0; i < 6; ++i) spr_splash[i] = spr_cell(t, 2, 3, i);
    if ((t = bank_tex(fxb, "AsplosionAtlas")))
        for (int i = 0; i < 7; ++i) spr_boom[i] = spr_cell(t, 3, 3, i);
    if ((t = bank_tex(fxb, "ShipFireAtlas"))) {
        for (int i = 0; i < 4; ++i) spr_fire[i] = spr_rect(t, i * 17, 56, 17, 28);
        spr_fire[4] = spr_rect(t, 0, 84, 17, 28);
    }
    if ((t = bank_tex(fxb, "CannonballAtlas"))) spr_ball = spr_rect(t, 0, 0, 7, 7);
    if ((t = bank_tex(fxb, "MiscAtlas"))) {
        spr_coin = spr_cell(t, 3, 3, 6);
        spr_chest = spr_cell(t, 3, 3, 3);
        spr_plank = spr_cell(t, 3, 3, 2);
    }
    if ((t = bank_tex(fxb, "TreasureIcons")))
        for (int i = 0; i < 12; ++i) spr_gem[i] = spr_cell(t, 5, 5, 8 + i);
    if ((t = bank_tex(fxb, "CannonballIcons"))) spr_powder = spr_cell(t, 2, 2, 0);
    if ((t = bank_tex(ui, "menu_plaquard"))) spr_plaque = spr_rect(t, 0, 0, t->w, t->h);
    if ((t = bank_tex(ui, "Pirates_menu_screen_cursor"))) spr_cursor = spr_rect(t, 0, 0, t->w, t->h);
    if ((t = bank_tex(mainui, "dialog_banner"))) spr_banner = spr_rect(t, 0, 0, t->w, t->h);

    for (int i = 0; i < NHULLS; ++i) {
        Bank *b = bank_load(zip, hulls[i].file);
        if (b && bank_count(b)) label_from(hull_name[i], sizeof hull_name[i], bank_name(b, 0));
        else snprintf(hull_name[i], sizeof hull_name[i], "Ship %d", i + 1);
        bank_free(b);
    }
    sea_previews();
    return 1;
}

/* ---------------------------------------------------------------------------------------------
 * Effects and pickups */

static void fx_add(int kind, float x, float y, float dur, float vx, float vy, float size) {
    for (int i = 0; i < MAX_FX; ++i)
        if (fxs[i].dur <= 0) {
            fxs[i] = (Fx){ kind, x, y, 0, dur, vx, vy, size };
            return;
        }
}

static void pickup_add(int kind, float x, float y, int value) {
    for (int i = 0; i < MAX_PICKUPS; ++i)
        if (!pickups[i].alive) {
            pickups[i] = (Pickup){ 1, kind, (int)(rnd() % 12), value, x, y, 0 };
            return;
        }
}

/* ---------------------------------------------------------------------------------------------
 * Ships */

static int free_water(float x, float y, float r) {
    for (int i = 0; i < 12; ++i) {
        float a = i * TAU / 12;
        if (map_land(&map, x + fcos(a) * r, y - fsin(a) * r)) return 0;
    }
    return !map_land(&map, x, y);
}

static Ship *ship_new(Kind *k, float x, float y, float ang) {
    for (int i = 0; i < MAX_SHIPS; ++i)
        if (!ships[i].alive) {
            Ship *s = &ships[i];
            memset(s, 0, sizeof *s);
            s->alive = 1, s->k = k, s->x = x, s->y = y, s->ang = ang;
            s->hp = s->maxhp = k->hp;
            s->dmg_mul = 1;
            s->side = rnd() & 1 ? 1 : -1;
            return s;
        }
    return 0;
}

static int frame_of(const Kind *k, float ang) {
    float t = ang / TAU * k->frames;
    int f = (int)(t + (t < 0 ? -0.5f : 0.5f)) % k->frames;
    return f < 0 ? f + k->frames : f;
}

static void fire(Ship *s, int side) { /* side 0 port (left), 1 starboard */
    if (s->reload[side] > 0 || s->sink > 0) return;
    float perp = s->ang + (side ? -PI / 2 : PI / 2);
    float px = fcos(perp), py = -fsin(perp), hx = fcos(s->ang), hy = -fsin(s->ang);
    int n = s->k->guns;
    float len = s->k->radius * 1.1f;
    for (int g = 0; g < n; ++g) {
        float along = n > 1 ? -len / 2 + len * g / (n - 1) : 0;
        float spread = (rndf() - 0.5f) * 0.08f, cs = fcos(spread), sn = fsin(spread);
        float bx = px * cs - py * sn, by = px * sn + py * cs;
        for (int i = 0; i < MAX_BALLS; ++i)
            if (!balls[i].alive) {
                Ball *b = &balls[i];
                b->alive = 1, b->mine = s->player;
                b->x = s->x + hx * along + px * s->k->radius * 0.6f;
                b->y = s->y + hy * along + py * s->k->radius * 0.6f;
                float sp = BALL_SPEED * (0.95f + rndf() * 0.1f);
                b->vx = bx * sp + hx * s->speed * 0.5f;
                b->vy = by * sp + hy * s->speed * 0.5f;
                b->t = 0, b->life = BALL_RANGE / BALL_SPEED * (0.9f + rndf() * 0.2f);
                b->dmg = (s->player ? 10 : 6 + wave * 0.6f) * s->dmg_mul;
                break;
            }
        fx_add(FX_SMOKE, s->x + hx * along + px * s->k->radius * 0.8f, s->y + hy * along + py * s->k->radius * 0.8f, 0.6f,
               px * 20, py * 20 - 6, 1);
    }
    s->reload[side] = s->k->reload * (s->player && powder_t > 0 ? 0.5f : 1);
    play_at(s_fire[rnd() % 3], s->x, s->y, s->player ? 200 : 150);
}

static void sink(Ship *s) {
    s->sink = 0.001f;
    play_at(s_sink, s->x, s->y, 220);
    for (int i = 0; i < 4; ++i)
        fx_add(FX_BOOM, s->x + (rndf() - 0.5f) * 24, s->y + (rndf() - 0.5f) * 16, 0.6f + 0.2f * i, 0, -4, 1);
    if (s->player) return;
    ++sunk;
    score += s->value;
    /* what floats free */
    pickup_add(s->k->big ? P_CHEST : P_GOLD, s->x, s->y, s->k->value / 2);
    float r = rndf();
    if (r < 0.28f) pickup_add(P_REPAIR, s->x + 18, s->y + 6, 0);
    else if (r < 0.42f) pickup_add(P_POWDER, s->x - 16, s->y + 8, 0);
    else if (r < 0.62f) pickup_add(P_GEM, s->x + 10, s->y - 14, 40);
}

static void damage(Ship *s, float d, float x, float y) {
    if (s->sink > 0) return;
    s->hp -= d;
    s->flash = 0.15f;
    if (s->player) shake = 5;
    play_at(s_hit[rnd() & 1], x, y, 190);
    fx_add(FX_BOOM, x, y, 0.45f, 0, 0, 0.6f);
    if (s->hp <= 0) s->hp = 0, sink(s);
}

/* turns s towards ang at its rate; how far there is still to go */
static float steer_to(Ship *s, float ang, float dt) {
    float d = wrap_pi(ang - s->ang), max = s->k->turn * dt * (0.45f + 0.55f * clampf(s->speed / 40, 0, 1));
    s->ang = wrap_pi(s->ang + clampf(d, -max, max));
    return d;
}

static void move(Ship *s, float dt) {
    float ox = s->x, oy = s->y;
    s->x += fcos(s->ang) * s->speed * dt;
    s->y -= fsin(s->ang) * s->speed * dt;
    float bowx = s->x + fcos(s->ang) * s->k->radius * 0.8f, bowy = s->y - fsin(s->ang) * s->k->radius * 0.8f;
    if (map_land(&map, s->x, s->y) || map_land(&map, bowx, bowy)) {
        s->x = ox, s->y = oy;
        if (s->bump <= 0 && s->speed > 15) {
            play_at(s_sand, s->x, s->y, 200);
            if (s->player) damage(s, 2, bowx, bowy);
            s->bump = 0.6f;
        }
        s->speed *= -0.25f;
    }
}

static void ai(Ship *s, float dt) {
    float dx = me->x - s->x, dy = me->y - s->y, d = fsqrt(dx * dx + dy * dy);
    float to = fatan2(-dy, dx);
    if ((s->think -= dt) <= 0) {
        s->think = 0.25f + rndf() * 0.2f;
        float want = to;
        if (me->sink > 0) want = to + PI; /* sail off */
        else if (d < 250) {
            /* lay a broadside: across the line to the player, on the side that turns least */
            float a = to + PI / 2, b = to - PI / 2;
            if ((rnd() & 15) == 0) s->side = -s->side;
            else s->side = fabs_(wrap_pi(a - s->ang)) < fabs_(wrap_pi(b - s->ang)) ? 1 : -1;
            want = to + s->side * (PI / 2 + (d < 110 ? 0.5f : d > 190 ? -0.35f : 0));
        }
        /* keep off the land: the first clear heading near the one wanted */
        static const float tries[] = { 0, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f, 2.4f, -2.4f, PI };
        for (unsigned i = 0; i < sizeof tries / sizeof *tries; ++i) {
            float a = want + tries[i], cx = fcos(a), cy = -fsin(a);
            if (!map_land(&map, s->x + cx * 40, s->y + cy * 40) && !map_land(&map, s->x + cx * 80, s->y + cy * 80) &&
                !map_land(&map, s->x + cx * 120, s->y + cy * 120)) {
                want = a;
                break;
            }
        }
        s->want = want;
    }
    float left = steer_to(s, s->want, dt);
    float target = s->k->speed * (fabs_(left) > 1.2f ? 0.6f : 1);
    s->speed += clampf(target - s->speed, -40 * dt, 30 * dt);
    /* fire when the player lies off a beam, in range */
    if (me->sink <= 0 && d < BALL_RANGE * 1.05f) {
        float rel = wrap_pi(to - s->ang);
        if (fabs_(wrap_pi(rel - PI / 2)) < 0.3f) fire(s, 0);
        if (fabs_(wrap_pi(rel + PI / 2)) < 0.3f) fire(s, 1);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Waves */

static int foes_alive(void) {
    int n = 0;
    for (int i = 0; i < MAX_SHIPS; ++i) n += ships[i].alive && !ships[i].player && ships[i].sink <= 0;
    return n;
}

static void spawn_foe(void) {
    /* pick a kind: sloops first, the big ships as the waves grow */
    int pick = -1;
    for (int tries = 0; tries < 20 && pick < 0; ++tries) {
        int i = (int)(rnd() % NFOES);
        if (!foe_kind[i].ok) continue;
        float big_chance = wave < 3 ? 0 : clampf(0.2f + 0.06f * wave, 0, 0.6f);
        if (foe_kind[i].big && rndf() > big_chance) continue;
        if (i == 4 && wave < 6) continue;
        pick = i;
    }
    if (pick < 0)
        for (int i = 0; i < NFOES; ++i)
            if (foe_kind[i].ok) pick = i;
    if (pick < 0) return;
    /* somewhere off screen, in open water */
    for (int tries = 0; tries < 60; ++tries) {
        float a = rndf() * TAU, r = 280 + rndf() * 160;
        float x = me->x + fcos(a) * r, y = me->y - fsin(a) * r;
        if (!free_water(x, y, 40)) continue;
        Ship *s = ship_new(&foe_kind[pick], x, y, fatan2(-(me->y - y), me->x - x));
        if (!s) return;
        float m = 1 + 0.1f * (wave - 1);
        s->hp = s->maxhp = s->k->hp * m;
        s->value = (int)(s->k->value * m);
        s->speed = s->k->speed * 0.5f;
        --to_spawn;
        return;
    }
}

static void wave_start(void) {
    ++wave;
    to_spawn = 2 + wave;
    spawn_t = 1.5f;
    snprintf(banner, sizeof banner, "WAVE %d", wave);
    snprintf(banner2, sizeof banner2, "%d ships sighted", to_spawn);
    banner_t = 3;
}

static void game_start(void) {
    memset(ships, 0, sizeof ships);
    memset(balls, 0, sizeof balls);
    memset(fxs, 0, sizeof fxs);
    memset(pickups, 0, sizeof pickups);
    wave = sunk = gold = score = 0;
    powder_t = 0;
    wave_t = 0;
    rnd_seed((unsigned)now_us());
    /* start in open water near the middle */
    float sx = map.w * 16.0f, sy = map.h * 16.0f;
    for (int r = 0; r < 3000; r += 24) {
        int found = 0;
        for (int i = 0; i < 16 && !found; ++i) {
            float a = i * TAU / 16, x = map.w * 16.0f + fcos(a) * r, y = map.h * 16.0f - fsin(a) * r;
            if (free_water(x, y, 160) && free_water(x, y, 90)) sx = x, sy = y, found = 1;
        }
        if (found) break;
    }
    me = ship_new(&player_kind, sx, sy, PI / 2);
    me->player = 1;
    me->sail = 2;
    cam_x = sx, cam_y = sy;
    if (getenv("PS_WAVE")) wave = atoi(getenv("PS_WAVE")) - 1; /* host tests: a later wave */
    wave_start();
    if (s_wind) wind_ch = snd_play(s_wind, 70, 0, 1);
}

/* ---------------------------------------------------------------------------------------------
 * One step of the game, STEP seconds */

static void step_play(const Input *in, float dt) {
    clock_s += dt;
    /* the player */
    if (me->sink <= 0) {
        me->turn = clampf(me->turn + in->wheel * (TAU * 0.75f / 200), -PI, PI);
        if (in->next) me->sail = clampi(me->sail + 1, 0, 3);
        if (in->prev) me->sail = clampi(me->sail - 1, 0, 3);
        if (in->centre) fire(me, 0), fire(me, 1);
        if (in->tap) {
            /* the side of the ship the tap is on */
            float sx = in->tap_x - (me->x - cam_x + VIEW_W / 2), sy = in->tap_y - (me->y - cam_y + VIEW_H / 2);
            float port = -sx * fsin(me->ang) - sy * fcos(me->ang); /* > 0: left of the heading */
            fire(me, port > 0 ? 0 : 1);
        }
        float rate = me->k->turn * dt * (0.45f + 0.55f * clampf(me->speed / 40, 0, 1));
        float t = clampf(me->turn, -rate, rate);
        me->ang = wrap_pi(me->ang + t);
        me->turn -= t;
        if (fabs_(me->turn) < 0.002f) me->turn = 0;
        float target = me->k->speed * (float[]){ 0, 0.4f, 0.72f, 1 }[me->sail] * (fabs_(me->turn) > 0.6f ? 0.85f : 1);
        me->speed += clampf(target - me->speed, -45 * dt, 28 * dt);
    } else {
        me->speed *= 0.98f;
    }
    if (powder_t > 0) powder_t -= dt;

    /* waves */
    if (banner_t > 0) banner_t -= dt;
    if (me->sink <= 0) {
        if (to_spawn > 0 && (spawn_t -= dt) <= 0 && foes_alive() < 5) {
            spawn_foe();
            spawn_t = 2.5f + rndf() * 2;
        }
        if (to_spawn <= 0 && !foes_alive()) {
            if (wave_t <= 0) {
                wave_t = 4;
                snprintf(banner, sizeof banner, "WAVE %d CLEARED", wave);
                snprintf(banner2, sizeof banner2, "Repairs made: hull +25%%");
                banner_t = 3;
                me->hp = me->hp + me->maxhp * 0.25f > me->maxhp ? me->maxhp : me->hp + me->maxhp * 0.25f;
            } else if ((wave_t -= dt) <= 0) {
                wave_start();
            }
        }
    }

    /* ships */
    for (int i = 0; i < MAX_SHIPS; ++i) {
        Ship *s = &ships[i];
        if (!s->alive) continue;
        if (s->flash > 0) s->flash -= dt;
        if (s->bump > 0) s->bump -= dt;
        for (int k = 0; k < 2; ++k)
            if (s->reload[k] > 0) s->reload[k] -= dt;
        if (s->sink > 0) {
            s->sink += dt;
            s->speed *= 0.97f;
            if (s->sink < 1.2f && rndf() < 0.15f)
                fx_add(FX_SMOKE, s->x + (rndf() - 0.5f) * 20, s->y + (rndf() - 0.5f) * 14, 1.2f, 0, -10, 1.4f);
            if (s->sink > 3 && !s->player) s->alive = 0;
        } else if (!s->player) {
            ai(s, dt);
        }
        move(s, dt);
        if (s->speed > 18 && s->sink <= 0 && (s->wake -= dt) <= 0) {
            s->wake = 0.07f;
            float bx = s->x - fcos(s->ang) * s->k->radius * 0.7f, by = s->y + fsin(s->ang) * s->k->radius * 0.7f;
            fx_add(FX_FOAM, bx + (rndf() - 0.5f) * 4, by + (rndf() - 0.5f) * 4 + 4, 1.3f, 0, 0, s->k->radius / 14);
        }
        if (s->hp < s->maxhp * 0.4f && s->sink <= 0 && rndf() < 0.05f)
            fx_add(FX_SMOKE, s->x + (rndf() - 0.5f) * 10, s->y - 8, 1.1f, 0, -14, 0.8f);
    }
    /* ships bumping */
    for (int i = 0; i < MAX_SHIPS; ++i)
        for (int j = i + 1; j < MAX_SHIPS; ++j) {
            Ship *a = &ships[i], *b = &ships[j];
            if (!a->alive || !b->alive || a->sink > 0 || b->sink > 0) continue;
            float dx = b->x - a->x, dy = b->y - a->y, r = (a->k->radius + b->k->radius) * 0.85f, d2 = dx * dx + dy * dy;
            if (d2 >= r * r || d2 < 0.01f) continue;
            float d = fsqrt(d2), push = (r - d) / 2;
            dx /= d, dy /= d;
            a->x -= dx * push, a->y -= dy * push;
            b->x += dx * push, b->y += dy * push;
            if (map_land(&map, a->x, a->y)) a->x += dx * push, a->y += dy * push;
            if (map_land(&map, b->x, b->y)) b->x -= dx * push, b->y -= dy * push;
            if (a->bump <= 0 && b->bump <= 0 && (a->player || b->player)) {
                play_at(s_bump, (a->x + b->x) / 2, (a->y + b->y) / 2, 200);
                damage(a, 3, a->x + dx * 8, a->y + dy * 8);
                damage(b, 3, b->x - dx * 8, b->y - dy * 8);
                a->bump = b->bump = 0.8f;
                a->speed *= 0.5f, b->speed *= 0.5f;
            }
        }

    /* cannonballs */
    for (int i = 0; i < MAX_BALLS; ++i) {
        Ball *b = &balls[i];
        if (!b->alive) continue;
        b->x += b->vx * dt, b->y += b->vy * dt, b->t += dt;
        int hit = 0;
        for (int k = 0; k < MAX_SHIPS && !hit; ++k) {
            Ship *s = &ships[k];
            if (!s->alive || s->sink > 0 || s->player == b->mine) continue;
            float dx = s->x - b->x, dy = s->y - b->y;
            if (dx * dx + dy * dy < s->k->radius * s->k->radius) {
                damage(s, b->dmg, b->x, b->y);
                hit = 1;
            }
        }
        if (hit) {
            b->alive = 0;
        } else if (b->t >= b->life) {
            b->alive = 0;
            if (map_land(&map, b->x, b->y)) {
                fx_add(FX_BOOM, b->x, b->y, 0.4f, 0, 0, 0.5f);
                play_at(s_land, b->x, b->y, 90);
            } else {
                fx_add(FX_SPLASH, b->x, b->y, 0.55f, 0, 0, 1);
                play_at(s_splash, b->x, b->y, 70);
            }
        }
    }

    /* pickups */
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        Pickup *p = &pickups[i];
        if (!p->alive) continue;
        p->t += dt;
        if (p->t > 25) {
            p->alive = 0;
            continue;
        }
        float dx = me->x - p->x, dy = me->y - p->y;
        if (me->sink > 0 || dx * dx + dy * dy > (me->k->radius + 16) * (me->k->radius + 16)) continue;
        p->alive = 0;
        fx_add(FX_SPARK, p->x, p->y, 0.6f, 0, -30, 1);
        switch (p->kind) {
        case P_GOLD:
        case P_CHEST:
        case P_GEM:
            gold += p->value;
            score += p->value;
            play_at(s_coin, p->x, p->y, 200);
            break;
        case P_REPAIR:
            me->hp = me->hp + me->maxhp * 0.3f > me->maxhp ? me->maxhp : me->hp + me->maxhp * 0.3f;
            play_at(s_ammo, p->x, p->y, 200);
            break;
        case P_POWDER:
            powder_t = 12;
            play_at(s_ammo, p->x, p->y, 200);
            break;
        }
    }

    /* effects */
    for (int i = 0; i < MAX_FX; ++i) {
        Fx *f = &fxs[i];
        if (f->dur <= 0) continue;
        f->t += dt;
        f->x += f->vx * dt, f->y += f->vy * dt;
        if (f->t >= f->dur) f->dur = 0;
    }

    /* the camera: ahead of the ship */
    float lx = me->x + fcos(me->ang) * me->speed * 0.45f, ly = me->y - fsin(me->ang) * me->speed * 0.45f;
    cam_x += (lx - cam_x) * 0.06f;
    cam_y += (ly - cam_y) * 0.06f;
    if (shake > 0) shake -= dt * 20;
    if (wind_ch >= 0) snd_set(wind_ch, 50 + me->sail * 12, 0);
    if (s_gulls && rndf() < dt / 25) snd_play(s_gulls, 60, (int)(rndf() * 300) - 150, 0);
}

/* ---------------------------------------------------------------------------------------------
 * Drawing the sea */

static int sx_of(float x) { return (int)(x - cam_x) + VIEW_W / 2; }
static int sy_of(float y) { return (int)(y - cam_y) + VIEW_H / 2; }

static void draw_ship(const Ship *s) {
    int x = sx_of(s->x), y = sy_of(s->y);
    if (x < -80 || x > VIEW_W + 80 || y < -80 || y > VIEW_H + 80) return;
    const Spr *f = &s->k->frame[frame_of(s->k, s->ang)];
    int alpha = 256;
    if (s->sink > 0) {
        alpha = (int)(256 * clampf(1 - (s->sink - 0.8f) / 2.2f, 0, 1));
        y += (int)(s->sink * 5);
    }
    if (s->flash > 0) spr_tint(f, x, y, alpha, 0xff9080);
    else if (s->sink > 0) spr_tint(f, x, y, alpha, 0x908070);
    else spr(f, x, y, alpha);
    /* fire on a ship low in its hull */
    if (spr_fire[0].t && (s->hp < s->maxhp * 0.35f || (s->sink > 0 && s->sink < 2.5f))) {
        int k = (int)(clock_s * 12 + (s - ships) * 3) % 5;
        spr(&spr_fire[k], x - 4, y - 6, alpha * 3 / 4);
        if (s->sink > 0) spr(&spr_fire[(k + 2) % 5], x + 8, y - 2, alpha * 3 / 4);
    }
}

static void draw_sea(void) {
    int ox = (int)cam_x - VIEW_W / 2, oy = (int)cam_y - VIEW_H / 2;
    if (shake > 0) ox += (int)((rndf() - 0.5f) * shake * 2), oy += (int)((rndf() - 0.5f) * shake * 2);
    /* the water, drifting a little against the map so the sea seems to move */
    if (water) tex_tiled(water, ox + (int)(clock_s * 4), oy + (int)(clock_s * 9));
    else fill(0x1f5a84);
    /* the map's tiles */
    if (map.tile) {
        int tx0 = ox >> 5, ty0 = oy >> 5, tx1 = (ox + VIEW_W) >> 5, ty1 = (oy + VIEW_H) >> 5;
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx) {
                if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) continue;
                int t = map.tile[ty * map.w + tx];
                if (!t) continue;
                --t;
                Spr s = { map.atlas, (short)(t % map.cols * 32), (short)(t / map.cols * 32), 32, 32, 0, 0 };
                if (s.y + 32 > map.atlas->h) continue;
                spr(&s, tx * 32 - ox, ty * 32 - oy, 256);
            }
        /* the map's edge: darker water beyond it */
        if (ox < 0) rect(0, 0, -ox, VIEW_H, 0x0a2030, 160);
        if (oy < 0) rect(0, 0, VIEW_W, -oy, 0x0a2030, 160);
        if (ox + VIEW_W > map.w * 32) rect(map.w * 32 - ox, 0, VIEW_W, VIEW_H, 0x0a2030, 160);
        if (oy + VIEW_H > map.h * 32) rect(0, map.h * 32 - oy, VIEW_W, VIEW_H, 0x0a2030, 160);
    }
    /* foam */
    for (int i = 0; i < MAX_FX; ++i) {
        const Fx *f = &fxs[i];
        if (f->dur <= 0 || f->kind != FX_FOAM) continue;
        float k = f->t / f->dur;
        disc(sx_of(f->x), sy_of(f->y), (int)((2 + k * 5) * f->size), 0xe8f4f8, (int)(90 * (1 - k)));
    }
    /* pickups */
    for (int i = 0; i < MAX_PICKUPS; ++i) {
        const Pickup *p = &pickups[i];
        if (!p->alive) continue;
        if (p->t > 20 && ((int)(p->t * 8) & 1)) continue;
        int x = sx_of(p->x), y = sy_of(p->y) + (int)(fsin(p->t * 3 + i) * 2);
        disc(x, y + 5, 9, 0x0a2838, 70);
        switch (p->kind) {
        case P_GOLD: spr_scaled(&spr_coin, x, y, 110, 256); break;
        case P_CHEST: spr_scaled(&spr_chest, x, y, 150, 256); break;
        case P_REPAIR: spr_scaled(&spr_plank, x, y, 130, 256); break;
        case P_GEM: spr(&spr_gem[p->gem], x, y, 256); break;
        case P_POWDER: spr(&spr_powder, x, y, 256); break;
        }
    }
    /* ships, from the back */
    const Ship *order[MAX_SHIPS];
    int n = 0;
    for (int i = 0; i < MAX_SHIPS; ++i)
        if (ships[i].alive) order[n++] = &ships[i];
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && order[j]->y < order[j - 1]->y; --j) {
            const Ship *t = order[j];
            order[j] = order[j - 1], order[j - 1] = t;
        }
    for (int i = 0; i < n; ++i) draw_ship(order[i]);
    /* cannonballs, in an arc over their shadows */
    for (int i = 0; i < MAX_BALLS; ++i) {
        const Ball *b = &balls[i];
        if (!b->alive) continue;
        float k = b->t / b->life, z = fsin(k * PI) * 12;
        int x = sx_of(b->x), y = sy_of(b->y);
        disc(x, y + 2, 2, 0x082030, 90);
        if (spr_ball.t) spr(&spr_ball, x, y - (int)z, 256);
        else disc(x, y - (int)z, 2, 0x202020, 256);
    }
    /* effects */
    for (int i = 0; i < MAX_FX; ++i) {
        const Fx *f = &fxs[i];
        if (f->dur <= 0) continue;
        float k = f->t / f->dur;
        int x = sx_of(f->x), y = sy_of(f->y);
        switch (f->kind) {
        case FX_SPLASH: spr(&spr_splash[clampi((int)(k * 6), 0, 5)], x, y - 6, 256); break;
        case FX_BOOM: spr_scaled(&spr_boom[clampi((int)(k * 6), 0, 5)], x, y - 6, (int)(256 * f->size), 256); break;
        case FX_SMOKE:
            disc(x, y, (int)((3 + k * 7) * f->size), 0x9a9a9a, (int)(130 * (1 - k)));
            disc(x - 1, y - 1, (int)((2 + k * 5) * f->size), 0xc8c8c8, (int)(90 * (1 - k)));
            break;
        case FX_SPARK:
            for (int j = 0; j < 6; ++j) {
                float a = j * TAU / 6 + k * 3;
                disc(x + (int)(fcos(a) * k * 18), y - (int)(fsin(a) * k * 18), 2, 0xffe890, (int)(256 * (1 - k)));
            }
            break;
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * The HUD, kept clear of the glass's rounded corners */

static void bar(int x, int y, int w, int h, float v, uint32_t c) {
    rect(x - 1, y - 1, w + 2, h + 2, 0x1a0e04, 230);
    rect(x, y, w, h, 0x3a2a1a, 200);
    rect(x, y, (int)(w * clampf(v, 0, 1)), h, c, 256);
    rect(x, y, (int)(w * clampf(v, 0, 1)), 1, 0xffffff, 60);
}

static void plaque(int cx, int cy, int w) {
    if (spr_plaque.t) {
        /* the plaque, stretched: its ends, then its middle */
        int sw = spr_plaque.w, h = spr_plaque.h;
        int x0 = cx - w / 2, y0 = cy - h / 2;
        Spr l = spr_plaque, m = spr_plaque, r = spr_plaque;
        l.w = 12, l.ax = 0, l.ay = 0;
        r.x = (short)(spr_plaque.x + sw - 12), r.w = 12, r.ax = 0, r.ay = 0;
        m.x = (short)(spr_plaque.x + 12), m.w = 1, m.ax = 0, m.ay = 0;
        spr(&l, x0, y0, 256);
        for (int x = x0 + 12; x < x0 + w - 12; ++x) {
            m.x = (short)(spr_plaque.x + 12 + (x - x0 - 12) % (sw - 24));
            spr(&m, x, y0, 256);
        }
        spr(&r, x0 + w - 12, y0, 256);
    } else {
        rect(cx - w / 2, cy - 8, w, 16, 0x5a3a1a, 230);
    }
}

static void offscreen_arrows(void) {
    for (int i = 0; i < MAX_SHIPS; ++i) {
        const Ship *s = &ships[i];
        if (!s->alive || s->player || s->sink > 0) continue;
        int x = sx_of(s->x), y = sy_of(s->y);
        if (x > -10 && x < VIEW_W + 10 && y > -10 && y < VIEW_H + 10) continue;
        /* on an ellipse inside the glass */
        float dx = (float)(x - VIEW_W / 2), dy = (float)(y - VIEW_H / 2), a = fatan2(dy, dx);
        float ex = VIEW_W / 2 - 24, ey = VIEW_H / 2 - 44;
        int ax = VIEW_W / 2 + (int)(fcos(a) * ex), ay = VIEW_H / 2 + (int)(fsin(a) * ey);
        float c = fcos(a), sn = fsin(a);
        tri(ax + (int)(c * 9), ay + (int)(sn * 9), ax + (int)(-sn * 6 - c * 4), ay + (int)(c * 6 - sn * 4),
            ax + (int)(sn * 6 - c * 4), ay + (int)(-c * 6 - sn * 4), s->k->big ? 0xff5030 : 0xffb030, 220);
    }
}

static void draw_hud(void) {
    char t[48];
    /* top: the hull and the gold */
    plaque(VIEW_W / 2, 14, 180);
    float hv = me->hp / me->maxhp;
    bar(VIEW_W / 2 - 74, 10, 88, 7, hv, hv > 0.5f ? 0x58c040 : hv > 0.25f ? 0xe0b030 : 0xe04030);
    spr_scaled(&spr_coin, VIEW_W / 2 + 30, 14, 72, 256);
    snprintf(t, sizeof t, "%d", gold);
    text_shadow(&small, t, VIEW_W / 2 + 42, 8, GOLD, 1);
    /* bottom: the wave, the sail and the guns */
    plaque(VIEW_W / 2, VIEW_H - 16, 200);
    snprintf(t, sizeof t, "WAVE %d", wave);
    text_shadow(&small, t, VIEW_W / 2 - 88, VIEW_H - 22, GOLD, 1);
    for (int i = 0; i < 3; ++i) {
        int x = VIEW_W / 2 - 22 + i * 10;
        rect(x, VIEW_H - 21, 7, 9, i < me->sail ? 0xf0e8d0 : 0x3a2a1a, i < me->sail ? 256 : 160);
    }
    for (int side = 0; side < 2; ++side) {
        int x = VIEW_W / 2 + 26 + side * 34;
        float r = me->reload[side] > 0 ? 1 - me->reload[side] / (me->k->reload * (powder_t > 0 ? 0.5f : 1)) : 1;
        bar(x, VIEW_H - 19, 26, 5, r, r >= 1 ? (powder_t > 0 ? 0xff8030 : 0xd8d8d8) : 0x707070);
    }
    text_shadow(&small, "L", VIEW_W / 2 + 18, VIEW_H - 22, WHITE, 1);
    text_shadow(&small, "R", VIEW_W / 2 + 54, VIEW_H - 22, WHITE, 1);
    offscreen_arrows();
    /* the banner */
    if (banner_t > 0) {
        int a = (int)(256 * clampf(banner_t, 0, 1));
        int y = VIEW_H / 2 - 70;
        text(&large, banner, VIEW_W / 2 - text_w(&large, banner, 1) / 2 + 2, y + 2, 0, 1, a * 3 / 4);
        text_c(&large, banner, VIEW_W / 2, y, GOLD, 1, a);
        text(&small, banner2, VIEW_W / 2 - text_w(&small, banner2, 1) / 2 + 1, y + 27, 0, 1, a * 3 / 4);
        text_c(&small, banner2, VIEW_W / 2, y + 26, WHITE, 1, a);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Menus */

static void menu_tick(void) { snd_play(s_knob, 110, 0, 0); }

#define TITLE_Y 34 /* the big plaque's line: capitals 1.4 x the large sheet's, centred on the plank */
#define TITLE_SCALE 358

static void draw_title(void) {
    if (bg_title.px) img(&bg_title, 0, 0, 256);
    else fill(0x2a1a0a);
    text(&large, "PIRATE SURVIVAL", VIEW_W / 2 - text_w(&large, "PIRATE SURVIVAL", 1) / 2 + 1, 7, 0, 1, 160);
    text_c(&large, "PIRATE SURVIVAL", VIEW_W / 2, 6, GOLD, 1, 256);
    /* the game's name on the big plaque under it, larger, gold on a dark shadow */
    text_smooth(&large, "CHALLENGER DEEP", VIEW_W / 2 + 1, TITLE_Y + 2, 0x1a0c02, TITLE_SCALE, 210);
    text_smooth(&large, "CHALLENGER DEEP", VIEW_W / 2, TITLE_Y, 0xffe6a8, TITLE_SCALE, 256);
    static const char *items[] = { "SET SAIL", "SHIP", "SEA" };
    char sub[64];
    for (int i = 0; i < 3; ++i) {
        int y = 108 + i * 58;
        if (i == menu_sel) {
            rect(VIEW_W / 2 - 136, y - 6, 272, 46, 0x000000, 90);
            rect_frame(VIEW_W / 2 - 136, y - 6, 272, 46, GOLD, 160);
        }
        text_cs(&large, items[i], VIEW_W / 2, y, i == menu_sel ? GOLD : 0xc8b8a0, 1);
        sub[0] = 0;
        if (i == 0 && save.best_wave) snprintf(sub, sizeof sub, "Best: wave %d, %d points", save.best_wave, save.best_score);
        if (i == 0 && !save.best_wave) snprintf(sub, sizeof sub, "Survive as many waves as you can");
        if (i == 1) snprintf(sub, sizeof sub, "%s", hull_name[save.hull]);
        if (i == 2) snprintf(sub, sizeof sub, "%s", seas[save.sea].name);
        text_cs(&small, sub, VIEW_W / 2, y + 23, WHITE, 1);
    }
    text_cs(&small, "Turn the wheel, press Centre", VIEW_W / 2, 282, 0xc8b8a0, 1);
}

/* a stat in a box: its name, and its bar under it, centred on cx */
static void stat_cell(int cx, int y, const char *name, float v) {
    text_cs(&small, name, cx, y, WHITE, 1);
    bar(cx - 36, y + 12, 72, 4, v, GOLD);
}

static void draw_ships_menu(void) {
    if (bg_ship.px) img(&bg_ship, 0, 0, 256);
    else fill(0x2a1a0a);
    /* the background's places: the top middle and right plaques, the right plaque across the
     * middle, the four boxes under them (their two middle ones), the dark strip at the bottom */
    text_cs(&small, "CHOOSE YOUR SHIP", 190, 9, GOLD, 1);
    /* the ship, turning, in the round window */
    int f = (int)(clock_s * 6) % (player_kind.frames ? player_kind.frames : 1);
    if (player_kind.ok) spr_scaled(&player_kind.frame[f], 268, 120, 420, 256);
    char t[32];
    snprintf(t, sizeof t, "%d of %d", save.hull + 1, NHULLS);
    text_cs(&small, t, 304, 9, GOLD, 1); /* the top right plaque, clear of the glass's corner */
    text_cs(&small, hull_name[save.hull], 294, 217, GOLD, 1);
    stat_cell(136, 240, "Speed", (hulls[save.hull].speed - 60) / 60);
    stat_cell(136, 260, "Turning", (hulls[save.hull].turn - 0.8f) / 1.3f);
    stat_cell(136, 280, "Hull", hulls[save.hull].hp / 180);
    stat_cell(239, 245, "Guns", hulls[save.hull].guns / 5.0f);
    stat_cell(239, 270, "Reload", (2.0f - hulls[save.hull].reload) / 1.0f);
    text_cs(&small, "Wheel: ship   Centre: take her", VIEW_W / 2, 308, WHITE, 1); /* on the dark under the boxes */
}

#define SEA_Y 52 /* the sea menu's first name, and the step down to the next */
#define SEA_STEP 46

static void draw_seas_menu(void) {
    if (bg_sea.px) img(&bg_sea, 0, 0, 256);
    else fill(0xd8c090);
    text_cs(&large, "CHOOSE YOUR SEA", 187, 7, GOLD, 1);
    /* the chosen sea's islands, inked on the chart */
    enum { PX = 186, PY = 52, PS = 172 };
    rect(PX - 4, PY - 4, PS + 8, PS + 8, 0x3b2410, 200);
    rect(PX, PY, PS, PS, 0xe8d8b0, 235);
    for (int y = 0; y < PS; ++y)
        for (int x = 0; x < PS; ++x)
            if (sea_preview[save.sea][(y * 188 / PS) * 188 + x * 188 / PS]) fb[(PY + y) * VIEW_W + PX + x] = 0x6a4a28;
    for (int i = 0; i < NSEAS; ++i) {
        int y = SEA_Y + i * SEA_STEP;
        if (i == save.sea) {
            rect(24, y - 6, 148, 31, 0x2a1808, 225);
            rect_frame(24, y - 6, 148, 31, GOLD, 230);
            text_c(&large, seas[i].name, 98, y, GOLD, 1, 256);
        } else {
            text_c(&large, seas[i].name, 98, y, PARCH, 1, 256);
        }
    }
    text_cs(&small, "Wheel: sea   Centre: sail here", VIEW_W / 2, 305, WHITE, 1);
}

static void draw_pause(void) {
    darken(130);
    if (spr_banner.t) spr(&spr_banner, VIEW_W / 2, VIEW_H / 2, 256);
    text_cs(&large, "PAUSED", VIEW_W / 2, VIEW_H / 2 - 46, GOLD, 1);
    static const char *items[] = { "Back to sea", "Strike colours" };
    for (int i = 0; i < 2; ++i) {
        int y = VIEW_H / 2 - 8 + i * 26;
        if (i == pause_sel) rect(VIEW_W / 2 - 80, y - 4, 160, 20, 0, 120), rect_frame(VIEW_W / 2 - 80, y - 4, 160, 20, GOLD, 200);
        text_cs(&small, items[i], VIEW_W / 2, y, i == pause_sel ? GOLD : WHITE, 1);
    }
}

static void draw_over(void) {
    if (bg_over.px) img(&bg_over, 0, 0, 256);
    else fill(0x200808);
    rect(VIEW_W / 2 - 110, 206, 220, 96, 0, 150);
    rect_frame(VIEW_W / 2 - 110, 206, 220, 96, GOLD, 180);
    char t[64];
    snprintf(t, sizeof t, "Sunk in wave %d", wave);
    text_cs(&small, t, VIEW_W / 2, 214, GOLD, 1);
    snprintf(t, sizeof t, "%d ships sent down, %d gold", sunk, gold);
    text_cs(&small, t, VIEW_W / 2, 234, WHITE, 1);
    snprintf(t, sizeof t, over_new_best ? "New best: %d points!" : "%d points (best %d)", score, save.best_score);
    text_cs(&small, t, VIEW_W / 2, 254, over_new_best ? GOLD : WHITE, 1);
    text_cs(&small, "Centre: to port", VIEW_W / 2, 278, 0xc8b8a0, 1);
}

/* ---------------------------------------------------------------------------------------------
 * The loop */

static Input in;
static int wheel_acc;

/* Q2 Pod passes a held button's repeats on as more key-downs: while a key is held, a down is a
 * repeat, and only the first counts. A down REPEAT_GAP after the key's last one is a new press,
 * so a release lost on the way doesn't swallow the next press for good. */
#define REPEAT_GAP 600000
static uint8_t key_held[256];
static uint64_t key_last[256];

static void read_input(void) {
    int kind, a, b, c;
    static int touch_moved;
    while (next_event(&kind, &a, &b, &c)) {
        if (kind == 'k') {
            int k = b & 255;
            uint64_t t = now_us();
            if (a) {
                int repeat = key_held[k] && t - key_last[k] < REPEAT_GAP;
                key_held[k] = 1, key_last[k] = t;
                if (repeat) continue;
            } else {
                key_held[k] = 0;
            }
        }
        if (kind == 'w') in.wheel += a;
        else if (kind == 'k' && a) {
            if (b == K_CENTRE) in.centre = 1, in.centre_held = 1;
            if (b == K_NEXT) in.next = 1;
            if (b == K_PREV) in.prev = 1;
            if (b == K_PLAY) in.play = 1;
            if (b == K_RETURN) in.back = 1;
        } else if (kind == 'k' && !a) {
            if (b == K_CENTRE) in.centre_held = 0;
        } else if (kind == 't') {
            if (a == 'd') in.touch = 1, in.touch_x = b, in.touch_y = c, touch_moved = 0;
            if (a == 'm' && in.touch) {
                int dx = b - in.touch_x, dy = c - in.touch_y;
                if (dx * dx + dy * dy > 15 * 15) touch_moved = 1;
            }
            if (a == 'u' && in.touch) {
                if (!touch_moved) in.tap = 1, in.tap_x = in.touch_x, in.tap_y = in.touch_y;
                in.touch = 0;
            }
        }
    }
}

static void presses_used(void) { in.wheel = in.centre = in.next = in.prev = in.play = in.back = in.tap = 0; }

/* menu steps from the wheel: one a fifth of a turn, clockwise down */
static int wheel_steps(void) {
    wheel_acc += in.wheel;
    int n = 0;
    while (wheel_acc <= -40) wheel_acc += 40, ++n;
    while (wheel_acc >= 40) wheel_acc -= 40, --n;
    return n;
}

static void to_title(void) {
    state = S_TITLE;
    menu_sel = 0;
    snd_stop_all();
    wind_ch = -1;
}


static void update_menus(void) {
    int steps = wheel_steps() + (in.next ? 1 : 0) - (in.prev ? 1 : 0);
    switch (state) {
    case S_TITLE:
        if (steps) menu_sel = clampi(menu_sel + steps, 0, 2), menu_tick();
        if (in.tap)
            for (int i = 0; i < 3; ++i)
                if (in.tap_y >= 102 + i * 58 && in.tap_y < 148 + i * 58) menu_sel = i, in.centre = 1;
        if (in.centre) {
            if (menu_sel == 0) {
                show_loading("Setting sail...");
                if (load_hull(save.hull) && load_sea(save.sea)) {
                    game_start();
                    state = S_PLAY;
                    save_write();
                } else {
                    message("CHALLENGER DEEP", "That ship or sea couldn't be read from the .ipg. challengerdeep.log has more.");
                    sleep_us(3000000);
                }
            } else if (menu_sel == 1) {
                load_hull(save.hull);
                state = S_SHIPS;
            } else {
                state = S_SEAS;
            }
        }
        break;
    case S_SHIPS:
        if (steps) {
            save.hull = (save.hull + steps % NHULLS + NHULLS) % NHULLS;
            load_hull(save.hull);
            menu_tick();
        }
        if (in.centre || in.back || in.play || in.tap) state = S_TITLE, save_write();
        break;
    case S_SEAS:
        if (steps) save.sea = (save.sea + steps % NSEAS + NSEAS) % NSEAS, menu_tick();
        if (in.tap && in.tap_x < 180)
            for (int i = 0; i < NSEAS; ++i)
                if (in.tap_y >= SEA_Y - 14 + i * SEA_STEP && in.tap_y < SEA_Y - 14 + (i + 1) * SEA_STEP) save.sea = i, menu_tick();
        if (in.centre || in.back || in.play) state = S_TITLE, save_write();
        break;
    case S_PAUSE:
        if (steps) pause_sel = clampi(pause_sel + steps, 0, 1), menu_tick();
        if (in.tap) pause_sel = in.tap_y > VIEW_H / 2 + 12, in.centre = 1;
        if (in.play || in.back) state = S_PLAY;
        else if (in.centre) {
            if (pause_sel == 1 && me->sink <= 0) me->hp = 0, sink(me); /* strike the colours: as if sunk */
            state = S_PLAY;
        }
        break;
    case S_OVER:
        if (in.centre || in.tap || in.back || in.play) to_title();
        break;
    }
}

int game_main(void) {
    save_read();
    if (!load_common()) {
        while (!quit_asked()) sleep_us(50000);
        return 0;
    }
    if (getenv("PS_AUTOPLAY")) { /* host tests: straight to sea */
        save.hull = atoi(getenv("PS_AUTOPLAY")) % NHULLS;
        if (getenv("PS_SEA")) save.sea = atoi(getenv("PS_SEA")) % NSEAS;
        if (load_hull(save.hull) && load_sea(save.sea)) game_start(), state = S_PLAY;
    }
    uint64_t last = now_us(), stat_at = last;
    float acc = 0;
    int frames = 0;
    uint64_t draw_us = 0;
    while (!quit_asked()) {
        /* presses and wheel travel gather in `in` until something uses them: a frame too quick
         * for a game step passes them on to the next, rather than losing them */
        read_input();
        uint64_t now = now_us();
        float dt = (float)(unsigned)(now - last) / 1e6f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (state == S_PLAY) {
            if (in.play || in.back) {
                state = S_PAUSE, pause_sel = 0;
                presses_used();
            } else {
                acc += dt;
                while (acc >= STEP) {
                    step_play(&in, STEP);
                    presses_used(); /* the first step takes them */
                    acc -= STEP;
                }
                if (me->sink > 2.8f) {
                    /* the end */
                    over_new_best = score > save.best_score;
                    if (score > save.best_score) save.best_score = score;
                    if (wave > save.best_wave) save.best_wave = wave;
                    save_write();
                    snd_stop_all();
                    wind_ch = -1;
                    state = S_OVER;
                }
            }
        } else {
            clock_s += dt;
            update_menus();
            presses_used();
        }
        uint64_t t0 = now_us();
        clip_reset();
        switch (state) {
        case S_TITLE: draw_title(); break;
        case S_SHIPS: draw_ships_menu(); break;
        case S_SEAS: draw_seas_menu(); break;
        case S_PLAY: draw_sea(), draw_hud(); break;
        case S_PAUSE: draw_sea(), draw_hud(), draw_pause(); break;
        case S_OVER: draw_over(); break;
        }
        present(fb);
        draw_us += now_us() - t0;
        ++frames;
        if (now - stat_at > 10000000) {
            logf_("%.1f fps, %.1f ms a frame drawing; state %d, wave %d, score %d", frames * 1e6 / (unsigned)(now - stat_at),
                  (unsigned)draw_us / 1000.0 / frames, state, wave, score);
            stat_at = now, frames = 0, draw_us = 0;
        }
        /* about 60 frames a second at most */
        uint64_t spent = now_us() - now;
        if (spent < 16000) sleep_us((unsigned)(16000 - spent));
        if (getenv("PS_STATE")) { /* host tests: the state for the driver to read */
            FILE *f = fopen("state.tmp", "w");
            if (f) {
                fprintf(f, "state %d\nwave %d\nscore %d\nsunk %d\nfoes %d\n", state, wave, score, sunk, me ? foes_alive() : 0);
                int nb = 0;
                for (int i = 0; i < MAX_BALLS; ++i) nb += balls[i].alive;
                if (me) fprintf(f, "x %d\ny %d\nang %d\nspeed %d\nsail %d\nhp %d\nballs %d\n", (int)me->x, (int)me->y,
                                (int)(me->ang * 1000), (int)me->speed, me->sail, (int)me->hp, nb);
                fclose(f);
                rename("state.tmp", "state.txt");
            }
        }
    }
    save_write();
    return 0;
}
