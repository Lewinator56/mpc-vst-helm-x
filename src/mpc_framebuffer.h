#pragma once
#ifndef MPC_FRAMEBUFFER_H
#define MPC_FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPC_SCREEN_W 1280
#define MPC_SCREEN_H 800

/* ---- Library Initialization & Lifecycle ---- */
int  mpc_fb_init(void);
void mpc_fb_cleanup(void);

/* Render callback function type */
typedef void (*mpc_fb_render_fn)(void);
void mpc_fb_set_render_callback(mpc_fb_render_fn callback);

/* Frame control */
int  mpc_fb_is_active(void);
int  mpc_fb_tab_detect_enabled(void);
int  mpc_fb_get_card_y(void);
void mpc_fb_begin_frame(void);
void mpc_fb_end_frame(void);

/* ---- 2D Drawing Primitives (0..1279, 0..799 screen coordinates) ---- */

/* Single Pixel */
void     mpc_fb_put_pixel(int x, int y, uint32_t argb);
void     mpc_fb_blend_pixel(int x, int y, uint32_t argb);
uint32_t mpc_fb_get_pixel(int x, int y);

/* Text Rendering (proportional font) */
int  mpc_fb_text_width(const char *str, float scale);
void mpc_fb_draw_text(int x, int y, const char *str, float scale, uint32_t color);

/* Lines */
void mpc_fb_draw_hline(int x, int y, int w, uint32_t color);
void mpc_fb_draw_vline(int x, int y, int h, uint32_t color);
void mpc_fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color, int thickness);

/* Rectangles */
void mpc_fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void mpc_fb_draw_rect(int x, int y, int w, int h, uint32_t color, int thickness);

/* Circles */
void mpc_fb_draw_circle(int cx, int cy, int radius, uint32_t color, int filled);

/* Curves & Area Fills */
void mpc_fb_draw_curve(const int *curve_y, int x_start, int x_end, uint32_t color, int thickness);
void mpc_fb_fill_curve_area(const int *curve_y, int x_start, int x_end, int y_baseline, uint32_t fill_color);

/* Block Transfer / Fast Buffer Blit */
void mpc_fb_blit(int dst_x, int dst_y, int w, int h, const uint32_t *src_pixels, int src_stride);

/* ---- Backward-compatible state forwarders (routed to EQ UI) ---- */
typedef struct {
    int on;
    int shelf;       /* 0 = Shelf, 1 = Bell */
    float frequency; /* Hz */
    float gain_db;   /* dB (-15.0 .. +15.0) */
    float q;         /* Q factor (0.2 .. 8.0) */
} mpc_eq_band_t;

typedef struct {
    int master_on;
    mpc_eq_band_t bands[5];
} mpc_eq_state_t;

void mpc_fb_set_eq_state(const mpc_eq_state_t *state);
void mpc_fb_set_band(int band_idx, int on, int shelf, float freq, float gain, float q);
void mpc_fb_set_master_on(int on);

#ifdef __cplusplus
}
#endif

#endif /* MPC_FRAMEBUFFER_H */
