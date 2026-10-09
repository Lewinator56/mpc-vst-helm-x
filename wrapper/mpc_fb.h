#ifndef MPC_FB_H
#define MPC_FB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Screen dimensions for Akai MPC Standalone (MPC Live, One, X, Key, Force) */
#define MPC_FB_WIDTH    1280
#define MPC_FB_HEIGHT   800
#define MPC_FB_STRIDE   1280

/* Canvas physical top coordinate (below the 110px system top bar & tabs) */
#define MPC_FB_CANVAS_Y 110

/* -------------------------------------------------------------------------
 * Framebuffer Engine Lifecycle & Thread Management
 * ------------------------------------------------------------------------- */

typedef void (*mpc_fb_render_fn)(void);

/* Initialize framebuffer mappings and 60fps render thread */
int  mpc_fb_init(void);

/* Clean up framebuffer resources and terminate thread */
void mpc_fb_cleanup(void);

/* Returns 1 if framebuffer is active and mapped */
int  mpc_fb_is_active(void);

/* Register custom UI render callback (called at 60 FPS by the background thread) */
void mpc_fb_set_render_callback(mpc_fb_render_fn callback);

/* -------------------------------------------------------------------------
 * Tab Detection Functions
 * ------------------------------------------------------------------------- */

/* Returns 1 if tab detection is enabled in fb_timing.txt (default 1) */
int  mpc_fb_tab_detect_enabled(void);

/* Reads current active tab name into out_tab (e.g. "MAIN", "LFO", "EQUALISER")
 * Returns 1 if a tab tag was found, 0 otherwise. */
int  mpc_fb_get_current_tab(char *out_tab, size_t max_len);

/* Checks if the user is currently viewing a specific tab (e.g. "EQ", "LFO", "STEP")
 * Returns 1 if active, 0 otherwise. */
int  mpc_fb_is_on_tab(const char *tab_name);

/* Check if currently viewing the Equalizer / EQ tab */
int  mpc_fb_is_on_eq_tab(void);

/* Read raw tab tag barcode string from screen at (x=0, y=110) */
int  mpc_fb_read_tab_tag(char *out_tag, size_t max_len);

/* Get configured card Y offset (default 112) */
int  mpc_fb_get_card_y(void);

/* -------------------------------------------------------------------------
 * Framebuffer 2D Drawing Primitives (Colors are 0xAARRGGBB)
 * ------------------------------------------------------------------------- */

uint32_t mpc_fb_get_pixel(int x, int y);
void     mpc_fb_put_pixel(int x, int y, uint32_t color);
void     mpc_fb_blend_pixel(int x, int y, uint32_t color);

void     mpc_fb_draw_hline(int x, int y, int w, uint32_t color);
void     mpc_fb_draw_vline(int x, int y, int h, uint32_t color);
void     mpc_fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color, int thickness);

void     mpc_fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void     mpc_fb_draw_rect(int x, int y, int w, int h, uint32_t color, int thickness);
void     mpc_fb_draw_circle(int cx, int cy, int r, uint32_t color, int filled);

void     mpc_fb_draw_text(int x, int y, const char *str, float scale, uint32_t color);
int      mpc_fb_text_width(const char *str, float scale);

void     mpc_fb_draw_curve(const int *curve_y, int x_start, int x_end, uint32_t color, int thickness);
void     mpc_fb_fill_curve_area(const int *curve_y, int x_start, int x_end, int y_baseline, uint32_t fill_color);

/* Atomically blit an offscreen 32-bit pixel buffer to the screen */
void     mpc_fb_blit(int dst_x, int dst_y, int w, int h, const uint32_t *src_pixels, int src_stride);

#ifdef __cplusplus
}
#endif

#endif /* MPC_FB_H */
