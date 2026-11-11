/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Black Napkin: the audio engine shared by the LV2, VST3 and AU wrappers.
 * The host-facing state is an array of float pointers indexed like the LV2 ports (ports.h);
 * each wrapper points them at its own buffers and parameter values. The circuit model is
 * in filter.h. */
#ifndef ENGINE_H
#define ENGINE_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "oversample.h"
#include "ports.h"

/* Flush denormals to zero while processing, so decaying filter states can't stall the CPU. */
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#    include <xmmintrin.h>
typedef unsigned FpState;
static FpState fp_enter(void)
{
    const FpState s = _mm_getcsr();
    _mm_setcsr(s | 0x8040); /* FTZ | DAZ */
    return s;
}
static void fp_leave(FpState s) { _mm_setcsr(s); }
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
typedef unsigned long long FpState;
static FpState fp_enter(void)
{
    FpState s;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(s));
    __asm__ __volatile__("msr fpcr, %0" : : "r"(s | (1ULL << 24))); /* FZ */
    return s;
}
static void fp_leave(FpState s) { __asm__ __volatile__("msr fpcr, %0" : : "r"(s)); }
#else
typedef int FpState;
static FpState fp_enter(void) { return 0; }
static void fp_leave(FpState s) { (void)s; }
#endif

#define CTRL_BLOCK 32

/* Knobs that are smoothed, in this order. */
typedef struct {
    double range, resonance, attack, decay, speed, sh_depth, sh_offset;
} Knobs;
#define N_KNOBS 7

typedef struct {
    float* port[PORT_COUNT];
    double rate;
    int os;
    Vcf vcf;
    Envelope env;
    SampleHold sh;
    Oversampler ovs;
    Knobs k;
    int k_valid;
    double iabc;       /* total bias current, smoothed by the 150 nF cap */
    double iabc_a;     /* its per-sample smoothing coefficient */
    double hp_in_x, hp_in_y, hp_out_x, hp_out_y, hp_a;
    double dry[OS_LATENCY + 1];
    int dry_pos, latency;
    double wet;        /* 1 enabled, 0 bypassed; ramps */
    double gain;       /* smoothed output gain */
    double ramp;       /* per-sample step for the ramps */
    double smooth;     /* per-block smoothing coefficient for the knobs */
} Plugin;

static float port_or(const Plugin* p, int i, float def)
{
    const float v = p->port[i] ? *p->port[i] : def;
    return isfinite(v) ? v : def;
}

static void read_targets(const Plugin* p, Knobs* k)
{
    k->range = clamp01(port_or(p, PORT_RANGE, 0.6f));
    k->resonance = clamp01(port_or(p, PORT_RESONANCE, 0.4f));
    k->attack = clamp01(port_or(p, PORT_ATTACK, 0.2f));
    k->decay = clamp01(port_or(p, PORT_DECAY, 0.3f));
    k->speed = clamp01(port_or(p, PORT_SPEED, 0.5f));
    k->sh_depth = clamp01(port_or(p, PORT_SH_DEPTH, 0.7f));
    k->sh_offset = clamp01(port_or(p, PORT_SH_OFFSET, 0.6f));
}

/* Moves the knobs toward their port values; returns 1 if the resonance changed. */
static int smooth_controls(Plugin* p)
{
    Knobs t;
    double* cur = &p->k.range;
    const double* tgt = &t.range;
    const double res0 = p->k.resonance;
    int i;
    read_targets(p, &t);
    if (!p->k_valid) {
        p->k = t;
        p->k_valid = 1;
        return 1;
    }
    for (i = 0; i < N_KNOBS; ++i) {
        const double d = tgt[i] - cur[i];
        if (fabs(d) > 1e-5)
            cur[i] += fabs(d) < 1e-3 ? d : d * p->smooth;
        else
            cur[i] = tgt[i];
    }
    return p->k.resonance != res0;
}

/* The rate is fixed for the life of the engine: make a new one when it changes. */
static Plugin* engine_new(double rate)
{
    Plugin* p = (Plugin*)calloc(1, sizeof(Plugin));
    if (!p)
        return NULL;
    p->rate = rate;
    p->os = rate <= 50000.0 ? 4 : (rate <= 100000.0 ? 2 : 1);
    p->latency = p->os > 1 ? OS_LATENCY : 0;
    p->ramp = 1.0 / (0.01 * rate);
    p->smooth = 1.0 - exp(-CTRL_BLOCK / (0.02 * rate));
    p->iabc_a = 1.0 - exp(-1.0 / (TAU_IABC * rate));
    p->hp_a = exp(-1.0 / ((R_IN + R_GND) * C_COUPLE * rate)); /* about 10 Hz */
    return p;
}

/* Latency in samples at this engine's rate. */
static int engine_latency(const Plugin* p) { return p->latency; }

static void engine_connect(Plugin* p, uint32_t port, float* data)
{
    if (port < PORT_COUNT)
        p->port[port] = data;
}

