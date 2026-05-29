#ifndef SpaceInvadersScreen_h
#define SpaceInvadersScreen_h

#include <lvgl.h>
#include <ui.h>
#include "Audio/ToneService.h"

// =====================================================================
// Easter egg: minimal Space Invaders. Hold the rat image on the main
// menu for 5 s to land here.
//
//   - 24 aliens (4×6 grid) marching down toward the player
//   - 1 player ship, 1 bullet, up to 3 alien bombs
//   - On-screen LEFT / RIGHT (hold) and FIRE (tap) controls
//   - Score + lives HUD at top, BACK at the very bottom
//
// 20 FPS game tick via lv_timer; all widgets are simple lv_obj
// rectangles styled at create time, then moved by lv_obj_set_pos.
// =====================================================================

#define SI_COLS       6
#define SI_ROWS       4
#define SI_ALIENS     (SI_COLS * SI_ROWS)
#define SI_ALIEN_W    24
#define SI_ALIEN_H    14
#define SI_SPACING_X  10
#define SI_SPACING_Y  8
#define SI_GRID_X0    32
#define SI_GRID_Y0    50
#define SI_PLAY_H     390     // game area height before controls strip
#define SI_PLAY_BOT   (SI_PLAY_H - 4)
#define SI_PLAYER_W   28
#define SI_PLAYER_H   10
#define SI_PLAYER_Y   (SI_PLAY_BOT - SI_PLAYER_H)
#define SI_BULLET_W   3
#define SI_BULLET_H   8
#define SI_MAX_BOMBS  3
#define SI_TICK_MS    50

static lv_obj_t *ui_scrSpaceInvaders   = NULL;
static lv_obj_t *si_aliens[SI_ALIENS]  = {NULL};
static lv_obj_t *si_player             = NULL;
static lv_obj_t *si_bullet             = NULL;
static lv_obj_t *si_bombs[SI_MAX_BOMBS]= {NULL};
static lv_obj_t *si_ufo                = NULL;
static lv_obj_t *si_lblScore           = NULL;
static lv_obj_t *si_lblLives           = NULL;
static lv_obj_t *si_lblStatus          = NULL;
static lv_obj_t *si_btnFire            = NULL;
static lv_timer_t *si_tickTimer        = NULL;

static bool  si_alive[SI_ALIENS];
static int   si_alien_x;
static int   si_alien_y;
static int   si_alien_dir;          // +1 right, -1 left
static int   si_alien_count;
static uint32_t si_last_alien_move;
static uint8_t si_march_step;       // cycles 0..3 for the 4-note pattern
static int   si_player_x;
static int   si_player_dx;          // -1 / 0 / +1
static int   si_bullet_x = -1, si_bullet_y;
static int   si_bombs_x[SI_MAX_BOMBS], si_bombs_y[SI_MAX_BOMBS];
static int   si_ufo_x = -1;         // -1 = inactive, else current x
static int   si_ufo_dir;            // +1 / -1
static uint32_t si_ufo_next_ms;     // when to spawn the next UFO
static int   si_score;
static int   si_lives;
static int   si_level;
static bool  si_game_over;
static bool  si_paused;             // becomes true on game-over / win
static uint32_t si_player_hit_until; // freeze player for a beat after death

#define SI_UFO_W       30
#define SI_UFO_H       12
#define SI_UFO_Y       30          // just under the HUD strip

// -----------------------------------------------------------------
// Forward decls
// -----------------------------------------------------------------
static void si_open();
static void si_close();
static void si_reset();
static void si_tick(lv_timer_t *t);
static void si_show_status(const char *msg, uint32_t color);
static void si_clear_status();

