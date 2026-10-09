#define _GNU_SOURCE
#include "mpc_eq_ui.h"
#include "mpc_fb.h"
#include "font8x8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>

/* Screen coordinates for the EQ canvas card */
#define EQ_CARD_X 11
#define EQ_CARD_Y 112  /* Shifted down 20px (was 92) */
#define EQ_CARD_W 1154
#define EQ_CARD_H 240  /* 112 + 224 = 336, stops 2px above bottom frames at Y=338 */

/* Oscilloscope display viewport inside the card (local buffer coordinates) */
#define PLOT_X_MIN 48
#define PLOT_X_MAX 1134
#define PLOT_W (PLOT_X_MAX - PLOT_X_MIN + 1) /* 1087 px */

#define PLOT_Y_MIN 30    /* +15 dB */
#define PLOT_Y_MAX 210   /* -15 dB */
#define PLOT_Y_CENTER 120 /* 0 dB */
#define PLOT_H (PLOT_Y_MAX - PLOT_Y_MIN + 1) /* 181 px */
#define PX_PER_DB 6.0f                       /* 90 px / 15 dB */

/* Theme Colors matching MPC dark aesthetic */
#define COLOR_CARD_BG      0xFF101216  /* Deep dark plate background */
#define COLOR_CARD_BORDER  0xFF20242D  /* Outer plate outline */
#define COLOR_SCOPE_BG     0xFF0C0E12  /* Recessed dark oscilloscope screen */
#define COLOR_SCOPE_BORDER 0xFF262D3A  /* Scope screen border */
#define COLOR_GRID_MAJOR   0xFF1B222E  /* Major frequency octave line */
#define COLOR_GRID_MINOR   0xFF131720  /* Minor frequency tick line */
#define COLOR_DB_LINE      0xFF161C26  /* Horizontal dB grid line */
#define COLOR_DB_ZERO      0xFF2D3C50  /* Prominent 0 dB center baseline */
#define COLOR_TEXT_DIM     0xFF4B5A6E  /* Frequency & dB grid text */
#define COLOR_TEXT_BOLD    0xFF6E87A5  /* Highlight text ("0 dB", headers) */
#define COLOR_TITLE        0xFF556982  /* Title text color */

static pthread_mutex_t g_eq_lock = PTHREAD_MUTEX_INITIALIZER;

static mpc_eq_state_t g_eq_state = {
    .master_on = 1,
    .bands = {
        { .on = 1, .shelf = 0, .frequency = 80.0f,    .gain_db = 0.0f, .q = 0.707f },
        { .on = 1, .shelf = 1, .frequency = 250.0f,   .gain_db = 0.0f, .q = 1.0f   },
        { .on = 1, .shelf = 1, .frequency = 1000.0f,  .gain_db = 0.0f, .q = 1.0f   },
        { .on = 1, .shelf = 1, .frequency = 3500.0f,  .gain_db = 0.0f, .q = 1.0f   },
        { .on = 1, .shelf = 0, .frequency = 10000.0f, .gain_db = 0.0f, .q = 0.707f }
    }
};

static float g_freq_lut[PLOT_W];
static int g_freq_lut_inited = 0;

static void init_freq_lut(void) {
    if (g_freq_lut_inited) return;
    for (int i = 0; i < PLOT_W; ++i) {
        float t = (float)i / (float)(PLOT_W - 1);
        g_freq_lut[i] = 20.0f * powf(1000.0f, t); /* 20 Hz .. 20 kHz */
    }
    g_freq_lut_inited = 1;
}

static inline int freq_to_local_x(float f) {
    if (f < 20.0f) f = 20.0f;
    if (f > 20000.0f) f = 20000.0f;
    float t = log10f(f / 20.0f) / 3.0f;
    return PLOT_X_MIN + (int)lroundf(t * (float)(PLOT_W - 1));
}

void mpc_eq_ui_set_state(const mpc_eq_state_t *state) {
    if (!state) return;
    pthread_mutex_lock(&g_eq_lock);
    g_eq_state = *state;
    pthread_mutex_unlock(&g_eq_lock);
}

void mpc_eq_ui_set_master_on(int on) {
    pthread_mutex_lock(&g_eq_lock);
    g_eq_state.master_on = on;
    pthread_mutex_unlock(&g_eq_lock);
}

void mpc_eq_ui_set_band(int b, int on, int shelf, float freq, float gain, float q) {
    if (b < 0 || b >= 5) return;
    pthread_mutex_lock(&g_eq_lock);
    g_eq_state.bands[b].on = on;
    g_eq_state.bands[b].shelf = shelf;
    g_eq_state.bands[b].frequency = freq;
    g_eq_state.bands[b].gain_db = gain;
    g_eq_state.bands[b].q = q;
    pthread_mutex_unlock(&g_eq_lock);
}

