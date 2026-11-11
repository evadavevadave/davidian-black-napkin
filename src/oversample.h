/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Polyphase FIR up- and down-sampling, so the filter runs at 4x (or 2x) the host rate
 * and the OTA distortion does not alias. Kaiser-windowed sinc, linear phase. The filters have
 * 32*OS+1 taps, so up plus down delays the signal by exactly 32 host samples. */
#ifndef OVERSAMPLE_H
#define OVERSAMPLE_H

#include <math.h>
#include <string.h>

#define OS_MAX       4
#define OS_HALF_SPAN 16 /* taps each side, in host samples */
#define OS_TAPS_MAX  (2 * OS_HALF_SPAN * OS_MAX + 1)
#define OS_LATENCY   (2 * OS_HALF_SPAN)

typedef struct {
    int os, taps, phase_len;
    double h[OS_TAPS_MAX + OS_MAX]; /* zero padded to a multiple of os */
    double up_hist[2 * (OS_TAPS_MAX / 1 + 1)];
    double dn_hist[2 * OS_TAPS_MAX];
    int up_pos, dn_pos;
} Oversampler;

static double bessel_i0(double x)
{
    double s = 1.0, t = 1.0;
    int k;
    for (k = 1; k < 40; ++k) {
        t *= (x / (2.0 * k)) * (x / (2.0 * k));
        s += t;
    }
    return s;
}

static void os_init(Oversampler* o, int os)
{
    const double beta = 9.0;
    int i, mid;
    double sum = 0.0;
    memset(o, 0, sizeof *o);
    o->os = os;
    if (os <= 1)
        return;
    o->taps = 2 * OS_HALF_SPAN * os + 1;
    o->phase_len = (o->taps + os - 1) / os;
    mid = o->taps / 2;
    for (i = 0; i < o->taps; ++i) {
        const double n = i - mid;
        const double fc = 0.5 / os * 0.9; /* cutoff, cycles per oversampled sample */
        const double x = 2.0 * fc * n;
        const double sinc = n == 0 ? 1.0 : sin(3.14159265358979 * x) / (3.14159265358979 * x);
        const double r = n / mid;
        o->h[i] = 2.0 * fc * sinc * bessel_i0(beta * sqrt(1.0 - r * r)) / bessel_i0(beta);
        sum += o->h[i];
    }
    for (i = 0; i < o->taps; ++i)
        o->h[i] /= sum; /* unity DC gain */
}

/* One host sample in, os samples out. */
static void os_up(Oversampler* o, double x, double* out)
{
    const int L = o->phase_len;
    int p, j;
    if (o->os <= 1) {
        out[0] = x;
        return;
    }
    /* Doubled history buffer so reads are contiguous. */
    o->up_pos = (o->up_pos == 0 ? L : o->up_pos) - 1;
    o->up_hist[o->up_pos] = o->up_hist[o->up_pos + L] = x;
    for (p = 0; p < o->os; ++p) {
        double s = 0.0;
        for (j = 0; j < L; ++j)
            s += o->h[j * o->os + p] * o->up_hist[o->up_pos + j];
        out[p] = s * o->os;
    }
}

/* os samples in, one host sample out. */
static double os_down(Oversampler* o, const double* in)
{
    const int T = o->taps;
    int p, j;
    double s = 0.0;
    if (o->os <= 1)
        return in[0];
    for (p = 0; p < o->os; ++p) {
        o->dn_pos = (o->dn_pos == 0 ? T : o->dn_pos) - 1;
        o->dn_hist[o->dn_pos] = o->dn_hist[o->dn_pos + T] = in[p];
        /* The output is aligned with the first of the os samples, so the total delay is a
         * whole number of host samples. */
        if (p == 0)
            for (j = 0; j < T; ++j)
                s += o->h[j] * o->dn_hist[o->dn_pos + j];
    }
    return s;
}

#endif
