#define _GNU_SOURCE
#include "mpc_fb.h"
#include "plugin_dir.h"
#include "font8x8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

#define SCREEN_W MPC_FB_WIDTH
#define SCREEN_H MPC_FB_HEIGHT
#define SCREEN_STRIDE MPC_FB_STRIDE

static pthread_t g_fb_thread;
static volatile int g_fb_running = 0;
static mpc_fb_render_fn g_render_callback = NULL;
static pthread_mutex_t g_callback_lock = PTHREAD_MUTEX_INITIALIZER;

#define MAX_FB_MAPS 16
typedef struct {
    uintptr_t start;
    uintptr_t end;
    size_t size;
    char name[256];
} fb_map_t;

static fb_map_t s_fbs[MAX_FB_MAPS];
static int s_num_fbs = 0;

static void log_fb(const char *msg) {
    FILE *f = fopen("/tmp/mpc_fb.log", "a");
    if (f) {
        time_t now = time(NULL);
        char tbuf[32];
        strftime(tbuf, sizeof(tbuf), "%H:%M:%S", localtime(&now));
        fprintf(f, "[%s] %s\n", tbuf, msg);
        fclose(f);
    }
}

/* ---- Timing & Configuration ---- */
typedef struct {
    uint32_t interval_us; /* Sleep interval in microseconds (default 16666 = 60 FPS) */
    uint32_t offset_us;   /* Offset / delay before drawing in microseconds (default 0) */
    int repeat;           /* Draw burst count (default 1) */
    int continuous;       /* 1 = continuous 1ms draw loop */
    int tab_detect;       /* 1 = only render when on active tab (default 1) */
    int card_y;           /* Y position on screen (default 112) */
    int debug;            /* 1 = verbose logging */
} fb_timing_cfg_t;

static fb_timing_cfg_t g_last_cfg = {
    .interval_us = 16666,
    .offset_us = 0,
    .repeat = 1,
    .continuous = 0,
    .tab_detect = 1,
    .card_y = 112,
    .debug = 0
};

static void read_timing_config(fb_timing_cfg_t *cfg, const char *plugin_dir) {
    cfg->tab_detect = 1; /* Default to checking tab */
    cfg->card_y = 112;   /* Default to Y=112 */
    char path[512] = {0};
    FILE *f = NULL;
    if (plugin_dir && plugin_dir[0]) {
        snprintf(path, sizeof(path), "%s/fb_timing.txt", plugin_dir);
        f = fopen(path, "r");
        if (!f) {
            snprintf(path, sizeof(path), "%s/timing.txt", plugin_dir);
            f = fopen(path, "r");
        }
    }
    if (!f) {
        snprintf(path, sizeof(path), "/tmp/fb_timing.txt");
        f = fopen(path, "r");
    }
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;

        char key[64] = {0};
        int val = 0;
        if (sscanf(p, "%63[^=]=%d", key, &val) == 2) {
            char *end = key + strlen(key) - 1;
            while (end > key && (*end == ' ' || *end == '\t')) *end-- = 0;
            if (!strcmp(key, "interval_us") || !strcmp(key, "sleep_us")) {
                if (val >= 100 && val <= 500000) cfg->interval_us = (uint32_t)val;
            } else if (!strcmp(key, "offset_us") || !strcmp(key, "delay_us")) {
                if (val >= 0 && val <= 100000) cfg->offset_us = (uint32_t)val;
            } else if (!strcmp(key, "repeat") || !strcmp(key, "burst")) {
                if (val >= 1 && val <= 10) cfg->repeat = val;
            } else if (!strcmp(key, "continuous")) {
                cfg->continuous = (val != 0);
            } else if (!strcmp(key, "tab_detect") || !strcmp(key, "detect_tab")) {
                cfg->tab_detect = (val != 0);
            } else if (!strcmp(key, "card_y") || !strcmp(key, "y_pos") || !strcmp(key, "y_shift")) {
                if (val >= 0 && val <= 600) cfg->card_y = val;
            } else if (!strcmp(key, "debug")) {
                cfg->debug = (val != 0);
            }
        } else if (sscanf(p, "%d", &val) == 1) {
            if (val >= 100 && val <= 500000) cfg->interval_us = (uint32_t)val;
        }
    }
    fclose(f);
}

int mpc_fb_tab_detect_enabled(void) {
    return g_last_cfg.tab_detect;
}

