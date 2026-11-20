/*
 * PSP2DC companion demo (docs/psp2dc-companion.md, Appendix C): a small game
 * on the television with its dashboard on the PSP. Walk the maze with the
 * d-pad, pick up the five keys, reach the exit; lava costs health. The PSP
 * shows the map as it is found out (the same tiles, sent to it once as
 * sprites), health, score, steps, where you are,
 * what to do next and what you carry; the PSP's L and R move the mark in
 * that list. The game plays the same with no PSP there.
 */
#include <kos.h>
#include <stdio.h>
#include <string.h>
#include "psp2dc.h"
#include "tiles.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define N 20                               /* the maze is N x N tiles */
static const char *const plan[N] = {
    "####################",
    "#P.....#.....~.....#",
    "#.###..#..#..~..##.#",
    "#...#.....#.....#K.#",
    "###.#.#####.###.#..#",
    "#K..#.....#...#.##.#",
    "#.#####.#.#.#.#....#",
    "#.......#...#.####.#",
    "#.##.####.###......#",
    "#..#....~~~...##.#.#",
    "##.####.~K~.#..#.#.#",
    "#.....#.~~~.##.#.#.#",
    "#.###.#.....#..#.#.#",
    "#.#K..####.##.##.#.#",
    "#.####.......#...#.#",
    "#......#####.#.###.#",
    "#.####.#...#.#...#.#",
    "#.#....#.#.#.###.#.#",
    "#...##...#K......#E#",
    "####################",
};
static const char *const key_names[5] = { "Brass key", "Iron key", "Bone key", "Glass key", "Old key" };
enum { W_TITLE, W_HEALTH, W_SCORE, W_STEPS, W_PLACE, W_LAVA, W_TODO, W_ITEMS };   /* the PSP's widgets */

static char map[N][N + 1];                 /* the plan, keys taken out as they are found */
static uint8_t seen[N][N];                 /* tiles the player has been next to: the PSP's map shows only these */
static uint8_t tv_dirty[N][N], psp_dirty[N][N];
static int px, py, health, score, steps, keys, marked;
static char items[128];

/* The tiles, 12 x 12 each, side by side in one picture (tiles.png, made into tiles.h by
 * dreamcast/tools/psp2dc-image.py): the PSP keeps each as a sprite, the television draws
 * them twice the size. */
enum { T_FLOOR, T_WALL, T_LAVA, T_KEY, T_SHUT, T_OPEN, T_PLAYER, TILES };
#define RGB(r, g, b) PSP2DC_RGB565(r, g, b)
static int tile_of(int x, int y)
{
    char c = map[y][x];
    return c == '#' ? T_WALL : c == '~' ? T_LAVA : c == 'E' ? (keys == 5 ? T_OPEN : T_SHUT) : T_FLOOR;
}
static void tv_sprite(int t, int x, int y, int keyed)
{
    int i, j;
    for (j = 0; j < 12; j++)
        for (i = 0; i < 12; i++) {
            int v = (tiles[j * TILES_STRIDE + t * 6 + i / 2] >> (i & 1 ? 0 : 4)) & 15;
            uint16_t *p = vram_s + (y + 2 * j) * 640 + x + 2 * i;
            if (keyed && v == TILES_KEY) continue;
            p[0] = p[1] = p[640] = p[641] = tiles_palette[v];
        }
}
static void tv_tile(int x, int y)          /* 24 pixels a tile */
{
    tv_sprite(tile_of(x, y), x * 24, y * 24, 0);
    if (map[y][x] == 'K') tv_sprite(T_KEY, x * 24, y * 24, 1);
    if (x == px && y == py) tv_sprite(T_PLAYER, x * 24, y * 24, 1);
}
static int psp_tile(int x, int y)          /* 12 pixels a tile, in region 0; 0 when it is queued */
{
    if (!seen[y][x]) return psp2dc_draw_rect(0, x * 12, y * 12, 12, 12, 0);
    if (psp2dc_draw_sprite(0, tile_of(x, y), x * 12, y * 12, -1)) return -1;
    if (map[y][x] == 'K' && psp2dc_draw_sprite(0, T_KEY, x * 12, y * 12, TILES_KEY)) return -1;
    if (x == px && y == py && psp2dc_draw_sprite(0, T_PLAYER, x * 12, y * 12, TILES_KEY)) return -1;
    return 0;
}
static void touch(int x, int y) { tv_dirty[y][x] = psp_dirty[y][x] = 1; }
static void look(void)                     /* the tiles around the player are found out */
{
    int x, y;
    for (y = py - 2; y <= py + 2; y++)
        for (x = px - 2; x <= px + 2; x++)
            if (x >= 0 && y >= 0 && x < N && y < N && !seen[y][x]) { seen[y][x] = 1; psp_dirty[y][x] = 1; }
}
static void text(int row, const char *s)   /* beside the maze on the television, 12 characters a row */
{
    char t[13];
    snprintf(t, sizeof(t), "%-12s", s);
    bfont_draw_str(vram_s + (24 + row * 28) * 640 + 492, 640, 1, t);
}