__attribute__((unused))
static float eval_band_db(const mpc_eq_band_t *b, float f, float sample_rate) {
    if (!b->on || fabsf(b->gain_db) < 0.01f) return 0.0f;

    float f0 = b->frequency;
    if (f0 < 10.0f) f0 = 10.0f;
    if (f0 > sample_rate * 0.49f) f0 = sample_rate * 0.49f;

    float gain_db = b->gain_db;
    float Q = b->q > 0.1f ? b->q : 0.1f;

    if (b->shelf == 0) {
        float A = powf(10.0f, gain_db / 40.0f);
        float w = f / f0;
        if (f0 <= 1000.0f) {
            float w2 = w * w;
            float mag2 = (A*A + w2) / (1.0f + w2);
            return 10.0f * log10f(mag2 > 1e-6f ? mag2 : 1e-6f);
        } else {
            float w2 = w * w;
            float mag2 = (1.0f + (A*A) * w2) / (1.0f + w2);
            return 10.0f * log10f(mag2 > 1e-6f ? mag2 : 1e-6f);
        }
    } else {
        float ratio = f / f0;
        float x = (ratio - 1.0f / ratio) * Q;
        float x2 = x * x;
        float V = powf(10.0f, fabsf(gain_db) / 20.0f);
        float mag2;
        if (gain_db >= 0.0f) {
            mag2 = (1.0f + (V * V) * x2) / (1.0f + x2);
            return 10.0f * log10f(mag2 > 1e-6f ? mag2 : 1e-6f);
        } else {
            mag2 = (1.0f + x2) / (1.0f + (V * V) * x2);
            return 10.0f * log10f(mag2 > 1e-6f ? mag2 : 1e-6f);
        }
    }
}

/* ---- Private Offscreen RAM Buffer & Fast Drawing Primitives ---- */
static uint32_t s_eq_buffer[EQ_CARD_W * EQ_CARD_H];

static inline void buf_put_pixel(int x, int y, uint32_t argb) {
    if ((unsigned)x >= (unsigned)EQ_CARD_W || (unsigned)y >= (unsigned)EQ_CARD_H) return;
    s_eq_buffer[y * EQ_CARD_W + x] = argb;
}

static inline void buf_blend_pixel(int x, int y, uint32_t argb) {
    if ((unsigned)x >= (unsigned)EQ_CARD_W || (unsigned)y >= (unsigned)EQ_CARD_H) return;
    uint32_t sa = (argb >> 24) & 0xFF;
    if (sa == 0) return;
    if (sa == 255) {
        s_eq_buffer[y * EQ_CARD_W + x] = argb;
        return;
    }
    uint32_t dst = s_eq_buffer[y * EQ_CARD_W + x];
    uint32_t inv_sa = 255 - sa;
    uint32_t sr = (argb >> 16) & 0xFF;
    uint32_t sg = (argb >> 8)  & 0xFF;
    uint32_t sb = argb & 0xFF;
    uint32_t dr = (dst >> 16)  & 0xFF;
    uint32_t dg = (dst >> 8)   & 0xFF;
    uint32_t db = dst & 0xFF;
    uint32_t out_r = (sr * sa + dr * inv_sa) >> 8;
    uint32_t out_g = (sg * sa + dg * inv_sa) >> 8;
    uint32_t out_b = (sb * sa + db * inv_sa) >> 8;
    s_eq_buffer[y * EQ_CARD_W + x] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
}

static void buf_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > EQ_CARD_W) w = EQ_CARD_W - x;
    if (y + h > EQ_CARD_H) h = EQ_CARD_H - y;
    if (w <= 0 || h <= 0) return;

    for (int r = y; r < y + h; ++r) {
        uint32_t *p = &s_eq_buffer[r * EQ_CARD_W + x];
        for (int c = 0; c < w; ++c) p[c] = color;
    }
}

static void buf_draw_rect(int x, int y, int w, int h, uint32_t color, int thickness) {
    buf_fill_rect(x, y, w, thickness, color);
    buf_fill_rect(x, y + h - thickness, w, thickness, color);
    buf_fill_rect(x, y, thickness, h, color);
    buf_fill_rect(x + w - thickness, y, thickness, h, color);
}

__attribute__((unused))
static void buf_draw_hline(int x, int y, int w, uint32_t color) {
    buf_fill_rect(x, y, w, 1, color);
}

__attribute__((unused))
static void buf_draw_vline(int x, int y, int h, uint32_t color) {
    buf_fill_rect(x, y, 1, h, color);
}