// -----------------------------------------------------------------
// Build / show
// -----------------------------------------------------------------
static lv_obj_t *si_mk_rect(lv_obj_t *parent, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Helper: paint a child rect inside an invader container at local (x,y)
static void si_pixel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

// -----------------------------------------------------------------
// Composite invader sprites — fitted inside a 24×14 container so the
// existing alien grid positions remain valid. Three classic types:
//
//   SQUID (top row)    : antennae + small head + skinny legs
//   CRAB  (mid rows)   : two-eye head, segmented body, side arms
//   OCTOPUS (bottom)   : wide body, four hanging tentacles
//
// All aliens use the same bright pastel palette so the iconic
// "row colors come from a CRT overlay" arcade look reads correctly.
// -----------------------------------------------------------------
enum SiAlienType { SI_SQUID = 0, SI_CRAB = 1, SI_OCTOPUS = 2 };

static lv_obj_t *si_mk_alien(lv_obj_t *parent, SiAlienType type, uint32_t color) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, SI_ALIEN_W, SI_ALIEN_H);
    lv_obj_set_style_bg_opa(p, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);

    const uint32_t EYE = 0x000000;

    switch (type) {
        case SI_SQUID:
            // Antenna pair on top
            si_pixel(p, 10, 0, 2, 3, color);
            si_pixel(p, 12, 0, 2, 3, color);
            // Head
            si_pixel(p,  8, 3, 8, 2, color);
            // Eyes (dark)
            si_pixel(p,  9, 4, 2, 1, EYE);
            si_pixel(p, 13, 4, 2, 1, EYE);
            // Wide body
            si_pixel(p,  4, 5, 16, 4, color);
            // Outer arms
            si_pixel(p,  2, 7, 2, 2, color);
            si_pixel(p, 20, 7, 2, 2, color);
            // Skinny legs
            si_pixel(p,  6, 10, 2, 4, color);
            si_pixel(p, 16, 10, 2, 4, color);
            si_pixel(p, 10, 10, 2, 3, color);
            si_pixel(p, 12, 10, 2, 3, color);
            break;

        case SI_CRAB:
            // Outer eyes/antennae
            si_pixel(p,  4, 1, 2, 3, color);
            si_pixel(p, 18, 1, 2, 3, color);
            // Head stripe
            si_pixel(p,  6, 3, 12, 2, color);
            // Eyes
            si_pixel(p,  8, 3, 2, 2, EYE);
            si_pixel(p, 14, 3, 2, 2, EYE);
            // Body widest
            si_pixel(p,  2, 5, 20, 3, color);
            // Notched belly
            si_pixel(p,  4, 8, 4, 2, color);
            si_pixel(p, 10, 8, 4, 2, color);
            si_pixel(p, 16, 8, 4, 2, color);
            // Side arms (drooping)
            si_pixel(p,  0, 10, 2, 3, color);
            si_pixel(p, 22, 10, 2, 3, color);
            // Inner legs
            si_pixel(p,  6, 11, 3, 3, color);
            si_pixel(p, 15, 11, 3, 3, color);
            break;

        case SI_OCTOPUS:
            // Small dome top
            si_pixel(p,  8, 0, 8, 2, color);
            // Side bulges
            si_pixel(p,  4, 2, 4, 2, color);
            si_pixel(p, 16, 2, 4, 2, color);
            // Eyes
            si_pixel(p,  6, 2, 2, 2, EYE);
            si_pixel(p, 16, 2, 2, 2, EYE);
            // Wide body
            si_pixel(p,  2, 4, 20, 4, color);
            // Tentacles
            si_pixel(p,  2, 8,  2, 4, color);
            si_pixel(p,  6, 8,  2, 5, color);
            si_pixel(p, 10, 8,  2, 4, color);
            si_pixel(p, 14, 8,  2, 4, color);
            si_pixel(p, 18, 8,  2, 5, color);
            si_pixel(p, 22, 8,  2, 4, color);
            // Curled foot tips
            si_pixel(p,  0, 10, 2, 2, color);
            si_pixel(p, 22, 10, 2, 2, color);
            break;
    }

    return p;
}

