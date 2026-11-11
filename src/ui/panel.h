/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * The design is original: the house style (graphite panel, cream lettering, amber accents)
 * with the controls grouped by circuit block (envelope, filter, sample and hold, levels,
 * footswitches). It uses no branding or panel artwork from any original pedal. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "../ports.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 1000
#define PANEL_H 500
#define READ_X0 640 /* readout window */
#define READ_X1 984

enum { W_KNOB, W_TRIM, W_SWITCH, W_FOOT, W_SLIDER };

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def;
    int log;
    const char* label; /* on the panel */
    const char* name;  /* in the display */
    const char* lo;    /* scale end labels */
    const char* hi;
    const char* opts[3];
    int nopts;
} Widget;

static const Widget WIDGETS[] = {
    { W_KNOB, PORT_RANGE, 86, 170, 30, 0, 1, 0.6f, 0, "RANGE", "ENVELOPE RANGE", "0", "10", {0}, 0 },
    { W_KNOB, PORT_ATTACK, 206, 170, 30, 0, 1, 0.2f, 0, "ATTACK", "ATTACK", "FAST", "SLOW", {0}, 0 },
    { W_KNOB, PORT_DECAY, 326, 170, 30, 0, 1, 0.3f, 0, "DECAY", "DECAY", "FAST", "SLOW", {0}, 0 },
    { W_SWITCH, PORT_DIRECTION, 426, 170, 0, 0, 1, 0, 0, "SWEEP", "SWEEP DIRECTION", 0, 0, { "UP", "DOWN" }, 2 },
    { W_KNOB, PORT_RESONANCE, 622, 170, 30, 0, 1, 0.4f, 0, "RESONANCE", "RESONANCE", "0", "10", {0}, 0 },
    { W_KNOB, PORT_SPEED, 806, 170, 30, 0, 1, 0.5f, 0, "SPEED", "S/H SPEED", "SLOW", "FAST", {0}, 0 },
    { W_TRIM, PORT_SH_DEPTH, 922, 128, 17, 0, 1, 0.7f, 0, "DEPTH", "S/H DEPTH (INTERNAL)", 0, 0, {0}, 0 },
    { W_TRIM, PORT_SH_OFFSET, 922, 220, 17, 0, 1, 0.6f, 0, "CENTRE", "S/H CENTRE (INTERNAL)", 0, 0, {0}, 0 },
    { W_TRIM, PORT_VOLTS, 110, 392, 17, 0.1f, 10.0f, 1.0f, 1, "INPUT V", "VOLTS AT 0 DBFS", 0, 0, {0}, 0 },
    { W_TRIM, PORT_OUTPUT, 270, 392, 17, -24.0f, 12.0f, 0.0f, 0, "OUTPUT", "OUTPUT LEVEL", 0, 0, {0}, 0 },
    { W_FOOT, PORT_MODE, 560, 410, 25, 0, 1, 0, 0, "SAMPLE/HOLD", "MODE", 0, 0, { "ENVELOPE", "SAMPLE AND HOLD" }, 2 },
    { W_FOOT, PORT_ENABLED, 820, 410, 25, 0, 1, 1, 0, "ON", "FILTER", 0, 0, { "BYPASS", "ON" }, 2 },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */

typedef void (*PanelWrite)(void* host, int port, float value);
/* Optional: brackets each user edit (begin = 1, then writes, then begin = 0), so hosts
 * that record automation see one gesture per drag. */
typedef void (*PanelGesture)(void* host, int port, int begin);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[PORT_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_x, drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    PanelGesture gesture; /* may be NULL; set after panel_init */
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    float n = w->log ? logf(v / w->min) / logf(w->max / w->min) : (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->log ? w->min * powf(w->max / w->min, n) : w->min + n * (w->max - w->min);
}

static int w_index(const Panel* p, const Widget* w)
{
    int i = (int)(p->val[w->port] + 0.5f);
    int n = w->kind == W_FOOT ? 2 : w->nopts;
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->port];
    switch (w->kind) {
    case W_SWITCH:
    case W_FOOT:
        snprintf(buf, n, "%s", w->opts[w_index(p, w)]);
        break;
    default:
        /* Times and rates from the part values in filter.h (copied here so the GUI does not
         * pull in the DSP code). */
        if (w->port == PORT_VOLTS)
            snprintf(buf, n, "%.2f V", v);
        else if (w->port == PORT_OUTPUT)
            snprintf(buf, n, "%+.1f DB", v);
        else if (w->port == PORT_ATTACK)
            snprintf(buf, n, "%.1f  (%.1f MS)", 10.0f * v, (330.0f + 5e3f * v) * 4.7e-3f);
        else if (w->port == PORT_DECAY)
            snprintf(buf, n, "%.1f  (%.2f S)", 10.0f * v, (10e3f + 1e6f * v) * 4.7e-6f);
        else if (w->port == PORT_SPEED)
            snprintf(buf, n, "%.1f  (%.1f HZ)", 10.0f * v,
                     1.0f / (1.658f * 330e-9f * (180e3f + 1e6f * (powf(81.0f, 1.0f - v) - 1.0f) / 80.0f)));
        else
            snprintf(buf, n, "%.1f", 10.0f * v);
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, 9, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, 9, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, 7, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, 7, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? 10.0f : 8.0f, 2.0f,
                big ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_switch_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x + 14, w->y - 44, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
    /* Mounting plate and nut. */
    cv_rrect(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 0x000000, 0.35f);
    cv_rrect_line(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 1, 0x71757d, 0.7f);
    cv_circle(c, w->x + 1, w->y + 2, 8.5f, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, 8.5f, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, 5, 0x2d2f33, 1);
}

static void draw_foot_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 9, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 8, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 8, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 18, w->label, 10, 2.5f, 1.7f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 26, 7.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 26, 7.5f, 1.5f, 0x8a8e96, 0.8f);
}