__attribute__((unused))
static void buf_draw_circle(int cx, int cy, int radius, uint32_t color, int filled) {
    if (radius <= 0) { buf_put_pixel(cx, cy, color); return; }
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (filled) {
                if (d2 <= r2) buf_put_pixel(cx + dx, cy + dy, color);
            } else {
                if (d2 <= r2 && d2 >= (radius - 1) * (radius - 1)) {
                    buf_put_pixel(cx + dx, cy + dy, color);
                }
            }
        }
    }
}

static void buf_draw_curve(const int *curve_y, int x_start, int x_end, uint32_t color, int thickness) {
    if (!curve_y || x_start >= x_end) return;
    for (int x = x_start; x <= x_end; ++x) {
        int cy = curve_y[x - x_start];
        for (int t = -(thickness / 2); t <= thickness / 2; ++t) {
            buf_put_pixel(x, cy + t, color);
        }
        if (x > x_start) {
            int prev = curve_y[x - x_start - 1];
            if (prev != cy) {
                int step = (cy > prev) ? 1 : -1;
                for (int sy = prev + step; sy != cy; sy += step) {
                    buf_put_pixel(x, sy, color);
                }
            }
        }
    }
}

__attribute__((unused))
static void buf_fill_curve_area(const int *curve_y, int x_start, int x_end, int y_baseline, uint32_t fill_color) {
    if (!curve_y || x_start >= x_end) return;
    for (int x = x_start; x <= x_end; ++x) {
        int cy = curve_y[x - x_start];
        if (cy < y_baseline) {
            for (int y = cy + 1; y < y_baseline; ++y) buf_blend_pixel(x, y, fill_color);
        } else if (cy > y_baseline) {
            for (int y = y_baseline + 1; y < cy; ++y) buf_blend_pixel(x, y, fill_color);
        }
    }
}

__attribute__((unused))
static int buf_glyph_index(char ch) {
    for (size_t i = 0; i < sizeof(font_chars) - 1; i++) {
        if (font_chars[i] == ch) return (int)i;
    }
    return 0;
}

__attribute__((unused))
static int buf_glyph_width(int idx) {
    if (idx < 0 || (size_t)idx >= sizeof(font8x8)/sizeof(font8x8[0])) return 4;
    const uint8_t *g = font8x8[idx];
    int maxcol = -1;
    for (int row = 0; row < 9; row++) {
        for (int col = 0; col < 9; col++) {
            if (g[row * 9 + col] > 0 && col > maxcol) maxcol = col;
        }
    }
    return maxcol < 0 ? 4 : maxcol + 2;
}

__attribute__((unused))
static int buf_text_width(const char *str, float scale) {
    if (!str || scale <= 0.0f) return 0;
    float w = 0.0f;
    for (const char *p = str; *p; p++) {
        w += (float)buf_glyph_width(buf_glyph_index(*p)) * scale;
    }
    return (int)(w + 0.5f);
}

__attribute__((unused))
static void buf_draw_text(int x, int y, const char *str, float scale, uint32_t color) {
    if (!str || scale <= 0.0f) return;
    int out_cell = (int)(9 * scale + 0.5f);
    int cx = x;
    uint32_t base_a = (color >> 24) & 0xFF;
    if (base_a == 0) base_a = 0xFF;
    uint32_t rgb = color & 0x00FFFFFF;

    for (const char *p = str; *p; p++) {
        int idx = buf_glyph_index(*p);
        const uint8_t *g = font8x8[idx];
        for (int dy = 0; dy < out_cell; dy++) {
            int row = (int)((float)dy / scale);
            if (row >= 9) row = 8;
            for (int dx = 0; dx < out_cell; dx++) {
                int col = (int)((float)dx / scale);
                if (col >= 9) col = 8;
                int cov = g[row * 9 + col];
                if (cov <= 0) continue;
                uint32_t a = (base_a * (uint32_t)cov) >> 8;
                if (a > 0) buf_blend_pixel(cx + dx, y + dy, (a << 24) | rgb);
            }
        }
        cx += (int)((float)buf_glyph_width(idx) * scale + 0.5f);
    }
}

