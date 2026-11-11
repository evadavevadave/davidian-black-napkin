/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Davidian Black Napkin: the circuit model.
 *
 * Signal path. The input is attenuated about 31 times (100k / 3k3) into the first OTA of a
 * dual transconductance amplifier. Each OTA drives a capacitor (1 nF, then 30 nF) as an
 * integrator; a JFET source follower buffers the first. The two integrators in a loop make
 * a state-variable band-pass, and the pedal's output is the band-pass node:
 *
 *   C3 dVbp/dt =  Iabc tanh((Vlp - kin Vin - kf Vbp) / 2VT)      first OTA, inputs +Vlp / -V2
 *   C5 dVlp/dt = -Iabc tanh(b Vbp / 2VT)                         second OTA, b = 3k3 / 103k3
 *
 * kin and kf come from the divider at the first OTA's inverting input: 100k from the input,
 * 100k plus the 2M Resonance pot from the band-pass node, 3k3 to ground. With equal bias
 * currents, w0 = Iabc / 2VT * sqrt(b / (C3 C5)), Q = sqrt(b C3 / C5) / kf (about 1 to 21)
 * and the gain at w0 is (100k + Resonance) / 100k. The tanh is the OTA's differential pair,
 * which gives the filter its grit on loud notes and limits the resonance.
 *
 * Control path. One PNP transistor with a grounded base is the bias-current source for both
 * OTAs (22k into each Iabc pin); 2M7 to ground keeps a trickle flowing when it is off, which
 * parks the filter near 120 Hz, and 150 nF smooths the current. Its emitter is fed through
 * 22k from the envelope (filter mode) or through 100k from the sample and hold (S/H mode),
 * plus 1M from +9 V.
 *
 * Envelope: the input, set by the Range pot, drives a precision half-wave rectifier (gain
 * 1M / 22k) that charges 4.7 uF through 330R plus the 5k Attack pot; 10k plus the 1M Decay
 * pot discharge it towards -9 V. An op-amp buffers it (up) or inverts it about 3.6 V (down).
 *
 * Sample and hold: a reverse-biased transistor junction makes noise, an op-amp amplifies it,
 * and a JFET switch samples it onto a capacitor each time a Schmitt-trigger clock ticks
 * (330 nF, 180k plus the 1M Speed pot: about 1.5 to 10 Hz). A second JFET buffers the held
 * voltage. */
#ifndef FILTER_H
#define FILTER_H

#include <math.h>
#include <stdint.h>

/* ---- Parts ---- */
#define VT2       0.0517   /* 2 kT/q at 27 C: an OTA's tanh input scale */
#define C_INT1    1e-9     /* first integrator */
#define C_INT2    30e-9    /* second integrator */
#define R_IN      100e3    /* input to the first OTA */
#define R_GND     3.3e3    /* first OTA input to ground */
#define R_FB      100e3    /* band-pass feedback, plus the Resonance pot */
#define POT_RES   2e6
#define B_DIV     (3.3e3 / 103.3e3) /* band-pass node into the second OTA */
#define V_RAIL    7.5      /* op-amp and buffer swing on +/-9 V */
#define V_NEG     -9.0     /* charge pump supply */
#define R_OUT     3.3e3    /* output series resistor and load into the output cap */
#define R_LOAD    100e3
#define C_COUPLE  150e-9   /* input and output coupling caps */

/* Envelope */
#define POT_RANGE 100e3
#define R_RECT    22e3     /* rectifier input; 12k in parallel when sweeping down */
#define R_RECT_DN 12e3
#define R_RECT_FB 1e6      /* rectifier gain resistor */
#define V_RECT    6.9      /* rectifier output limit: op-amp swing less a diode */
#define R_ATT     330.0
#define POT_ATT   5e3
#define R_DEC     10e3
#define POT_DEC   1e6
#define C_ENV     4.7e-6
#define V_REV     7.2      /* down sweep: 2 x 3.6 V (180k / 120k divider from +9 V) */

