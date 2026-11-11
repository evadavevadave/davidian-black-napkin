/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Port indices, shared by the plugin and its GUI. They must match bundle/black_napkin.ttl.
 * The VST3 and AU parameter IDs are the same numbers. */
#ifndef PORTS_H
#define PORTS_H

#define BN_URI    "urn:davidian:black-napkin"
#define BN_UI_URI "urn:davidian:black-napkin#ui"

enum {
    PORT_IN, PORT_OUT,
    /* Front panel */
    PORT_RANGE,     /* envelope sensitivity (100k pot feeding the rectifier) */
    PORT_RESONANCE, /* 2M pot in the band-pass feedback path */
    PORT_ATTACK,    /* 5k pot charging the envelope capacitor */
    PORT_DECAY,     /* 1M pot discharging it */
    PORT_SPEED,     /* 1M reverse-audio pot on the sample clock */
    PORT_DIRECTION, /* toggle: 0 sweep up, 1 sweep down */
    PORT_MODE,      /* footswitch: 0 envelope filter, 1 sample and hold */
    /* Internal trimmers */
    PORT_SH_DEPTH,  /* 10k trimmer: noise level into the sample and hold */
    PORT_SH_OFFSET, /* 100k trimmer: sample and hold centre */
    /* Plugin extras */
    PORT_VOLTS, PORT_OUTPUT, PORT_ENABLED,
    PORT_LATENCY,   /* output */
    PORT_COUNT
};

#endif