static void begin(void)
{
    int x, y;
    for (y = 0; y < N; y++) {
        memcpy(map[y], plan[y], N + 1);
        for (x = 0; x < N; x++) {
            if (map[y][x] == 'P') { px = x; py = y; map[y][x] = '.'; }
            seen[y][x] = 0;
            touch(x, y);
        }
    }
    health = 100; score = steps = keys = 0; marked = -1;
    items[0] = 0;
    look();
}
static void faint(void)                    /* no health left: from the start again, for 150 points */
{
    int was = score > 150 ? score - 150 : 0;
    psp2dc_notify("You fainted");
    begin();
    score = was;
}
static void move(int dx, int dy)
{
    int x = px + dx, y = py + dy;
    char s[48];
    if (map[y][x] == '#') return;
    touch(px, py);
    px = x; py = y;
    touch(px, py);
    steps++;
    look();
    if (map[y][x] == 'K') {
        map[y][x] = '.';
        snprintf(s, sizeof(s), "%s found", key_names[keys]);
        psp2dc_notify(s);
        if (keys) strcat(items, "\n");
        strcat(items, key_names[keys]);
        marked = keys++;
        score += 100;
        if (keys == 5) {                   /* the exit opens: its tile changes */
            for (y = 0; y < N; y++)
                for (x = 0; x < N; x++)
                    if (map[y][x] == 'E') touch(x, y);
        }
    } else if (map[y][x] == '~') {
        health -= 8;
    } else if (map[y][x] == 'E' && keys == 5) {
        psp2dc_notify("Out! Once more...");
        x = score + 500 + health;
        begin();
        score = x;
    }
    if (health <= 0) faint();
}

