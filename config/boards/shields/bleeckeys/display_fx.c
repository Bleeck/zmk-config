/*
 * Full-screen effects on a canvas in LVGL's top layer, over the status screen:
 *   boot: the word BLEECK, one of three at random (about 2 s):
 *         typed with a cursor, then slid up as the status screen slides in from below;
 *         letters dropped in with a bounce, then dissolved pixel by pixel;
 *         shown, then swept away by a vertical bar
 *   idle: matrix rain for the last RAIN_MS before the display blanks; activity (the same
 *         events ZMK's activity.c counts) ends it
 */

#include <string.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>

#include "display_fx.h"

#define W 128
#define H 32
#define PALETTE_SIZE (4 * sizeof(lv_color32_t))
#define RAIN_MS 2000
#define RAIN_COLS (W / 2)

#define LETTERS 6
#define SPACING 2
#define BASELINE 22
#define TYPE_MS 120
#define HOLD_TICKS 7
#define DROP_STAGGER_MS 120
#define DROP_MS 500
#define DROP_FALL_MS 300

enum { PX_CLEAR, PX_BG, PX_FG }; // canvas palette: transparent, background, text

static const char word[] = "BLEECK";
static lv_coord_t letter_x[LETTERS + 1]; // [LETTERS] is the end of the word

static uint8_t buf[LV_CANVAS_BUF_SIZE_INDEXED_2BIT(W, H)] __aligned(4);
static lv_obj_t *canvas;
static lv_img_dsc_t *img;
static lv_obj_t *status;
static uint32_t rng;

static uint32_t rand_next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void px(int x, int y, uint8_t index) {
    if (x >= 0 && x < W && y >= 0 && y < H) {
        lv_img_buf_set_px_color(img, x, y, (lv_color_t){.full = index});
    }
}

static void fill(uint8_t index) { memset(buf + PALETTE_SIZE, index * 0x55, W * H / 4); }

static void draw_letter(int i, int dy) {
    lv_font_glyph_dsc_t g;
    lv_font_get_glyph_dsc(LV_FONT_DEFAULT, &g, word[i], 0);
    const uint8_t *bitmap = lv_font_get_glyph_bitmap(LV_FONT_DEFAULT, word[i]);
    uint8_t max = (1 << g.bpp) - 1;
    int x0 = letter_x[i] + g.ofs_x;
    int y0 = BASELINE - g.box_h - g.ofs_y + dy;

    for (int n = 0; n < g.box_w * g.box_h; n++) {
        uint32_t bit = n * g.bpp;
        uint8_t v = (bitmap[bit / 8] >> (8 - g.bpp - bit % 8)) & max;
        if (v * 2 > max) {
            px(x0 + n % g.box_w, y0 + n / g.box_w, PX_FG);
        }
    }
}

static void draw_word(int letters) {
    fill(PX_BG);
    for (int i = 0; i < letters; i++) {
        draw_letter(i, 0);
    }
}

static void boot_done(lv_anim_t *a) {
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(canvas, 0);
    lv_obj_set_y(status, 0);
}

static void anim_start(lv_anim_exec_xcb_t exec, int32_t end, uint32_t time, uint32_t delay,
                       lv_anim_path_cb_t path, lv_anim_ready_cb_t done) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, 0, end);
    lv_anim_set_time(&a, time);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_early_apply(&a, false);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_ready_cb(&a, done);
    lv_anim_start(&a);
}

/* (a) typed, then slid up */

static void slide_exec(void *var, int32_t v) {
    lv_obj_set_y(canvas, -v);
    lv_obj_set_y(status, H - v);
}

static void type_tick(lv_timer_t *timer) {
    static int step;
    int letters = MIN(++step, LETTERS);
    bool cursor = step <= LETTERS || (step / 2) % 2 == 0; // solid while typing, then blinks

    draw_word(letters);
    for (int y = BASELINE - 12; cursor && y < BASELINE; y++) {
        px(letter_x[letters], y, PX_FG);
        px(letter_x[letters] + 1, y, PX_FG);
    }
    lv_obj_invalidate(canvas);

    if (step == LETTERS + HOLD_TICKS) {
        lv_timer_del(timer);
        anim_start(slide_exec, H, 350, 0, lv_anim_path_ease_in_out, boot_done);
    }
}

/* (b) dropped in with a bounce, then dissolved */

// Offset of a letter dropping for t ms: falls from above the screen, then bounces once.
static int drop_offset(int t) {
    if (t < DROP_FALL_MS) {
        int left = DROP_FALL_MS - t;
        return -28 * left * left / (DROP_FALL_MS * DROP_FALL_MS);
    }
    return -(4 * lv_trigo_sin((t - DROP_FALL_MS) * 180 / (DROP_MS - DROP_FALL_MS))) >> 15;
}

static void drop_exec(void *var, int32_t t) {
    fill(PX_BG);
    for (int i = 0; i < LETTERS; i++) {
        int lt = t - i * DROP_STAGGER_MS;
        if (lt >= 0) {
            draw_letter(i, drop_offset(MIN(lt, DROP_MS)));
        }
    }
    lv_obj_invalidate(canvas);
}

