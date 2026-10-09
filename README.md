# mpc-vst-helm-x (HelmX)

Native VST2 port and enhanced edition of the [Helm](https://tytel.org/helm/) polyphonic synthesizer for Akai MPC OS standalone devices (MPC Live, MPC One, MPC X, MPC Key, Force), built using [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins).

Developed & Maintained by **Lewinator56**.

---

## Overview & Features in HelmX

HelmX builds upon the Helm synthesis engine with enhanced capabilities tailored for modern stereo production and MPC standalone workflow:
- **True Stereo Unison Spread**: Spread unison detuned oscillator voices across the stereo field with dedicated spread amount controls (`Spread 1`, `Spread 2`).
- **Per-Oscillator Panning**: Independent stereo positioning for Oscillator 1 and Oscillator 2 (`Pan 1`, `Pan 2`).
- **Dual Filter Architecture**: Independent Filter 1 and Filter 2 with series, parallel, and split stereo routing options.
- **5-Band Parametric Graphic EQ**: Real-time interactive curve visualizer rendered directly to the hardware framebuffer.
- **Native Framebuffer Visualizer Engine**: Built-in 60 FPS drawing pipeline and automated tab detection for custom oscilloscope and graphics rendering.
- **Redesigned Touch UI Layout**: Full-width dual oscillator controls with 3-knob rows (`VOICES`, `DETUNE`, `SPREAD` & `TRANSPOSE`, `TUNE`, `PAN`) and interactive signal flow routing buttons.
- **32-Voice Polyphony** with full stereo voice signal paths and dual-channel effects routing.
- **Hardware Q-Link Integration & Touchscreen Preset Browser**.

---

## Native Framebuffer Drawing & Custom UI Guide

The wrapper library (`wrapper/mpc_fb.h` and `wrapper/mpc_fb.c`) provides direct, high-performance 60 FPS hardware framebuffer graphics on MPC standalone devices.

### 1. How It Works

- **Direct DRM Framebuffer Rendering**: The wrapper automatically finds and memory-maps the active display buffer (`/dev/dri`) at startup in a dedicated background worker thread.
- **Automatic Tab Detection**: The skin builder (`tools/shadow_skin.py`) automatically stamps a tiny 4-pixel barcode tag at the top-left of each tab's background image ($X \in [0..3], Y = 0$, mapping to screen $X = 0, Y = 110$). The framebuffer engine continuously checks this barcode to know which tab the user is viewing, ensuring custom graphics only render when their tab is active (0% CPU when viewing other tabs).
- **Zero-Latency Blitting**: Draw into a fast offscreen 32-bit pixel buffer in memory, then call `mpc_fb_blit()` to transfer the entire frame to the physical screen in a single memory burst (<150 microseconds).

---

### 2. Basic Custom UI Example

Here is how to create a custom visualizer (e.g. an oscilloscope, meter, or curve graph) on any tab:

#### Step A: Define your tab in `layout.conf`
```ini
[tab SCOPE]
# Leave empty area or place parameter knobs around your canvas
```

#### Step B: Write your custom UI component (`src/my_custom_ui.c`)
```c
#include "mpc_fb.h"
#include <stdint.h>

#define CANVAS_X 40
#define CANVAS_Y 120
#define CANVAS_W 1200
#define CANVAS_H 250

static uint32_t s_local_buffer[CANVAS_W * CANVAS_H];

void my_custom_ui_render(void) {
    // 1. Only draw if the user is currently on the "SCOPE" tab
    if (!mpc_fb_is_on_tab("SCOPE")) return;

    // 2. Clear canvas with dark background
    for (int i = 0; i < CANVAS_W * CANVAS_H; ++i) {
        s_local_buffer[i] = 0xFF101216; // 0xAARRGGBB
    }

    // 3. Draw UI elements (grid, text, shapes)
    // Draw border
    // (Local buffer coordinates: 0..CANVAS_W-1, 0..CANVAS_H-1)
    for (int x = 0; x < CANVAS_W; ++x) {
        s_local_buffer[0 * CANVAS_W + x] = 0xFF262D3A;
        s_local_buffer[(CANVAS_H - 1) * CANVAS_W + x] = 0xFF262D3A;
    }

    // 4. Atomically blit the buffer to the screen
    mpc_fb_blit(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, s_local_buffer, CANVAS_W);
}

// Register render callback with the framebuffer worker thread
__attribute__((constructor))
static void my_custom_ui_init(void) {
    mpc_fb_set_render_callback(my_custom_ui_render);
}
```

---

### 3. Framebuffer API Reference (`wrapper/mpc_fb.h`)

#### Tab Detection Functions
```c
// Returns 1 if currently viewing the specified tab name (e.g. "MAIN", "LFO", "SCOPE")
int mpc_fb_is_on_tab(const char *tab_name);

// Returns active tab name string into out_tab
int mpc_fb_get_current_tab(char *out_tab, size_t max_len);

// Dedicated helper for checking the Equalizer / EQ tab
int mpc_fb_is_on_eq_tab(void);

// Returns 1 if tab detection is active (configured in fb_timing.txt)
int mpc_fb_tab_detect_enabled(void);
```

#### 2D Drawing Primitives (Screen Coordinates: $X \in [0..1279], Y \in [0..799]$)
```c
// Colors are 32-bit ARGB (0xAARRGGBB)
void     mpc_fb_put_pixel(int x, int y, uint32_t color);
void     mpc_fb_blend_pixel(int x, int y, uint32_t color);
uint32_t mpc_fb_get_pixel(int x, int y);

void     mpc_fb_draw_hline(int x, int y, int w, uint32_t color);
void     mpc_fb_draw_vline(int x, int y, int h, uint32_t color);
void     mpc_fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color, int thickness);

void     mpc_fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void     mpc_fb_draw_rect(int x, int y, int w, int h, uint32_t color, int thickness);
void     mpc_fb_draw_circle(int cx, int cy, int radius, uint32_t color, int filled);

// Proportional 8x8 font text drawing
void     mpc_fb_draw_text(int x, int y, const char *str, float scale, uint32_t color);
int      mpc_fb_text_width(const char *str, float scale);

// Waveform and response curve plotting
void     mpc_fb_draw_curve(const int *curve_y, int x_start, int x_end, uint32_t color, int thickness);
void     mpc_fb_fill_curve_area(const int *curve_y, int x_start, int x_end, int y_baseline, uint32_t color);

// Fast DMA / memory block transfer
void     mpc_fb_blit(int dst_x, int dst_y, int w, int h, const uint32_t *src_pixels, int src_stride);
```

---

### 4. Hot-Reload Configuration (`fb_timing.txt`)

You can place an `fb_timing.txt` file directly inside the plugin folder (`/sdcard/Synths/Lewinator56 - VST - HelmX/fb_timing.txt`) to tweak drawing parameters on real hardware without recompiling:

```ini
# Frame rate interval in microseconds (16666 = 60 FPS, 33333 = 30 FPS)
interval_us=16666

# Delay/offset in microseconds before drawing each cycle
offset_us=0

# Number of burst draws per frame (default 1)
repeat=1

# 1 = Only render when active tab is detected; 0 = Always render
tab_detect=1

# Vertical screen offset for UI cards
card_y=112

# 1 = Enable debug logging to /tmp/mpc_fb.log
debug=0
```

---

## Building

```bash
# Build the plugin binary
make -j8

# Generate custom skins & package release zip
python3 tools/helm_paint.py
make package
```

Release bundle is output to `dist/HelmX-1.0.0-mpc-armv7.zip`.

---

## License

GPL-3.0 (see `LICENSE`). Original Helm engine copyright Matt Tytel. HelmX modifications by Lewinator56.