// Player ship — three-piece cannon: base + barrel mount + barrel tip.
static lv_obj_t *si_mk_player(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, SI_PLAYER_W, SI_PLAYER_H + 4);
    lv_obj_set_style_bg_opa(p, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    const uint32_t COL = 0x00FFAA;
    // Barrel tip
    si_pixel(p, SI_PLAYER_W / 2 - 1, 0, 2, 3, COL);
    // Barrel mount
    si_pixel(p, SI_PLAYER_W / 2 - 3, 3, 6, 3, COL);
    // Base / tracks
    si_pixel(p, 0, 6, SI_PLAYER_W, 6, COL);
    return p;
}

// UFO saucer — oval body + a few "windows".
static lv_obj_t *si_mk_ufo(lv_obj_t *parent) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, SI_UFO_W, SI_UFO_H);
    lv_obj_set_style_bg_opa(p, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    const uint32_t COL = 0xFF44CC;
    // Dome
    si_pixel(p, 10, 0,  10, 3, COL);
    // Mid-body widest
    si_pixel(p,  4, 3,  22, 4, COL);
    // Underside taper
    si_pixel(p,  8, 7,  14, 2, COL);
    // Window lights (dark)
    si_pixel(p,  8, 4,  2, 2, 0x000000);
    si_pixel(p, 14, 4,  2, 2, 0x000000);
    si_pixel(p, 20, 4,  2, 2, 0x000000);
    return p;
}