// A fixed pseudo-random rank per pixel; pixels below the level are already dissolved.
static uint8_t px_rank(int x, int y) {
    uint32_t h = x * 73856093u ^ y * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

static void dissolve_exec(void *var, int32_t level) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (px_rank(x, y) < level) {
                px(x, y, PX_CLEAR);
            }
        }
    }
    lv_obj_invalidate(canvas);
}

static void drop_done(lv_anim_t *a) {
    anim_start(dissolve_exec, 256, 600, 150, lv_anim_path_linear, boot_done);
}

/* (c) shown, then swept away */

static void sweep_exec(void *var, int32_t bar) {
    for (int x = 0; x < bar; x++) {
        for (int y = 0; y < H; y++) {
            px(x, y, PX_CLEAR);
        }
    }
    for (int y = 0; y < H; y++) {
        px(bar, y, PX_FG);
        px(bar + 1, y, PX_FG);
    }
    lv_obj_invalidate(canvas);
}

static void boot_start(void) {
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);

    switch (rand_next() % 3) {
    case 0:
        draw_word(0);
        lv_obj_set_y(status, H);
        lv_timer_create(type_tick, TYPE_MS, NULL);
        break;
    case 1:
        fill(PX_BG);
        anim_start(drop_exec, (LETTERS - 1) * DROP_STAGGER_MS + DROP_MS, 1100, 0,
                   lv_anim_path_linear, drop_done);
        break;
    default:
        draw_word(LETTERS);
        anim_start(sweep_exec, W, 600, 1200, lv_anim_path_ease_in, boot_done);
        break;
    }
}

/* Matrix rain: drops fall in every other column, leaving the background behind them. */

static struct {
    int8_t y;
    uint8_t len;
    bool slow;
} drops[RAIN_COLS];
static lv_timer_t *rain_timer;

static void drop_reset(int i) {
    drops[i].y = -(int8_t)(rand_next() % H);
    drops[i].len = 4 + rand_next() % 8;
    drops[i].slow = rand_next() % 2;
}

static void rain_tick(lv_timer_t *timer) {
    static uint8_t frame;

    frame++;
    for (int i = 0; i < RAIN_COLS; i++) {
        int x = i * 2;
        if (drops[i].slow && frame % 2) {
            continue;
        }
        int y = ++drops[i].y;
        px(x, y, PX_FG);
        px(x, y - 1, rand_next() % 3 ? PX_FG : PX_BG);
        px(x, y - drops[i].len, PX_BG);
        px(x + 1, y - drops[i].len, PX_BG);
        if (y - drops[i].len >= H) {
            drop_reset(i);
        }
    }
    lv_obj_invalidate(canvas);
}

static void rain_start_cb(struct k_work *work) {
    if (canvas == NULL || rain_timer != NULL) {
        return;
    }
    for (int i = 0; i < RAIN_COLS; i++) {
        drop_reset(i);
    }
    fill(PX_CLEAR);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    rain_timer = lv_timer_create(rain_tick, 40, NULL);
}

static void rain_stop_cb(struct k_work *work) {
    if (rain_timer == NULL) {
        return;
    }
    lv_timer_del(rain_timer);
    rain_timer = NULL;
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
}

static K_WORK_DELAYABLE_DEFINE(rain_start_work, rain_start_cb);
static K_WORK_DEFINE(rain_stop_work, rain_stop_cb);

static void note_activity(void) {
    k_work_reschedule_for_queue(zmk_display_work_q(), &rain_start_work,
                                K_MSEC(CONFIG_ZMK_IDLE_TIMEOUT - RAIN_MS));
    k_work_submit_to_queue(zmk_display_work_q(), &rain_stop_work);
}

static int display_fx_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        note_activity();
    } else if (ev->state != ZMK_ACTIVITY_ACTIVE) {
        k_work_submit_to_queue(zmk_display_work_q(), &rain_stop_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_fx, display_fx_listener);
ZMK_SUBSCRIPTION(display_fx, zmk_position_state_changed);
ZMK_SUBSCRIPTION(display_fx, zmk_sensor_event);
ZMK_SUBSCRIPTION(display_fx, zmk_activity_state_changed);

static void display_fx_input(struct input_event *ev) { note_activity(); }

INPUT_CALLBACK_DEFINE(NULL, display_fx_input);

void display_fx_init(lv_obj_t *status_screen) {
    status = status_screen;
    rng = sys_rand32_get() | 1;

    canvas = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas, buf, W, H, LV_IMG_CF_INDEXED_2BIT);
    img = lv_canvas_get_img(canvas);
    lv_canvas_set_palette(canvas, PX_BG, lv_obj_get_style_bg_color(status, LV_PART_MAIN));
    lv_canvas_set_palette(canvas, PX_FG, lv_obj_get_style_text_color(status, LV_PART_MAIN));
    ((lv_color32_t *)buf)[PX_CLEAR].full = 0;

    lv_coord_t x = 0;
    for (int i = 0; i < LETTERS; i++) {
        letter_x[i] = x;
        x += lv_font_get_glyph_width(LV_FONT_DEFAULT, word[i], word[i + 1]) + SPACING;
    }
    letter_x[LETTERS] = x;
    for (int i = 0; i <= LETTERS; i++) {
        letter_x[i] += (W - x) / 2;
    }

    boot_start();
    k_work_reschedule_for_queue(zmk_display_work_q(), &rain_start_work,
                                K_MSEC(CONFIG_ZMK_IDLE_TIMEOUT - RAIN_MS));
}