static void draw_slider_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x, w->y - 42, w->label, 9, 2.5f, 1.5f, COL_INK, 1);
    cv_rrect(c, w->x - w->r - 4, w->y - 5, w->x + w->r + 4, w->y + 5, 5, 0x0b0b0d, 1);
    cv_rrect_line(c, w->x - w->r - 4, w->y - 5, w->x + w->r + 4, w->y + 5, 5, 1, 0x5d626c, 0.6f);
    text_draw(c, w->x - w->r - 4, w->y + 22, w->lo, 7, 1, 1.1f, COL_DIM, 1);
    text_draw(c, w->x + w->r + 4 - text_width(w->hi, 7, 1), w->y + 22, w->hi, 7, 1, 1.1f, COL_DIM, 1);
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, "DAVIDIAN BLACK NAPKIN", 20, 3.2f, 2.8f, COL_INK, 1);
    text_draw(&c, 32, 46, "VOLTAGE-CONTROLLED FILTER - ENVELOPE / SAMPLE AND HOLD", 7, 2.4f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, READ_X0 - 30, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, READ_X0, 14, READ_X1, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, READ_X0 + 2, 16, READ_X1 - 2, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, READ_X0, 14, READ_X1, 56, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, 76, 516, 286, "ENVELOPE");
    draw_section(&c, 532, 76, 712, 286, "FILTER");
    draw_section(&c, 728, 76, 984, 286, "SAMPLE + HOLD");
    draw_section(&c, 16, 306, 380, 484, "LEVELS");
    draw_section(&c, 396, 306, 984, 484, "FOOTSWITCHES");

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_SWITCH: draw_switch_static(&c, w); break;
        case W_FOOT: draw_foot_static(&c, w); break;
        case W_SLIDER: draw_slider_static(&c, w); break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int knurls = w->kind == W_KNOB ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), w->kind == W_KNOB ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), w->kind == W_KNOB ? 3.0f : 2.2f, COL_INK, 1);
}

static void draw_switch(Canvas* c, const Widget* w, int idx)
{
    const float x = w->x, y = w->y;
    float ey;
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const float oy = w->nopts == 3 ? y - 20.0f + 20.0f * i : y - 20.0f + 40.0f * i;
        text_draw(c, x + 15, oy - 3.5f, w->opts[i], 7, 1.6f, 1.25f, i == idx ? COL_AMBER : COL_DIM, 1);
    }
    if (w->nopts == 3 && idx == 1) {
        /* Centre: the lever points at the viewer. */
        cv_circle(c, x + 1, y + 2, 5.5f, 0x000000, 0.5f);
        cv_shaded_circle(c, x, y, 5.5f, 0xf2f3f5, 0x8d9097, -0.3f, -0.4f, 1);
        return;
    }
    ey = (idx == 0) ? y - 19.0f : y + 19.0f;
    cv_line(c, x + 1.5f, y + 2, x + 1.5f, ey + 2, 6, 0x000000, 0.45f);
    cv_line(c, x, y, x, ey, 5.5f, 0xb6b9bf, 1);
    cv_line(c, x - 1.2f, y, x - 1.2f, ey, 1.6f, 0xf0f1f3, 0.7f);
    cv_shaded_circle(c, x, ey, 4.8f, 0xf4f5f7, 0x7c7f86, -0.35f, -0.45f, 1);
}