/* Bias-current source */
#define V_BE      0.65
#define R_EMIT_HI 1e6      /* from +9 V */
#define R_SEL_ENV 22e3
#define R_SEL_SH  100e3
#define I_TRICKLE 2.4e-6   /* through 2M7 from the Iabc node (about -6.5 V) to ground */
#define TAU_IABC  1.65e-3  /* 150 nF with the two 22k Iabc resistors */
#define I_ABC_MAX 1e-3

/* Sample and hold */
#define C_CLOCK   330e-9
#define R_CLOCK   180e3
#define POT_SPEED 1e6
#define CLOCK_LN  1.658    /* ln(1.68 / 0.32): Schmitt thresholds at 0.68 of the swing */
#define NOISE_RMS 2.5      /* amplified junction noise, before the op-amp clips it */
#define V_JFET    1.5      /* gate to source of the hold buffer */

static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
static double clampv(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* tanh(u) / u */
static double tanhc(double u)
{
    const double a = fabs(u);
    return a < 1e-4 ? 1.0 - u * u / 3.0 : tanh(a) / a;
}

/* An audio-taper (10% at half way) pot: fraction of its resistance at rotation x. */
static double taper_audio(double x) { return (pow(81.0, clamp01(x)) - 1.0) / 80.0; }

/* ---- The voltage-controlled band-pass ---- */

typedef struct {
    double fs, T;
    double s1, s2;  /* trapezoidal integrator states */
    double u1, u2;  /* last OTA input levels, for the next estimate of the tanh gains */
    double kin, kf;
} Vcf;

static void vcf_set_resonance(Vcf* v, double res)
{
    const double rf = R_FB + POT_RES * clamp01(res);
    const double g = 1.0 / R_IN + 1.0 / rf + 1.0 / R_GND;
    v->kin = (1.0 / R_IN) / g;
    v->kf = (1.0 / rf) / g;
}

static void vcf_init(Vcf* v, double fs, double res)
{
    v->fs = fs;
    v->T = 1.0 / fs;
    v->s1 = v->s2 = v->u1 = v->u2 = 0.0;
    vcf_set_resonance(v, res);
}

/* Centre frequency (Hz) for a bias current per OTA, small signals. */
static double vcf_f0(double iabc)
{
    return iabc / VT2 * sqrt(B_DIV / (C_INT1 * C_INT2)) / (2.0 * 3.14159265358979);
}

/* One sample: input volts in, band-pass node volts out. Trapezoidal integration (bilinear,
 * prewarped at w0); the OTA tanh is folded into each integrator's gain, re-estimated twice
 * per sample from the solved node voltages. */
static double vcf_run(Vcf* v, double vin, double iabc)
{
    const double w1 = iabc / (C_INT1 * VT2), w2 = iabc * B_DIV / (C_INT2 * VT2);
    double wd = sqrt(w1 * w2) * 0.5 * v->T, pw, g1b, g2b, vbp = 0.0, vlp = 0.0;
    int it;
    if (wd > 1.4)
        wd = 1.4;
    pw = wd > 1e-6 ? tan(wd) / wd : 1.0;
    g1b = w1 * 0.5 * v->T * pw;
    g2b = w2 * 0.5 * v->T * pw;
    for (it = 0; it < 2; ++it) {
        const double g1 = g1b * tanhc(v->u1), g2 = g2b * tanhc(v->u2);
        vbp = (v->s1 + g1 * (v->s2 - v->kin * vin)) / (1.0 + g1 * g2 + g1 * v->kf);
        vlp = v->s2 - g2 * vbp;
        v->u1 = (vlp - v->kin * vin - v->kf * vbp) / VT2;
        v->u2 = B_DIV * vbp / VT2;
    }
    if (vbp > V_RAIL || vbp < -V_RAIL) { /* the buffer and OTA output run out of supply */
        vbp = clampv(vbp, -V_RAIL, V_RAIL);
        v->s1 = vbp;
    } else {
        v->s1 = 2.0 * vbp - v->s1;
    }
    v->s2 = clampv(2.0 * vlp - v->s2, -2.0 * V_RAIL, 2.0 * V_RAIL);
    return vbp;
}

/* ---- Envelope follower ---- */

typedef struct {
    double v;   /* envelope capacitor volts */
} Envelope;

/* Rectifier gain from the input volts, for the Range pot and sweep direction. */
static double env_gain(double range, int down)
{
    const double p = clamp01(range);
    const double rin = down ? R_RECT * R_RECT_DN / (R_RECT + R_RECT_DN) : R_RECT;
    return p * R_RECT_FB / (p * (1.0 - p) * POT_RANGE + rin);
}

/* One sample of length dt. The rectifier drives the capacitor through the attack resistance
 * while its output is above the capacitor; otherwise the capacitor only leaks back through
 * the attack and 1M resistors. The decay path pulls towards -9 V all the time. */
static void env_run(Envelope* e, double vin, double gain, double attack, double decay, double dt)
{
    const double vt = clampv(-vin * gain, 0.0, V_RECT);
    const double ra = R_ATT + POT_ATT * clamp01(attack), rd = R_DEC + POT_DEC * clamp01(decay);
    double g, veq;
    if (e->v < vt) {
        g = 1.0 / ra + 1.0 / rd;
        veq = (vt / ra + V_NEG / rd) / g;
    } else {
        g = 1.0 / (ra + R_RECT_FB) + 1.0 / rd;
        veq = (V_NEG / rd) / g;
    }
    e->v = veq + (e->v - veq) * exp(-dt * g / C_ENV);
}

/* Control volts from the envelope buffer. */
static double env_cv(const Envelope* e, int down)
{
    return clampv(down ? V_REV - e->v : e->v, V_NEG + 0.7, V_RAIL);
}

/* ---- Sample and hold ---- */

typedef struct {
    double phase; /* clock, 0..1 per period */
    double held;  /* volts on the hold capacitor */
    uint32_t rng;
    double spare;
    int have_spare;
} SampleHold;

static double sh_uniform(SampleHold* s)
{
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    return (s->rng + 0.5) / 4294967296.0;
}

static double sh_gauss(SampleHold* s)
{
    double r, a;
    if (s->have_spare) {
        s->have_spare = 0;
        return s->spare;
    }
    r = sqrt(-2.0 * log(sh_uniform(s)));
    a = 2.0 * 3.14159265358979 * sh_uniform(s);
    s->spare = r * sin(a);
    s->have_spare = 1;
    return r * cos(a);
}

/* Clock period (s) at Speed rotation k (clockwise is faster). */
static double sh_period(double speed)
{
    return CLOCK_LN * C_CLOCK * (R_CLOCK + POT_SPEED * taper_audio(1.0 - clamp01(speed)));
}

static void sh_init(SampleHold* s, uint32_t seed)
{
    s->phase = 0.0;
    s->held = 0.0;
    s->rng = seed ? seed : 0x9E3779B9u;
    s->have_spare = 0;
}

/* Advances the clock by dt; on a tick, samples fresh noise. Returns 1 on a tick. */
static int sh_run(SampleHold* s, double speed, double depth, double dt)
{
    s->phase += dt / sh_period(speed);
    if (s->phase < 1.0)
        return 0;
    s->phase -= floor(s->phase);
    s->held = clampv(NOISE_RMS * sh_gauss(s), -V_RAIL, V_RAIL) * clamp01(depth);
    return 1;
}

/* Control volts from the hold buffer; the offset trimmer shifts the centre by up to
 * +/-4.5 V. */
static double sh_cv(const SampleHold* s, double offset)
{
    return clampv(s->held + V_JFET + 9.0 * (clamp01(offset) - 0.5), V_NEG, 9.0);
}

/* ---- Bias-current source ---- */

/* Total Iabc (both OTAs) for control volts cv fed through r_sel. */
static double iabc_total(double cv, double r_sel)
{
    double ie = (9.0 - V_BE) / R_EMIT_HI + (cv - V_BE) / r_sel;
    if (ie < 0.0)
        ie = 0.0;
    return clampv(0.99 * ie + I_TRICKLE, 0.0, 2.0 * I_ABC_MAX);
}

#endif