static void si_screen_init() {
    if (ui_scrSpaceInvaders) return;

    ui_scrSpaceInvaders = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_scrSpaceInvaders, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_scrSpaceInvaders, 255, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui_scrSpaceInvaders, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ui_scrSpaceInvaders, LV_OBJ_FLAG_SCROLLABLE);

    // Title banner — WAVE INVADERZ
    lv_obj_t *si_lblTitle = lv_label_create(ui_scrSpaceInvaders);
    lv_obj_set_width(si_lblTitle, 320);
    lv_obj_set_pos(si_lblTitle, 0, 2);
    lv_label_set_text(si_lblTitle, "WAVE INVADERZ");
    lv_obj_set_style_text_align(si_lblTitle, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(si_lblTitle, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_font(si_lblTitle, &ui_font_Verdana14, LV_PART_MAIN);

    // HUD: SCORE + LIVES
    si_lblScore = lv_label_create(ui_scrSpaceInvaders);
    lv_obj_set_pos(si_lblScore, 6, 22);
    lv_label_set_text(si_lblScore, "SCORE: 0");
    lv_obj_set_style_text_color(si_lblScore, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(si_lblScore, &ui_font_Verdana14, LV_PART_MAIN);

    si_lblLives = lv_label_create(ui_scrSpaceInvaders);
    lv_obj_set_pos(si_lblLives, 220, 22);
    lv_label_set_text(si_lblLives, "LIVES: 3");
    lv_obj_set_style_text_color(si_lblLives, lv_color_hex(0xFF6688), LV_PART_MAIN);
    lv_obj_set_style_text_font(si_lblLives, &ui_font_Verdana14, LV_PART_MAIN);

    si_lblStatus = lv_label_create(ui_scrSpaceInvaders);
    lv_obj_set_width(si_lblStatus, 280);
    lv_obj_set_pos(si_lblStatus, 20, 40);
    lv_label_set_text(si_lblStatus, "");
    lv_obj_set_style_text_color(si_lblStatus, lv_color_hex(0xFF9100), LV_PART_MAIN);
    lv_obj_set_style_text_align(si_lblStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(si_lblStatus, &ui_font_Verdana16, LV_PART_MAIN);

    // Aliens — three classic invader silhouettes by row, each painted
    // in a bright arcade colour. Row 0 = squid, rows 1-2 = crab,
    // row 3 = octopus.
    static const uint32_t ROW_COLORS[SI_ROWS] = {0x88FFFF, 0xFFCC44, 0xFFCC44, 0x88FF88};
    static const SiAlienType ROW_TYPES[SI_ROWS] = { SI_SQUID, SI_CRAB, SI_CRAB, SI_OCTOPUS };
    for (int r = 0; r < SI_ROWS; r++) {
        for (int c = 0; c < SI_COLS; c++) {
            int idx = r * SI_COLS + c;
            si_aliens[idx] = si_mk_alien(ui_scrSpaceInvaders, ROW_TYPES[r], ROW_COLORS[r]);
        }
    }

    // Player ship — multi-piece cannon (base + barrel mount + tip)
    si_player = si_mk_player(ui_scrSpaceInvaders);

    // Bullet (off-screen until fired)
    si_bullet = si_mk_rect(ui_scrSpaceInvaders, SI_BULLET_W, SI_BULLET_H, 0xFFFFFF);
    lv_obj_add_flag(si_bullet, LV_OBJ_FLAG_HIDDEN);

    // Bombs
    for (int i = 0; i < SI_MAX_BOMBS; i++) {
        si_bombs[i] = si_mk_rect(ui_scrSpaceInvaders, SI_BULLET_W, SI_BULLET_H, 0xFF4466);
        lv_obj_add_flag(si_bombs[i], LV_OBJ_FLAG_HIDDEN);
        si_bombs_x[i] = -1;
    }

    // Bonus UFO that crosses the top occasionally — multi-piece saucer.
    si_ufo = si_mk_ufo(ui_scrSpaceInvaders);
    lv_obj_add_flag(si_ufo, LV_OBJ_FLAG_HIDDEN);

    // Controls bar — LEFT / FIRE / RIGHT / BACK
    int ctrlY = SI_PLAY_H + 4;
    auto mkBtn = [&](int x, int w, const char *label, uint32_t bg) {
        lv_obj_t *b = lv_btn_create(ui_scrSpaceInvaders);
        lv_obj_set_pos(b, x, ctrlY);
        lv_obj_set_size(b, w, 56);
        lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
        lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, label);
        lv_obj_center(l);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(l, &ui_font_Verdana18, LV_PART_MAIN);
        return b;
    };

    lv_obj_t *btnL = mkBtn(6,   70, "<", 0x1A3366);
    lv_obj_add_event_cb(btnL, [](lv_event_t *e) { si_player_dx = -1; }, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btnL, [](lv_event_t *e) { si_player_dx =  0; }, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(btnL, [](lv_event_t *e) { si_player_dx =  0; }, LV_EVENT_PRESS_LOST, NULL);

    si_btnFire = mkBtn(82,  74, "FIRE", 0x663300);
    lv_obj_add_event_cb(si_btnFire, [](lv_event_t *e) {
        if (si_paused) { si_reset(); return; }
        if (si_player_hit_until > millis()) return;   // dead-frame freeze
        if (si_bullet_x < 0) {
            si_bullet_x = si_player_x + SI_PLAYER_W / 2 - SI_BULLET_W / 2;
            si_bullet_y = SI_PLAYER_Y - SI_BULLET_H - 1;
            lv_obj_clear_flag(si_bullet, LV_OBJ_FLAG_HIDDEN);
            tone_play(&TONE_SI_SHOOT);
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btnR = mkBtn(160, 70, ">", 0x1A3366);
    lv_obj_add_event_cb(btnR, [](lv_event_t *e) { si_player_dx =  1; }, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btnR, [](lv_event_t *e) { si_player_dx =  0; }, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(btnR, [](lv_event_t *e) { si_player_dx =  0; }, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *btnBack = mkBtn(236, 78, "BACK", 0x333355);
    lv_obj_add_event_cb(btnBack, [](lv_event_t *e) { si_close(); }, LV_EVENT_CLICKED, NULL);
}

static void si_show_status(const char *msg, uint32_t color) {
    if (!si_lblStatus) return;
    lv_label_set_text(si_lblStatus, msg);
    lv_obj_set_style_text_color(si_lblStatus, lv_color_hex(color), LV_PART_MAIN);
}
static void si_clear_status() { si_show_status("", 0xFF9100); }

// Reset only the swarm + bullets (used for next-level restart, keeps score)
static void si_reset_wave() {
    si_alien_x = SI_GRID_X0;
    si_alien_y = SI_GRID_Y0 + (si_level - 1) * 8;   // start a notch lower each level
    si_alien_dir = 1;
    si_alien_count = SI_ALIENS;
    si_last_alien_move = millis();
    si_march_step = 0;
    for (int i = 0; i < SI_ALIENS; i++) {
        si_alive[i] = true;
        if (si_aliens[i]) lv_obj_clear_flag(si_aliens[i], LV_OBJ_FLAG_HIDDEN);
    }
    si_bullet_x = -1;
    if (si_bullet) lv_obj_add_flag(si_bullet, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < SI_MAX_BOMBS; i++) {
        si_bombs_x[i] = -1;
        lv_obj_add_flag(si_bombs[i], LV_OBJ_FLAG_HIDDEN);
    }
    si_ufo_x = -1;
    if (si_ufo) lv_obj_add_flag(si_ufo, LV_OBJ_FLAG_HIDDEN);
    si_ufo_next_ms = millis() + 12000 + (esp_random() % 10000);
}

static void si_reset() {
    si_level = 1;
    si_score = 0;
    si_lives = 3;
    si_game_over = false;
    si_paused = false;
    si_player_hit_until = 0;
    si_player_x = 320 / 2 - SI_PLAYER_W / 2;
    si_player_dx = 0;
    si_reset_wave();
    lv_label_set_text(si_lblScore, "SCORE: 0");
    lv_label_set_text(si_lblLives, "LIVES: 3");
    si_clear_status();
}

static void si_tick(lv_timer_t * /*t*/) {
    if (!ui_scrSpaceInvaders) return;
    if (lv_scr_act() != ui_scrSpaceInvaders) return;
    if (si_paused) return;

    // --- Player ---
    if (si_player_hit_until == 0 || millis() > si_player_hit_until) {
        si_player_x += si_player_dx * 4;
        if (si_player_x < 2)            si_player_x = 2;
        if (si_player_x > 320 - SI_PLAYER_W - 2) si_player_x = 320 - SI_PLAYER_W - 2;
        lv_obj_set_pos(si_player, si_player_x, SI_PLAYER_Y);
        if (si_player_hit_until && millis() > si_player_hit_until) {
            // Just respawned — bring the ship back if it was hidden
            lv_obj_clear_flag(si_player, LV_OBJ_FLAG_HIDDEN);
            si_player_hit_until = 0;
        }
    }

    // --- Aliens (move as a group) ---
    // Speed scales with remaining aliens: fewer alive = faster. Step
    // each cycle plays one of the four march notes for the iconic tempo.
    uint32_t step_ms = 80 + (uint32_t)(si_alien_count * 25);
    if (millis() - si_last_alien_move >= step_ms) {
        si_last_alien_move = millis();
        tone_play_alien_step(si_march_step & 3);
        si_march_step++;
        // Find current bounds
        int min_c = SI_COLS, max_c = -1;
        int max_r = -1;
        for (int r = 0; r < SI_ROWS; r++)
            for (int c = 0; c < SI_COLS; c++)
                if (si_alive[r * SI_COLS + c]) {
                    if (c < min_c) min_c = c;
                    if (c > max_c) max_c = c;
                    if (r > max_r) max_r = r;
                }
        int left_x  = si_alien_x + min_c * (SI_ALIEN_W + SI_SPACING_X);
        int right_x = si_alien_x + max_c * (SI_ALIEN_W + SI_SPACING_X) + SI_ALIEN_W;
        bool drop = false;
        if (si_alien_dir > 0 && right_x + 6 >= 320 - 2) { si_alien_dir = -1; drop = true; }
        else if (si_alien_dir < 0 && left_x - 6 <= 2)   { si_alien_dir =  1; drop = true; }
        if (drop) si_alien_y += 8;
        else      si_alien_x += si_alien_dir * 6;

        // Reposition alien sprites
        for (int r = 0; r < SI_ROWS; r++)
            for (int c = 0; c < SI_COLS; c++) {
                int idx = r * SI_COLS + c;
                if (!si_alive[idx]) continue;
                int x = si_alien_x + c * (SI_ALIEN_W + SI_SPACING_X);
                int y = si_alien_y + r * (SI_ALIEN_H + SI_SPACING_Y);
                lv_obj_set_pos(si_aliens[idx], x, y);
            }

        // Did aliens reach the player line?
        int bottom_y = si_alien_y + max_r * (SI_ALIEN_H + SI_SPACING_Y) + SI_ALIEN_H;
        if (max_r >= 0 && bottom_y >= SI_PLAYER_Y) {
            si_game_over = true;
            si_paused = true;
            si_show_status("INVADED! Tap FIRE to retry", 0xFF4466);
            tone_play(&TONE_SI_HIT);
            return;
        }

        // Random alien bomb drop (chance scales with remaining)
        if ((esp_random() % 6) == 0) {
            // Pick a random alive column's bottom-most alien
            int col = esp_random() % SI_COLS;
            for (int r = SI_ROWS - 1; r >= 0; r--) {
                int idx = r * SI_COLS + col;
                if (si_alive[idx]) {
                    // Find a free bomb slot
                    for (int b = 0; b < SI_MAX_BOMBS; b++) {
                        if (si_bombs_x[b] < 0) {
                            si_bombs_x[b] = si_alien_x + col * (SI_ALIEN_W + SI_SPACING_X) + SI_ALIEN_W / 2 - SI_BULLET_W / 2;
                            si_bombs_y[b] = si_alien_y + r * (SI_ALIEN_H + SI_SPACING_Y) + SI_ALIEN_H + 1;
                            lv_obj_clear_flag(si_bombs[b], LV_OBJ_FLAG_HIDDEN);
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    // --- Bullet ---
    if (si_bullet_x >= 0) {
        si_bullet_y -= 7;
        if (si_bullet_y < 20) {
            si_bullet_x = -1;
            lv_obj_add_flag(si_bullet, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_pos(si_bullet, si_bullet_x, si_bullet_y);
            // Hit detect against aliens
            for (int r = 0; r < SI_ROWS && si_bullet_x >= 0; r++)
                for (int c = 0; c < SI_COLS && si_bullet_x >= 0; c++) {
                    int idx = r * SI_COLS + c;
                    if (!si_alive[idx]) continue;
                    int ax = si_alien_x + c * (SI_ALIEN_W + SI_SPACING_X);
                    int ay = si_alien_y + r * (SI_ALIEN_H + SI_SPACING_Y);
                    if (si_bullet_x + SI_BULLET_W > ax && si_bullet_x < ax + SI_ALIEN_W &&
                        si_bullet_y + SI_BULLET_H > ay && si_bullet_y < ay + SI_ALIEN_H) {
                        si_alive[idx] = false;
                        lv_obj_add_flag(si_aliens[idx], LV_OBJ_FLAG_HIDDEN);
                        si_alien_count--;
                        si_score += 10;
                        char buf[24]; snprintf(buf, sizeof(buf), "SCORE: %d", si_score);
                        lv_label_set_text(si_lblScore, buf);
                        si_bullet_x = -1;
                        lv_obj_add_flag(si_bullet, LV_OBJ_FLAG_HIDDEN);
                        tone_play(&TONE_SI_KILL);
                    }
                }
        }
        // Bullet hits UFO?
        if (si_bullet_x >= 0 && si_ufo_x >= 0) {
            if (si_bullet_x + SI_BULLET_W > si_ufo_x && si_bullet_x < si_ufo_x + SI_UFO_W &&
                si_bullet_y < SI_UFO_Y + SI_UFO_H && si_bullet_y + SI_BULLET_H > SI_UFO_Y) {
                si_ufo_x = -1;
                lv_obj_add_flag(si_ufo, LV_OBJ_FLAG_HIDDEN);
                si_bullet_x = -1;
                lv_obj_add_flag(si_bullet, LV_OBJ_FLAG_HIDDEN);
                int bonus = 50 + 50 * (esp_random() % 4);   // 50/100/150/200
                si_score += bonus;
                char buf[24]; snprintf(buf, sizeof(buf), "SCORE: %d", si_score);
                lv_label_set_text(si_lblScore, buf);
                tone_play(&TONE_SI_UFO_HIT);
                si_ufo_next_ms = millis() + 15000 + (esp_random() % 12000);
            }
        }
    }

    // --- UFO (bonus saucer) ---
    if (si_ufo_x < 0 && millis() >= si_ufo_next_ms && si_alien_count > 4) {
        si_ufo_dir = (esp_random() & 1) ? 1 : -1;
        si_ufo_x = (si_ufo_dir > 0) ? -SI_UFO_W : 320;
        lv_obj_clear_flag(si_ufo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(si_ufo, si_ufo_x, SI_UFO_Y);
        tone_play(&TONE_SI_UFO);
    } else if (si_ufo_x >= 0) {
        si_ufo_x += si_ufo_dir * 2;
        lv_obj_set_pos(si_ufo, si_ufo_x, SI_UFO_Y);
        // Re-trigger drone every ~600ms while crossing
        static uint32_t last_drone = 0;
        if (millis() - last_drone > 600) {
            last_drone = millis();
            tone_play(&TONE_SI_UFO);
        }
        if ((si_ufo_dir > 0 && si_ufo_x > 320) || (si_ufo_dir < 0 && si_ufo_x < -SI_UFO_W)) {
            si_ufo_x = -1;
            lv_obj_add_flag(si_ufo, LV_OBJ_FLAG_HIDDEN);
            si_ufo_next_ms = millis() + 15000 + (esp_random() % 12000);
        }
    }

    // --- Bombs ---
    for (int b = 0; b < SI_MAX_BOMBS; b++) {
        if (si_bombs_x[b] < 0) continue;
        si_bombs_y[b] += 4;
        if (si_bombs_y[b] > SI_PLAY_BOT) {
            si_bombs_x[b] = -1;
            lv_obj_add_flag(si_bombs[b], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(si_bombs[b], si_bombs_x[b], si_bombs_y[b]);
        // Bomb vs player
        if (si_bombs_x[b] + SI_BULLET_W > si_player_x && si_bombs_x[b] < si_player_x + SI_PLAYER_W &&
            si_bombs_y[b] + SI_BULLET_H > SI_PLAYER_Y && si_bombs_y[b] < SI_PLAYER_Y + SI_PLAYER_H) {
            si_bombs_x[b] = -1;
            lv_obj_add_flag(si_bombs[b], LV_OBJ_FLAG_HIDDEN);
            si_lives--;
            char buf[24]; snprintf(buf, sizeof(buf), "LIVES: %d", si_lives);
            lv_label_set_text(si_lblLives, buf);
            tone_play(&TONE_SI_HIT);
            // Freeze + blank the player ship for ~900ms, like the arcade
            lv_obj_add_flag(si_player, LV_OBJ_FLAG_HIDDEN);
            si_player_hit_until = millis() + 900;
            si_player_dx = 0;
            if (si_lives <= 0) {
                si_game_over = true;
                si_paused = true;
                si_show_status("GAME OVER — Tap FIRE", 0xFF4466);
                return;
            }
        }
    }

    // --- Win check — advance to next level ---
    if (si_alien_count == 0 && !si_paused) {
        si_level++;
        char buf[32]; snprintf(buf, sizeof(buf), "WAVE %d!", si_level);
        si_show_status(buf, 0x00FF88);
        si_reset_wave();
        // Brief celebratory pause via short status linger — no full pause
    }
}

static void si_open() {
    si_screen_init();
    si_reset();
    lv_scr_load(ui_scrSpaceInvaders);
    if (!si_tickTimer) {
        si_tickTimer = lv_timer_create(si_tick, SI_TICK_MS, NULL);
    } else {
        lv_timer_resume(si_tickTimer);
    }
}

static void si_close() {
    if (si_tickTimer) lv_timer_pause(si_tickTimer);
    si_player_dx = 0;
    lv_scr_load(ui_scrMain);
}

#endif