static void draw_foot(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 26;
    cv_shaded_circle(c, w->x, w->y, w->r, 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 5, 16, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 5.5f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 5.5f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static void draw_slider(Canvas* c, const Widget* w, float n)
{
    const float px = w->x - w->r + 2.0f * w->r * n;
    cv_rrect(c, w->x - w->r - 1, w->y - 2, px, w->y + 2, 2, COL_AMBER, 0.9f);
    cv_rrect(c, px - 9, w->y - 13, px + 11, w->y + 17, 4, 0x000000, 0.45f);
    cv_rrect_vgrad(c, px - 10, w->y - 15, px + 10, w->y + 15, 4, 0xcfd2d8, 0x5e6168, 1);
    cv_line(c, px, w->y - 10, px, w->y + 10, 2, 0x26282c, 0.8f);
}

static void draw_display(Panel* p)
{
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, READ_X0 + 16, 22, "BLACK NAPKIN", 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, READ_X0 + 16, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS", 7, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    text_draw(c, READ_X0 + 16, 22, WIDGETS[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, READ_X0 + 16, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_SWITCH: draw_switch(&p->c, w, w_index(p, w)); break;
        case W_FOOT: draw_foot(&p->c, w, w_index(p, w)); break;
        case W_SLIDER: draw_slider(&p->c, w, w_norm(w, p->val[w->port])); break;
        }
    }
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i)
        p->val[WIDGETS[i].port] = WIDGETS[i].def;
    p->val[PORT_ENABLED] = 1.0f;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= PORT_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    if (w->kind == W_SWITCH || w->kind == W_FOOT)
        v = floorf(v + 0.5f);
    if (v < w->min && !w->log) v = w->min;
    if (v > w->max) v = w->max;
    if (w->log && v < w->min) v = w->min;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        /* Outside a drag (clicks, wheel, double-click) each change is its own gesture. */
        const int lone = p->drag != wi && p->gesture;
        p->val[w->port] = v;
        if (lone)
            p->gesture(p->host, w->port, 1);
        if (p->write)
            p->write(p->host, w->port, v);
        if (lone)
            p->gesture(p->host, w->port, 0);
    }
}

static void drag_start(Panel* p, int wi)
{
    p->drag = wi;
    if (p->gesture)
        p->gesture(p->host, WIDGETS[wi].port, 1);
}

static int hit(float x, float y)
{
    int i;
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM:
        case W_FOOT:
            if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
                return i;
            break;
        case W_SWITCH:
            if (dx >= -14 && dx <= 62 && dy >= -30 && dy <= 30)
                return i;
            break;
        case W_SLIDER:
            if (fabsf(dx) <= w->r + 12 && fabsf(dy) <= 18)
                return i;
            break;
        }
    }
    return -1;
}

static int switch_pos_at(const Widget* w, float y)
{
    const float dy = y - w->y;
    if (w->nopts == 3)
        return dy < -8.0f ? 0 : (dy > 8.0f ? 2 : 1);
    return dy < 0.0f ? 0 : 1;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    switch (w->kind) {
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        drag_start(p, i);
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_SLIDER:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        drag_start(p, i);
        set_value(p, i, w_value(w, (x - (w->x - w->r)) / (2.0f * w->r)));
        break;
    case W_SWITCH:
        set_value(p, i, (float)switch_pos_at(w, y));
        break;
    case W_FOOT:
        set_value(p, i, w_index(p, w) ? 0.0f : 1.0f);
        break;
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    if (w->kind == W_SLIDER) {
        set_value(p, p->drag, w_value(w, (x - (w->x - w->r)) / (2.0f * w->r)));
        return;
    }
    {
        const float n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
        /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->drag_y = y;
    }
}

static void panel_mouse_up(Panel* p)
{
    const int d = p->drag;
    if (d < 0)
        return;
    p->dirty = 1;
    p->drag = -1;
    if (p->gesture)
        p->gesture(p->host, WIDGETS[d].port, 0);
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_SWITCH) {
        int n = w_index(p, w) - (steps > 0 ? 1 : -1);
        set_value(p, i, (float)(n < 0 ? 0 : (n >= w->nopts ? w->nopts - 1 : n)));
    } else if (w->kind != W_FOOT) {
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
    }
}

#endif
