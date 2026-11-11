/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* The GUI checks shared by ui_check.c (Win32) and ui_check_mac.m (Cocoa). The including
 * file defines check(), the last_port/last_value/writes globals filled by its write
 * callback, and these mouse helpers in panel coordinates (top-left origin):
 *   click(w, x, y), drag(w, x, y, dy), dblclick(w, x, y), wheel_up(w, x, y). */
#ifndef UI_SCRIPT_H
#define UI_SCRIPT_H

#include <math.h>
#include <stdio.h>

#include "../src/ports.h"

/* Port values as a host sends them on opening: the defaults in black_napkin.ttl. */
static const float UI_DEFAULTS[PORT_COUNT] = {
    0, 0, 0.6f, 0.4f, 0.2f, 0.3f, 0.5f, 0, 0, 0.7f, 0.6f, 1.0f, 0, 1, 0
};

static void ui_script(WIDGET_T w)
{
    char label[160];

    /* Knobs move 1/180 of their travel per pixel. */
    drag(w, 86, 170, 36);
    snprintf(label, sizeof label, "dragging Range down 36 px sends port %d = %.3f", last_port, last_value);
    check(label, last_port == PORT_RANGE && fabsf(last_value - 0.4f) < 0.01f);

    drag(w, 622, 170, -400);
    snprintf(label, sizeof label, "dragging Resonance far up stops at %.3f", last_value);
    check(label, last_port == PORT_RESONANCE && last_value == 1.0f);

    drag(w, 326, 170, 1);
    dblclick(w, 326, 170);
    snprintf(label, sizeof label, "double-click resets Decay to %.2f", last_value);
    check(label, last_port == PORT_DECAY && fabsf(last_value - 0.3f) < 1e-4f);

    click(w, 426, 170 + 18);
    check("bottom of the sweep switch selects Down", last_port == PORT_DIRECTION && last_value == 1.0f);
    click(w, 426, 170 - 18);
    check("top of the sweep switch selects Up", last_port == PORT_DIRECTION && last_value == 0.0f);
    click(w, 560, 410);
    check("mode footswitch selects sample and hold", last_port == PORT_MODE && last_value == 1.0f);
    click(w, 820, 410);
    check("on footswitch bypasses", last_port == PORT_ENABLED && last_value == 0.0f);

    drag(w, 270, 392, 18);
    snprintf(label, sizeof label, "dragging Output trim down sends %.2f dB", last_value);
    check(label, last_port == PORT_OUTPUT && last_value < -3.0f && last_value > -4.2f);
    drag(w, 922, 220, -18);
    snprintf(label, sizeof label, "dragging S/H Centre trim up sends %.3f", last_value);
    check(label, last_port == PORT_SH_OFFSET && fabsf(last_value - 0.7f) < 0.01f);

    writes = 0;
    wheel_up(w, 806, 170);
    snprintf(label, sizeof label, "mouse wheel on Speed sends %.3f", last_value);
    check(label, writes == 1 && last_port == PORT_SPEED && fabsf(last_value - 0.525f) < 1e-4f);
    click(w, 5, 5);
    check("clicking empty panel sends nothing new", writes == 1);
}

#endif