int main(int argc, char **argv)
{
    unsigned frame = 0, last = 0, psp_last = 0;
    int session = -2, x, y, held = 0;
    char s[64];
    (void)argc; (void)argv;

    psp2dc_init();
    /* The PSP's screen: made once, here; the library sends it whenever a PSP is there. */
    psp2dc_region_create(0, 8, 16, 240, 240);
    psp2dc_region_palette(0, 0, 16, tiles_palette);
    for (x = 0; x < TILES; x++) psp2dc_sprite_create(x, 12, 12, TILES_FORMAT, tiles + x * 6, TILES_STRIDE);
    psp2dc_widget_create(W_TITLE, PSP2DC_WIDGET_LABEL, PSP2DC_WF_BIG | PSP2DC_WF_CENTRE, 256, 10, 216, 20, "KEY HUNT");
    psp2dc_widget_create(W_HEALTH, PSP2DC_WIDGET_BAR, PSP2DC_WF_PANEL, 256, 36, 216, 36, "Health");
    psp2dc_widget_create(W_SCORE, PSP2DC_WIDGET_NUMBER, PSP2DC_WF_PANEL | PSP2DC_WF_BIG, 256, 76, 216, 28, "Score");
    psp2dc_widget_create(W_STEPS, PSP2DC_WIDGET_NUMBER, PSP2DC_WF_PANEL, 256, 108, 216, 20, "Steps");
    psp2dc_widget_create(W_PLACE, PSP2DC_WIDGET_LABEL, 0, 262, 133, 204, 10, "");
    psp2dc_widget_create(W_LAVA, PSP2DC_WIDGET_LIGHT, 0, 262, 146, 204, 12, "On lava");
    psp2dc_widget_set_colour(W_LAVA, RGB(240, 80, 30));
    psp2dc_widget_create(W_TODO, PSP2DC_WIDGET_LABEL, 0, 262, 161, 204, 10, "");
    psp2dc_widget_create(W_ITEMS, PSP2DC_WIDGET_LIST, PSP2DC_WF_PANEL, 256, 176, 216, 80, "");
    begin();

    for (;;) {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *cs = cont ? (cont_state_t *)maple_dev_status(cont) : NULL;
        unsigned b = cs ? cs->buttons : 0;
        psp2dc_event_t ev;
        int drew = 0;

        vid_waitvbl();
        frame++;
        psp2dc_update();

        /* the game: a step when the d-pad is pressed, and again every 8 frames while it is held */
        held = (b & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) ? held + 1 : 0;
        if (held == 1 || (held > 16 && held % 8 == 0)) {
            if (b & CONT_DPAD_UP) move(0, -1);
            else if (b & CONT_DPAD_DOWN) move(0, 1);
            else if (b & CONT_DPAD_LEFT) move(-1, 0);
            else if (b & CONT_DPAD_RIGHT) move(1, 0);
        }
        if ((b & CONT_START) && !(last & CONT_START)) begin();
        last = b;
        if (map[py][px] == '~' && frame % 20 == 0 && (health -= 2) <= 0) faint();
        if (map[py][px] != '~' && frame % 45 == 0 && health < 100) health++;

        /* the PSP's own buttons: L and R move the mark in the list of keys */
        while (psp2dc_poll(&ev)) {
            unsigned pb;
            if (ev.type != PSP2DC_REC_BUTTONS || ev.len < 4) continue;
            pb = ev.data[0] | ev.data[1] << 8;
            if (keys && (pb & 0x100) && !(psp_last & 0x100)) marked = (marked + keys - 1) % keys;
            if (keys && (pb & 0x200) && !(psp_last & 0x200)) marked = (marked + 1) % keys;
            psp_last = pb;
        }

        /* the dashboard: every value, every frame; only what changed is sent */
        psp2dc_widget_set_u32(W_HEALTH, (uint32_t)(health < 0 ? 0 : health));
        psp2dc_widget_set_u32(W_SCORE, (uint32_t)score);
        psp2dc_widget_set_u32(W_STEPS, (uint32_t)steps);
        psp2dc_widget_set_u32(W_LAVA, map[py][px] == '~');
        snprintf(s, sizeof(s), "Tile %d, %d", px, py);
        psp2dc_widget_set_text(W_PLACE, s);
        if (keys < 5) snprintf(s, sizeof(s), "Find %d more key%s", 5 - keys, keys == 4 ? "" : "s");
        else snprintf(s, sizeof(s), "Go to the exit");
        psp2dc_widget_set_text(W_TODO, s);
        psp2dc_widget_set_text(W_ITEMS, items);
        psp2dc_widget_set_u32(W_ITEMS, (uint32_t)marked);

        /* the map on the PSP: the tiles that changed, as many as the queue takes; all of them
         * when the PSP started over */
        if (psp2dc_psp() && psp2dc_session() != session) {
            session = psp2dc_session();
            memset(psp_dirty, 1, sizeof(psp_dirty));
        }
        for (y = 0; y < N && psp2dc_psp() && psp2dc_room() > 200; y++)
            for (x = 0; x < N; x++)
                if (psp_dirty[y][x] && psp2dc_room() > 200 && psp_tile(x, y) == 0) { psp_dirty[y][x] = 0; drew = 1; }
        if (drew) psp2dc_present(0);

        /* the television */
        for (y = 0; y < N; y++)
            for (x = 0; x < N; x++)
                if (tv_dirty[y][x]) { tv_dirty[y][x] = 0; tv_tile(x, y); }
        text(0, "KEY HUNT");
        snprintf(s, sizeof(s), "Health %d", health < 0 ? 0 : health); text(2, s);
        snprintf(s, sizeof(s), "Keys %d/5", keys); text(3, s);
        snprintf(s, sizeof(s), "Score %d", score); text(4, s);
        text(6, psp2dc_psp() ? "PSP: yes" : psp2dc_found() ? "PSP: no" : "no adapter");
        text(13, "D-pad: walk");
        text(14, "START: again");
    }
    return 0;
}