int mpc_fb_get_card_y(void) {
    return g_last_cfg.card_y;
}

/* =========================================================================
 * Top-Left 4-Pixel Tab Tag Barcode Reader (Screen X=0, Y=110)
 * Encodes ASCII tab name in 4 pixels: (4 pixels x 3 bytes RGB = 12 characters)
 * In native hardware BGRA framebuffer: b[2]=Red, b[1]=Green, b[0]=Blue
 * ========================================================================= */

static inline int color_match(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, int tol) {
    return (abs((int)r1 - (int)r2) <= tol &&
            abs((int)g1 - (int)g2) <= tol &&
            abs((int)b1 - (int)b2) <= tol);
}

static int g_last_tag_x = -1;
static int g_last_tag_y = -1;

int mpc_fb_read_tab_tag(char *out_tag, size_t max_len) {
    if (!out_tag || max_len == 0) return 0;
    out_tag[0] = '\0';
    if (s_num_fbs == 0) return 0;

    for (int i = 0; i < s_num_fbs; ++i) {
        uintptr_t base = s_fbs[i].start;
        uintptr_t end = s_fbs[i].end;
        size_t max_bytes = end - base;

        /* Target search: Read single 4-pixel row at X=0, Y in [108..112] (physical screen Y=110) */
        for (int y = 108; y <= 112; ++y) {
            for (int x = 0; x <= 4; x += 4) {
                size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
                if (offset + 16 > max_bytes) continue;

                const uint8_t *b = (const uint8_t*)(base + offset);

                /* Check BGRA / BGRX native hardware order: b[0]=B, b[1]=G, b[2]=R */
                char bgr_tag[13];
                bgr_tag[0]  = (char)b[2];  bgr_tag[1]  = (char)b[1];  bgr_tag[2]  = (char)b[0];
                bgr_tag[3]  = (char)b[6];  bgr_tag[4]  = (char)b[5];  bgr_tag[5]  = (char)b[4];
                bgr_tag[6]  = (char)b[10]; bgr_tag[7]  = (char)b[9];  bgr_tag[8]  = (char)b[8];
                bgr_tag[9]  = (char)b[14]; bgr_tag[10] = (char)b[13]; bgr_tag[11] = (char)b[12];
                bgr_tag[12] = '\0';

                /* Check RGBA / RGBX order fallback: b[0]=R, b[1]=G, b[2]=B */
                char rgb_tag[13];
                rgb_tag[0]  = (char)b[0];  rgb_tag[1]  = (char)b[1];  rgb_tag[2]  = (char)b[2];
                rgb_tag[3]  = (char)b[4];  rgb_tag[4]  = (char)b[5];  rgb_tag[5]  = (char)b[6];
                rgb_tag[6]  = (char)b[8];  rgb_tag[7]  = (char)b[9];  rgb_tag[8]  = (char)b[10];
                rgb_tag[9]  = (char)b[12]; rgb_tag[10] = (char)b[13]; rgb_tag[11] = (char)b[14];
                rgb_tag[12] = '\0';

                /* List of known synth tab names */
                static const char *KNOWN_TABS[] = {
                    "EQUALISER", "EQUALIZER", "EQ", "STEP", "LFO", "ROUTING",
                    "FILTER", "OSC", "MAIN", "PRESET", "ENV", "MOD", "FX"
                };

                /* Check BGR native hardware order first */
                for (size_t t = 0; t < sizeof(KNOWN_TABS)/sizeof(KNOWN_TABS[0]); ++t) {
                    if (!strncasecmp(bgr_tag, KNOWN_TABS[t], strlen(KNOWN_TABS[t]))) {
                        snprintf(out_tag, max_len, "%s", bgr_tag);
                        g_last_tag_x = x;
                        g_last_tag_y = y;
                        return 1;
                    }
                }

                /* Check RGB order fallback */
                for (size_t t = 0; t < sizeof(KNOWN_TABS)/sizeof(KNOWN_TABS[0]); ++t) {
                    if (!strncasecmp(rgb_tag, KNOWN_TABS[t], strlen(KNOWN_TABS[t]))) {
                        snprintf(out_tag, max_len, "%s", rgb_tag);
                        g_last_tag_x = x;
                        g_last_tag_y = y;
                        return 1;
                    }
                }

                /* Fuzzy Color Match (+/- 18 tolerance) for EQUALISER: 'E'(69), 'Q'(81), 'U'(85) */
                int match_bgr = color_match(b[2], b[1], b[0], 69, 81, 85, 18) &&
                                color_match(b[6], b[5], b[4], 65, 76, 73, 18);
                int match_rgb = color_match(b[0], b[1], b[2], 69, 81, 85, 18) &&
                                color_match(b[4], b[5], b[6], 65, 76, 73, 18);

                if (match_bgr || match_rgb) {
                    snprintf(out_tag, max_len, "EQUALISER");
                    g_last_tag_x = x;
                    g_last_tag_y = y;
                    return 1;
                }

                /* General ASCII tag fallback (prioritize BGR order) */
                if ((bgr_tag[0] >= 'A' && bgr_tag[0] <= 'Z') && (bgr_tag[1] >= 'A' && bgr_tag[1] <= 'Z')) {
                    snprintf(out_tag, max_len, "%s", bgr_tag);
                    g_last_tag_x = x;
                    g_last_tag_y = y;
                    return 1;
                }
                if ((rgb_tag[0] >= 'A' && rgb_tag[0] <= 'Z') && (rgb_tag[1] >= 'A' && rgb_tag[1] <= 'Z')) {
                    snprintf(out_tag, max_len, "%s", rgb_tag);
                    g_last_tag_x = x;
                    g_last_tag_y = y;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int mpc_fb_get_current_tab(char *out_tab, size_t max_len) {
    return mpc_fb_read_tab_tag(out_tab, max_len);
}

int mpc_fb_is_on_tab(const char *tab_name) {
    if (!g_last_cfg.tab_detect) return 1;
    if (!tab_name || !tab_name[0]) return 1;

    char current[16] = {0};
    if (!mpc_fb_read_tab_tag(current, sizeof(current))) {
        return 0;
    }
    return !strncasecmp(current, tab_name, strlen(tab_name));
}

static int s_eq_tab_state = 0;
static int s_miss_count = 0;
static int s_last_logged_state = -1;
static uint32_t s_log_tick = 0;

int mpc_fb_is_on_eq_tab(void) {
    if (!g_last_cfg.tab_detect) return 1;

    char tag[16] = {0};
    int has_tag = mpc_fb_read_tab_tag(tag, sizeof(tag));

    int is_equaliser = 0;
    if (has_tag) {
        if (!strncasecmp(tag, "EQUALISER", 9) || !strncasecmp(tag, "EQUALIZER", 9) ||
            !strncasecmp(tag, "EQ", 2)) {
            is_equaliser = 1;
        }
    }

    if (is_equaliser) {
        s_miss_count = 0;
        s_eq_tab_state = 1;
    } else {
        s_miss_count++;
        /* Debounce 2 frames to bridge host page flips */
        if (s_miss_count >= 2) {
            s_eq_tab_state = 0;
        }
    }

    s_log_tick++;
    if (g_last_cfg.debug && (s_eq_tab_state != s_last_logged_state || (s_log_tick % 60) == 0)) {
        s_last_logged_state = s_eq_tab_state;

        /* If tag not found, perform a diagnostic scan inside the VST viewport (Y=80..200) */
        int found_diag_x = -1, found_diag_y = -1;
        uint8_t diag_bytes[4] = {0};
        if (!has_tag && s_num_fbs > 0) {
            uintptr_t base = s_fbs[0].start;
            uintptr_t end = s_fbs[0].end;
            size_t max_bytes = end - base;
            for (int y = 80; y <= 200 && found_diag_x < 0; y += 1) {
                for (int x = 0; x < SCREEN_W - 4; x += 4) {
                    size_t off = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
                    if (off + 8 > max_bytes) continue;
                    const uint8_t *pb = (const uint8_t*)(base + off);
                    if ((color_match(pb[2], pb[1], pb[0], 69, 81, 85, 18) && color_match(pb[6], pb[5], pb[4], 65, 76, 73, 18)) ||
                        (color_match(pb[0], pb[1], pb[2], 69, 81, 85, 18) && color_match(pb[4], pb[5], pb[6], 65, 76, 73, 18))) {
                        found_diag_x = x;
                        found_diag_y = y;
                        diag_bytes[0] = pb[0]; diag_bytes[1] = pb[1]; diag_bytes[2] = pb[2]; diag_bytes[3] = pb[3];
                        break;
                    }
                }
            }
        }

        /* Sample raw bytes at a few representative screen coordinates */
        uint8_t b_0_86[4] = {0}, b_10_92[4] = {0};
        if (s_num_fbs > 0) {
            uintptr_t base = s_fbs[0].start;
            size_t off1 = (86 * SCREEN_STRIDE + 0) * 4;
            size_t off2 = (92 * SCREEN_STRIDE + 10) * 4;
            if (off1 + 4 <= s_fbs[0].end - base) memcpy(b_0_86, (void*)(base + off1), 4);
            if (off2 + 4 <= s_fbs[0].end - base) memcpy(b_10_92, (void*)(base + off2), 4);
        }

        char msg[256];
        if (has_tag) {
            snprintf(msg, sizeof(msg), "Tab Tag: '%s' (Screen X=%d, Y=%d) -> %s",
                     tag, g_last_tag_x, g_last_tag_y,
                     s_eq_tab_state ? "EQ ACTIVATED" : "EQ PAUSED");
        } else if (found_diag_x >= 0) {
            snprintf(msg, sizeof(msg), "Tab Tag: '<unknown>' -> EQ PAUSED | DIAG FOUND at Canvas (%d, %d)! bytes: [%d,%d,%d,%d]",
                     found_diag_x, found_diag_y,
                     diag_bytes[0], diag_bytes[1], diag_bytes[2], diag_bytes[3]);
        } else {
            snprintf(msg, sizeof(msg), "Tab Tag: '<unknown>' -> EQ PAUSED | Screen (0,86)=[%d,%d,%d] (10,92)=[%d,%d,%d]",
                     b_0_86[0], b_0_86[1], b_0_86[2],
                     b_10_92[0], b_10_92[1], b_10_92[2]);
        }
        log_fb(msg);
    }

    return s_eq_tab_state;
}

void mpc_fb_set_render_callback(mpc_fb_render_fn callback) {
    pthread_mutex_lock(&g_callback_lock);
    g_render_callback = callback;
    pthread_mutex_unlock(&g_callback_lock);
}

int mpc_fb_is_active(void) {
    return g_fb_running && (s_num_fbs > 0);
}

/* =========================================================================
 * 2D Framebuffer Drawing Primitives
 * ========================================================================= */

void mpc_fb_put_pixel(int x, int y, uint32_t color) {
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
    size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
    for (int i = 0; i < s_num_fbs; ++i) {
        uintptr_t addr = s_fbs[i].start + offset;
        if (addr + 4 <= s_fbs[i].end) {
            *(uint32_t*)addr = color;
        }
    }
}

void mpc_fb_blend_pixel(int x, int y, uint32_t color) {
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
    uint32_t sa = (color >> 24) & 0xFF;
    if (sa == 0) return;
    if (sa >= 255) {
        mpc_fb_put_pixel(x, y, color);
        return;
    }
    uint32_t sr = (color >> 16) & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = color & 0xFF;
    uint32_t da = 255 - sa;

    size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
    for (int i = 0; i < s_num_fbs; ++i) {
        uintptr_t addr = s_fbs[i].start + offset;
        if (addr + 4 <= s_fbs[i].end) {
            uint32_t dst = *(uint32_t*)addr;
            uint32_t dr = (dst >> 16) & 0xFF;
            uint32_t dg = (dst >> 8) & 0xFF;
            uint32_t db = dst & 0xFF;

            uint32_t r = (sr * sa + dr * da) >> 8;
            uint32_t g = (sg * sa + dg * da) >> 8;
            uint32_t b = (sb * sa + db * da) >> 8;

            *(uint32_t*)addr = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

uint32_t mpc_fb_get_pixel(int x, int y) {
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return 0;
    if (s_num_fbs == 0) return 0;
    size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
    uintptr_t addr = s_fbs[0].start + offset;
    if (addr + 4 <= s_fbs[0].end) {
        return *(uint32_t*)addr;
    }
    return 0;
}

#define GLYPH_CELL 9

static int font_glyph_index(char ch) {
    for (size_t i = 0; i < sizeof(font_chars) - 1; i++) {
        if (font_chars[i] == ch) return (int)i;
    }
    return 0; /* default space */
}

static int font_glyph_width(int idx) {
    static int cache[128];
    static char have[128];
    if (idx >= 0 && idx < 128 && have[idx]) return cache[idx];
    if (idx < 0 || (size_t)idx >= sizeof(font8x8)/sizeof(font8x8[0])) return 4;
    const uint8_t *g = font8x8[idx];
    int maxcol = -1;
    for (int row = 0; row < GLYPH_CELL; row++) {
        for (int col = 0; col < GLYPH_CELL; col++) {
            if (g[row * GLYPH_CELL + col] > 0 && col > maxcol) maxcol = col;
        }
    }
    int w = maxcol < 0 ? 4 : maxcol + 2;
    if (idx >= 0 && idx < 128) { cache[idx] = w; have[idx] = 1; }
    return w;
}

int mpc_fb_text_width(const char *str, float scale) {
    if (!str || scale <= 0.0f) return 0;
    float w = 0.0f;
    for (const char *p = str; *p; p++) {
        w += (float)font_glyph_width(font_glyph_index(*p)) * scale;
    }
    return (int)(w + 0.5f);
}

void mpc_fb_draw_text(int x, int y, const char *str, float scale, uint32_t color) {
    if (!str || scale <= 0.0f) return;
    int out_cell = (int)(GLYPH_CELL * scale + 0.5f);
    int cx = x;
    uint32_t base_a = (color >> 24) & 0xFF;
    if (base_a == 0) base_a = 0xFF;
    uint32_t rgb = color & 0x00FFFFFF;

    for (const char *p = str; *p; p++) {
        int idx = font_glyph_index(*p);
        const uint8_t *g = font8x8[idx];
        for (int dy = 0; dy < out_cell; dy++) {
            int row = (int)((float)dy / scale);
            if (row >= GLYPH_CELL) row = GLYPH_CELL - 1;
            for (int dx = 0; dx < out_cell; dx++) {
                int col = (int)((float)dx / scale);
                if (col >= GLYPH_CELL) col = GLYPH_CELL - 1;
                int cov = g[row * GLYPH_CELL + col];
                if (cov <= 0) continue;
                uint32_t a = (base_a * (uint32_t)cov) >> 8;
                if (a > 0) {
                    mpc_fb_blend_pixel(cx + dx, y + dy, (a << 24) | rgb);
                }
            }
        }
        cx += (int)((float)font_glyph_width(idx) * scale + 0.5f);
    }
}

void mpc_fb_draw_hline(int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= SCREEN_H || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCREEN_W) { w = SCREEN_W - x; }
    if (w <= 0) return;

    size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
    size_t byte_len = (size_t)w * 4;

    for (int i = 0; i < s_num_fbs; ++i) {
        uintptr_t addr = s_fbs[i].start + offset;
        if (addr + byte_len <= s_fbs[i].end) {
            uint32_t *p = (uint32_t*)addr;
            for (int k = 0; k < w; ++k) p[k] = color;
        }
    }
}

void mpc_fb_draw_vline(int x, int y, int h, uint32_t color) {
    if (x < 0 || x >= SCREEN_W || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCREEN_H) { h = SCREEN_H - y; }
    if (h <= 0) return;

    for (int row = y; row < y + h; ++row) {
        size_t offset = ((size_t)row * SCREEN_STRIDE + (size_t)x) * 4;
        for (int i = 0; i < s_num_fbs; ++i) {
            uintptr_t addr = s_fbs[i].start + offset;
            if (addr + 4 <= s_fbs[i].end) {
                *(uint32_t*)addr = color;
            }
        }
    }
}

void mpc_fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) { w = SCREEN_W - x; }
    if (y + h > SCREEN_H) { h = SCREEN_H - y; }
    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; ++row) {
        size_t offset = ((size_t)row * SCREEN_STRIDE + (size_t)x) * 4;
        size_t byte_len = (size_t)w * 4;
        for (int i = 0; i < s_num_fbs; ++i) {
            uintptr_t addr = s_fbs[i].start + offset;
            if (addr + byte_len <= s_fbs[i].end) {
                uint32_t *p = (uint32_t*)addr;
                for (int k = 0; k < w; ++k) p[k] = color;
            }
        }
    }
}

void mpc_fb_draw_rect(int x, int y, int w, int h, uint32_t color, int thickness) {
    if (thickness <= 0) thickness = 1;
    for (int t = 0; t < thickness; ++t) {
        mpc_fb_draw_hline(x + t, y + t, w - 2 * t, color);
        mpc_fb_draw_hline(x + t, y + h - 1 - t, w - 2 * t, color);
        mpc_fb_draw_vline(x + t, y + t, h - 2 * t, color);
        mpc_fb_draw_vline(x + w - 1 - t, y + t, h - 2 * t, color);
    }
}

void mpc_fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color, int thickness) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    int half_t = (thickness > 1) ? (thickness / 2) : 0;

    while (1) {
        if (half_t > 0) {
            for (int ty = -half_t; ty <= half_t; ++ty) {
                for (int tx = -half_t; tx <= half_t; ++tx) {
                    mpc_fb_put_pixel(x0 + tx, y0 + ty, color);
                }
            }
        } else {
            mpc_fb_put_pixel(x0, y0, color);
        }

        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void mpc_fb_draw_circle(int cx, int cy, int r, uint32_t color, int filled) {
    if (r <= 0) return;
    int x = r, y = 0;
    int err = 0;

    while (x >= y) {
        if (filled) {
            mpc_fb_draw_hline(cx - x, cy + y, 2 * x + 1, color);
            mpc_fb_draw_hline(cx - x, cy - y, 2 * x + 1, color);
            mpc_fb_draw_hline(cx - y, cy + x, 2 * y + 1, color);
            mpc_fb_draw_hline(cx - y, cy - x, 2 * y + 1, color);
        } else {
            mpc_fb_put_pixel(cx + x, cy + y, color);
            mpc_fb_put_pixel(cx + y, cy + x, color);
            mpc_fb_put_pixel(cx - y, cy + x, color);
            mpc_fb_put_pixel(cx - x, cy + y, color);
            mpc_fb_put_pixel(cx - x, cy - y, color);
            mpc_fb_put_pixel(cx - y, cy - x, color);
            mpc_fb_put_pixel(cx + y, cy - x, color);
            mpc_fb_put_pixel(cx + x, cy - y, color);
        }
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void mpc_fb_draw_curve(const int *curve_y, int x_start, int x_end, uint32_t color, int thickness) {
    if (!curve_y || x_start >= x_end) return;

    for (int x = x_start; x <= x_end; ++x) {
        int cy = curve_y[x - x_start];
        for (int t = -(thickness/2); t <= thickness/2; ++t) {
            mpc_fb_put_pixel(x, cy + t, color);
        }
        if (x > x_start) {
            int prev = curve_y[x - x_start - 1];
            int step = (cy > prev) ? 1 : -1;
            for (int sy = prev + step; sy != cy; sy += step) {
                mpc_fb_put_pixel(x, sy, color);
            }
        }
    }
}

void mpc_fb_fill_curve_area(const int *curve_y, int x_start, int x_end, int y_baseline, uint32_t fill_color) {
    if (!curve_y || x_start >= x_end) return;

    for (int x = x_start; x <= x_end; ++x) {
        int cy = curve_y[x - x_start];
        if (cy < y_baseline) {
            for (int y = cy + 1; y < y_baseline; ++y) {
                mpc_fb_blend_pixel(x, y, fill_color);
            }
        } else if (cy > y_baseline) {
            for (int y = y_baseline + 1; y < cy; ++y) {
                mpc_fb_blend_pixel(x, y, fill_color);
            }
        }
    }
}

void mpc_fb_blit(int dst_x, int dst_y, int w, int h, const uint32_t *src_pixels, int src_stride) {
    if (!src_pixels || w <= 0 || h <= 0 || s_num_fbs == 0) return;

    for (int row = 0; row < h; ++row) {
        int screen_y = dst_y + row;
        if (screen_y < 0 || screen_y >= SCREEN_H) continue;

        int screen_x = dst_x;
        int copy_w = w;
        int src_offset_x = 0;

        if (screen_x < 0) {
            src_offset_x = -screen_x;
            copy_w += screen_x;
            screen_x = 0;
        }
        if (screen_x + copy_w > SCREEN_W) {
            copy_w = SCREEN_W - screen_x;
        }
        if (copy_w <= 0) continue;

        size_t fb_row_offset = ((size_t)screen_y * SCREEN_STRIDE + (size_t)screen_x) * 4;
        const uint32_t *src_row = src_pixels + (size_t)row * src_stride + src_offset_x;
        size_t byte_len = (size_t)copy_w * 4;

        for (int i = 0; i < s_num_fbs; ++i) {
            uintptr_t addr = s_fbs[i].start + fb_row_offset;
            if (addr + byte_len <= s_fbs[i].end) {
                memcpy((void*)addr, src_row, byte_len);
            }
        }
    }
}

/* =========================================================================
 * Background Framebuffer Worker Thread
 * ========================================================================= */

static void* fb_worker_thread(void *arg) {
    (void)arg;

    log_fb("mpc_framebuffer library started");

    char plugin_dir[512] = {0};
    if (mpc_plugin_dir(plugin_dir, sizeof(plugin_dir))) {
        char dir_msg[540];
        snprintf(dir_msg, sizeof(dir_msg), "Detected plugin dir: %s", plugin_dir);
        log_fb(dir_msg);
    }

    fb_timing_cfg_t timing_cfg = {
        .interval_us = 16666,
        .offset_us = 0,
        .repeat = 1,
        .continuous = 0,
        .debug = 1
    };
    read_timing_config(&timing_cfg, plugin_dir);

    // Scan /proc/self/maps for display framebuffer mappings
    s_num_fbs = 0;
    FILE *maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (fgets(line, sizeof(line), maps) && s_num_fbs < MAX_FB_MAPS) {
            if (strstr(line, "/dev/dri") || strstr(line, "/dev/fb")) {
                unsigned long s = 0, e = 0;
                char perms[16] = {0};
                if (sscanf(line, "%lx-%lx %15s", &s, &e, perms) >= 3) {
                    if (perms[0] == 'r' && perms[1] == 'w') {
                        size_t len = e - s;
                        if (len >= 1024 * 1024) { // Minimum 1MB for a display buffer
                            s_fbs[s_num_fbs].start = (uintptr_t)s;
                            s_fbs[s_num_fbs].end = (uintptr_t)e;
                            s_fbs[s_num_fbs].size = len;
                            char *p = strchr(line, '/');
                            if (p) {
                                p[strcspn(p, "\r\n")] = 0;
                                snprintf(s_fbs[s_num_fbs].name, sizeof(s_fbs[s_num_fbs].name), "%s", p);
                            } else {
                                snprintf(s_fbs[s_num_fbs].name, sizeof(s_fbs[s_num_fbs].name), "display");
                            }

                            char map_msg[300];
                            snprintf(map_msg, sizeof(map_msg), "Registered display buffer #%d: %s (%zu KB, %lx-%lx)",
                                     s_num_fbs, s_fbs[s_num_fbs].name, len / 1024, s, e);
                            log_fb(map_msg);

                            s_num_fbs++;
                        }
                    }
                }
            }
        }
        fclose(maps);
    }

    if (s_num_fbs == 0) {
        log_fb("WARNING: No /dev/dri or /dev/fb mappings found >= 1MB in /proc/self/maps");
    } else {
        char summary[128];
        snprintf(summary, sizeof(summary), "Total active display buffers: %d", s_num_fbs);
        log_fb(summary);
    }

    uint32_t frame_count = 0;

    while (g_fb_running) {
        // Hot-reload timing config every 30 frames (~500ms)
        if ((frame_count % 30) == 0) {
            read_timing_config(&timing_cfg, plugin_dir);
            g_last_cfg = timing_cfg;
        }

        if (s_num_fbs > 0) {
            if (timing_cfg.offset_us > 0) {
                usleep(timing_cfg.offset_us);
            }

            for (int r = 0; r < timing_cfg.repeat; ++r) {
                pthread_mutex_lock(&g_callback_lock);
                mpc_fb_render_fn cb = g_render_callback;
                pthread_mutex_unlock(&g_callback_lock);

                if (cb) {
                    cb(); // Call user UI render function
                }

                if (r + 1 < timing_cfg.repeat) {
                    usleep(500);
                }
            }
        }

        frame_count++;

        if (timing_cfg.continuous) {
            usleep(1000); // 1ms continuous loop
        } else {
            usleep(timing_cfg.interval_us);
        }
    }

    log_fb("mpc_framebuffer library exiting");
    return NULL;
}

int mpc_fb_init(void) {
    if (g_fb_running) return 0;
    g_fb_running = 1;
    pthread_create(&g_fb_thread, NULL, fb_worker_thread, NULL);
    return 1;
}

void mpc_fb_cleanup(void) {
    if (!g_fb_running) return;
    g_fb_running = 0;
    pthread_join(g_fb_thread, NULL);
}
