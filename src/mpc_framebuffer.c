#define _GNU_SOURCE
#include "mpc_framebuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include "../wrapper/plugin_dir.h"
#include "font8x8.h"

#define SCREEN_W 1280
#define SCREEN_H 800
#define SCREEN_STRIDE 1280

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
    int tab_detect;       /* 1 = only render when on EQ tab (default 1) */
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
    cfg->tab_detect = 1; /* Default to checking EQ tab */
    cfg->card_y = 112;   /* Default to Y=112 (shifted down 20px from 92) */
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
    return g_last_cfg.card_y > 0 ? g_last_cfg.card_y : 112;
}

void mpc_fb_set_render_callback(mpc_fb_render_fn callback) {
    pthread_mutex_lock(&g_callback_lock);
    g_render_callback = callback;
    pthread_mutex_unlock(&g_callback_lock);
}

int mpc_fb_is_active(void) {
    return g_fb_running && (s_num_fbs > 0);
}

void mpc_fb_begin_frame(void) {
    // Reserved for sync or buffer flips if needed
}

void mpc_fb_end_frame(void) {
    // Reserved for flush or cache sync if needed
}

/* =========================================================================
 * 2D Graphics Drawing Primitives (Direct Screen Coordinates)
 * ========================================================================= */

void mpc_fb_put_pixel(int x, int y, uint32_t argb) {
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
    size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
    for (int i = 0; i < s_num_fbs; ++i) {
        uintptr_t addr = s_fbs[i].start + offset;
        if (addr + 4 <= s_fbs[i].end) {
            *(uint32_t*)addr = argb;
        }
    }
}

void mpc_fb_blend_pixel(int x, int y, uint32_t argb) {
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
    uint32_t sa = (argb >> 24) & 0xFF;
    if (sa == 0) return;
    if (sa == 255) {
        mpc_fb_put_pixel(x, y, argb);
        return;
    }

    uint32_t sr = (argb >> 16) & 0xFF;
    uint32_t sg = (argb >> 8)  & 0xFF;
    uint32_t sb = (argb >> 0)  & 0xFF;
    uint32_t inv_sa = 255 - sa;

    size_t offset = ((size_t)y * SCREEN_STRIDE + (size_t)x) * 4;
    for (int i = 0; i < s_num_fbs; ++i) {
        uintptr_t addr = s_fbs[i].start + offset;
        if (addr + 4 <= s_fbs[i].end) {
            uint32_t dst = *(uint32_t*)addr;
            uint32_t dr = (dst >> 16) & 0xFF;
            uint32_t dg = (dst >> 8)  & 0xFF;
            uint32_t db = (dst >> 0)  & 0xFF;

            uint32_t out_r = (sr * sa + dr * inv_sa) >> 8;
            uint32_t out_g = (sg * sa + dg * inv_sa) >> 8;
            uint32_t out_b = (sb * sa + db * inv_sa) >> 8;

            *(uint32_t*)addr = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
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

void mpc_fb_draw_circle(int cx, int cy, int radius, uint32_t color, int filled) {
    if (radius <= 0) return;
    if (filled) {
        for (int dy = -radius; dy <= radius; ++dy) {
            int dx = (int)lroundf(sqrtf((float)(radius * radius - dy * dy)));
            mpc_fb_draw_hline(cx - dx, cy + dy, 2 * dx + 1, color);
        }
    } else {
        int x = radius, y = 0;
        int err = 0;
        while (x >= y) {
            mpc_fb_put_pixel(cx + x, cy + y, color);
            mpc_fb_put_pixel(cx + y, cy + x, color);
            mpc_fb_put_pixel(cx - y, cy + x, color);
            mpc_fb_put_pixel(cx - x, cy + y, color);
            mpc_fb_put_pixel(cx - x, cy - y, color);
            mpc_fb_put_pixel(cx - y, cy - x, color);
            mpc_fb_put_pixel(cx + y, cy - x, color);
            mpc_fb_put_pixel(cx + x, cy - y, color);
            if (err <= 0) { y += 1; err += 2*y + 1; }
            if (err > 0)  { x -= 1; err -= 2*x + 1; }
        }
    }
}

void mpc_fb_draw_curve(const int *curve_y, int x_start, int x_end, uint32_t color, int thickness) {
    if (!curve_y || x_start >= x_end) return;
    if (thickness < 1) thickness = 1;

    for (int x = x_start; x <= x_end; ++x) {
        int cy = curve_y[x - x_start];
        for (int t = -(thickness / 2); t <= thickness / 2; ++t) {
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
            // Boost fill: from cy + 1 down to y_baseline - 1
            for (int y = cy + 1; y < y_baseline; ++y) {
                mpc_fb_blend_pixel(x, y, fill_color);
            }
        } else if (cy > y_baseline) {
            // Cut fill: from y_baseline + 1 down to cy - 1
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

/* =========================================================================
 * Backward-Compatible State Forwarders (Delegated to mpc_eq_ui)
 * ========================================================================= */

extern void mpc_eq_ui_set_state(const mpc_eq_state_t *state);
extern void mpc_eq_ui_set_band(int b, int on, int shelf, float freq, float gain, float q);
extern void mpc_eq_ui_set_master_on(int on);

void mpc_fb_set_eq_state(const mpc_eq_state_t *state) {
    mpc_eq_ui_set_state(state);
}

void mpc_fb_set_master_on(int on) {
    mpc_eq_ui_set_master_on(on);
}

void mpc_fb_set_band(int b, int on, int shelf, float freq, float gain, float q) {
    mpc_eq_ui_set_band(b, on, shelf, freq, gain, q);
}