/* Clears the filter, envelope and clock; the knobs jump to their current values. */
static void engine_activate(Plugin* p)
{
    p->k_valid = 0;
    smooth_controls(p);
    vcf_init(&p->vcf, p->rate * p->os, p->k.resonance);
    p->env.v = 0.0;
    sh_init(&p->sh, 0x2545F491u);
    os_init(&p->ovs, p->os);
    p->iabc = I_TRICKLE;
    p->hp_in_x = p->hp_in_y = p->hp_out_x = p->hp_out_y = 0.0;
    memset(p->dry, 0, sizeof p->dry);
    p->dry_pos = 0;
    p->wet = port_or(p, PORT_ENABLED, 1.0f) > 0.5f ? 1.0 : 0.0;
    p->gain = pow(10.0, port_or(p, PORT_OUTPUT, 0.0f) / 20.0);
}

/* PORT_IN and PORT_OUT may point at the same buffer. */
static void engine_run(Plugin* p, uint32_t n)
{
    const float* in = p->port[PORT_IN];
    float* out = p->port[PORT_OUT];
    const double volts = clampv(port_or(p, PORT_VOLTS, 1.0f), 0.01, 100.0);
    const double gain_t = pow(10.0, port_or(p, PORT_OUTPUT, 0.0f) / 20.0);
    const double wet_t = port_or(p, PORT_ENABLED, 1.0f) > 0.5f ? 1.0 : 0.0;
    const int down = port_or(p, PORT_DIRECTION, 0.0f) > 0.5f;
    const int sh_mode = port_or(p, PORT_MODE, 0.0f) > 0.5f;
    const double gain_a = 1.0 - exp(-1.0 / (0.02 * p->rate));
    const double dt = 1.0 / p->rate;
    const double out_scale = R_LOAD / (R_OUT + R_LOAD) / volts;
    double buf[OS_MAX], env_g = 0.0;
    uint32_t i;
    int k;
    const FpState fp = fp_enter();
    if (p->port[PORT_LATENCY])
        *p->port[PORT_LATENCY] = (float)p->latency;
    if (!in || !out)
        goto done;
    for (i = 0; i < n; ++i) {
        double x = in[i], v, cv, y, dry;
        if (i % CTRL_BLOCK == 0) {
            if (smooth_controls(p))
                vcf_set_resonance(&p->vcf, p->k.resonance);
            env_g = env_gain(p->k.range, down);
        }
        if (!isfinite(x))
            x = 0.0;
        /* Input coupling cap: about 10 Hz. */
        v = x * volts;
        p->hp_in_y = p->hp_a * (p->hp_in_y + v - p->hp_in_x);
        p->hp_in_x = v;
        v = p->hp_in_y;

        /* Control: both sources always run, as in the pedal; the mode switch picks one. */
        env_run(&p->env, v, env_g, p->k.attack, p->k.decay, dt);
        sh_run(&p->sh, p->k.speed, p->k.sh_depth, dt);
        cv = sh_mode ? iabc_total(sh_cv(&p->sh, p->k.sh_offset), R_SEL_SH)
                     : iabc_total(env_cv(&p->env, down), R_SEL_ENV);
        p->iabc += p->iabc_a * (cv - p->iabc);

        /* Audio: the filter runs oversampled. */
        os_up(&p->ovs, v, buf);
        for (k = 0; k < p->os; ++k)
            buf[k] = vcf_run(&p->vcf, buf[k], 0.5 * p->iabc);
        y = os_down(&p->ovs, buf);
        /* Output coupling cap into 100k: about 10 Hz. */
        p->hp_out_y = p->hp_a * (p->hp_out_y + y - p->hp_out_x);
        p->hp_out_x = y;
        y = p->hp_out_y * out_scale;

        p->gain += gain_a * (gain_t - p->gain);
        y *= p->gain;
        /* Bypass is delayed by the same latency, so switching does not jump in time. */
        p->dry[p->dry_pos] = x;
        dry = p->dry[(p->dry_pos + OS_LATENCY + 1 - p->latency) % (OS_LATENCY + 1)];
        p->dry_pos = (p->dry_pos + 1) % (OS_LATENCY + 1);
        p->wet += wet_t > p->wet ? p->ramp : (wet_t < p->wet ? -p->ramp : 0.0);
        p->wet = clamp01(p->wet);
        y = dry + p->wet * (y - dry);
        if (!isfinite(y)) { /* never pass garbage to the host; restart the model */
            y = 0.0;
            vcf_init(&p->vcf, p->rate * p->os, p->k.resonance);
            p->env.v = 0.0;
            p->iabc = I_TRICKLE;
            p->hp_in_x = p->hp_in_y = p->hp_out_x = p->hp_out_y = 0.0;
        }
        out[i] = (float)y;
    }
done:
    fp_leave(fp);
}

static void engine_free(Plugin* p) { free(p); }

#endif