void mpc_eq_ui_render(void) {
    if (!mpc_fb_is_on_eq_tab()) {
        return;
    }

    init_freq_lut();

    mpc_eq_state_t eq;
    pthread_mutex_lock(&g_eq_lock);
    eq = g_eq_state;
    pthread_mutex_unlock(&g_eq_lock);

    // 1. Draw exclusively into our private offscreen RAM buffer (zero screen bus traffic)
    // Outer EQ Card area (x=11, y=92, w=1154, h=240): dark background + subtle border
    buf_fill_rect(0, 0, EQ_CARD_W, EQ_CARD_H, 0xFF14161A);
    buf_draw_rect(0, 0, EQ_CARD_W, EQ_CARD_H, 0xFF2A2D34, 1);

    // Inner plot screen container (x=59, y=122 on screen -> local x=48, y=30, w=1086, h=181)
    int px = 48;
    int py = 30;
    int pw = 1086;
    int ph = 181;
    buf_fill_rect(px, py, pw, ph, 0xFF003344);
    buf_draw_rect(px, py, pw, ph, 0xFF00E5FF, 3);

    // Center 0 dB reference line inside plot screen
    buf_fill_rect(px, py + 90, pw, 1, 0xFF305060);

    // 5-Band Response Curve Computation (Bands 1 to 5)
    int curve_y[PLOT_W];
    float total_dbs[PLOT_W];
    memset(total_dbs, 0, sizeof(total_dbs));

    if (eq.master_on) {
        for (int b = 0; b < 5; ++b) {
            if (!eq.bands[b].on) continue;
            float g = eq.bands[b].gain_db;
            if (fabsf(g) < 0.05f) continue;

            float f0 = eq.bands[b].frequency;
            if (f0 < 10.0f) f0 = 10.0f;
            if (f0 > 20000.0f) f0 = 20000.0f;
            int shelf = eq.bands[b].shelf;
            float q = eq.bands[b].q > 0.1f ? eq.bands[b].q : 0.1f;

            if (shelf == 0 && b == 0) {
                // Low Shelf (Band 1 when Shelf selected)
                for (int i = 0; i < PLOT_W; ++i) {
                    float f = g_freq_lut[i];
                    float w = f / f0;
                    total_dbs[i] += g / (1.0f + w * w);
                }
            } else if (shelf == 0 && b == 4) {
                // High Shelf (Band 5 when Shelf selected)
                for (int i = 0; i < PLOT_W; ++i) {
                    float f = g_freq_lut[i];
                    float w = f / f0;
                    float w2 = w * w;
                    total_dbs[i] += (g * w2) / (1.0f + w2);
                }
            } else {
                // Peaking Bell filter (Bands 1..5 when Bell selected)
                for (int i = 0; i < PLOT_W; ++i) {
                    float f = g_freq_lut[i];
                    float ratio = f / f0;
                    float x = (ratio - 1.0f / ratio) * q;
                    total_dbs[i] += g / (1.0f + x * x);
                }
            }
        }
    }

    // Convert total dB to pixel Y coordinate
    for (int i = 0; i < PLOT_W; ++i) {
        float db = total_dbs[i];
        if (db > 15.0f) db = 15.0f;
        if (db < -15.0f) db = -15.0f;

        int cy = PLOT_Y_CENTER - (int)lroundf(db * PX_PER_DB);
        if (cy < PLOT_Y_MIN + 1) cy = PLOT_Y_MIN + 1;
        if (cy > PLOT_Y_MAX - 1) cy = PLOT_Y_MAX - 1;
        curve_y[i] = cy;
    }

    // Draw the 5-band response curve line (vibrant cyan 0xFF00E5FF, 2px thick)
    buf_draw_curve(curve_y, PLOT_X_MIN, PLOT_X_MAX, 0xFF00E5FF, 2);

    // 5 Band interactive node handles (Color coded 1 to 5)
    static const uint32_t BAND_COLORS[5] = {
        0xFFFF5252, // Band 1: Coral Red
        0xFFFFAB00, // Band 2: Warm Amber
        0xFFFFD600, // Band 3: Gold Yellow
        0xFF00E676, // Band 4: Emerald Green
        0xFF00E5FF  // Band 5: Electric Cyan
    };

    if (eq.master_on) {
        for (int b = 0; b < 5; ++b) {
            if (!eq.bands[b].on) continue;
            float fb = eq.bands[b].frequency;
            int nx = freq_to_local_x(fb);

            float g = eq.bands[b].gain_db;
            if (g > 15.0f) g = 15.0f;
            if (g < -15.0f) g = -15.0f;
            int ny = PLOT_Y_CENTER - (int)lroundf(g * PX_PER_DB);

            if (nx >= PLOT_X_MIN && nx <= PLOT_X_MAX && ny >= PLOT_Y_MIN && ny <= PLOT_Y_MAX) {
                buf_draw_circle(nx, ny, 5, BAND_COLORS[b], 1);
                buf_draw_circle(nx, ny, 2, 0xFFFFFFFF, 1);
            }
        }
    }

    // 2. Atomically blit the finished buffer to the framebuffer in a single burst (<150us)
    int card_y = mpc_fb_get_card_y();
    mpc_fb_blit(EQ_CARD_X, card_y, EQ_CARD_W, EQ_CARD_H, s_eq_buffer, EQ_CARD_W);
}

void mpc_eq_ui_init(void) {
    init_freq_lut();
    mpc_fb_set_render_callback(mpc_eq_ui_render);
}

/* Auto-register with framebuffer on startup */
__attribute__((constructor))
static void auto_init_eq_ui(void) {
    mpc_eq_ui_init();
}
