#pragma once
#ifndef MPC_EQ_UI_H
#define MPC_EQ_UI_H

#include "mpc_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* Initialize the EQ UI and auto-register render callback */
void mpc_eq_ui_init(void);

/* Parameter updates */
void mpc_eq_ui_set_state(const mpc_eq_state_t *state);
void mpc_eq_ui_set_band(int band_idx, int on, int shelf, float freq, float gain, float q);
void mpc_eq_ui_set_master_on(int on);

/* Main render function */
void mpc_eq_ui_render(void);

#ifdef __cplusplus
}
#endif

#endif /* MPC_EQ_UI_H */
