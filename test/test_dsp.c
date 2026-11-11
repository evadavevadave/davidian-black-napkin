/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Tests for the circuit model and the plugin. Usage: test_dsp
 * Compiles plugin.c in directly, so it tests the same code the bundle runs. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>

#include "../src/plugin.c"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: ");                                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

#define PI 3.14159265358979

static double db(double x) { return 20.0 * log10(x > 1e-30 ? x : 1e-30); }

/* ---- 1. The band-pass against its small-signal formulas ---- */

/* Gain of the filter model at f, for a fixed bias current, measured with a small sine. */
static double vcf_gain(double fs, double iabc, double res, double f, double amp)
{
    static Vcf v;
    const int settle = (int)(fs * 0.3), len = (int)(fs * 0.2);
    double sc = 0.0, ss = 0.0;
    int i;
    vcf_init(&v, fs, res);
    for (i = 0; i < settle + len; ++i) {
        const double ph = 2.0 * PI * f * i / fs;
        const double y = vcf_run(&v, amp * sin(ph), iabc);
        if (i >= settle) {
            sc += y * cos(ph);
            ss += y * sin(ph);
        }
    }
    return 2.0 * sqrt(sc * sc + ss * ss) / len / amp;
}

/* Finds the peak by golden-section search on a log frequency axis. */
static double vcf_peak(double fs, double iabc, double res, double f_guess, double* gain)
{
    double a = log(f_guess / 3.0), b = log(f_guess * 3.0);
    const double gr = 0.618033988749895;
    double c = b - gr * (b - a), d = a + gr * (b - a);
    int it;
    for (it = 0; it < 30; ++it) {
        if (vcf_gain(fs, iabc, res, exp(c), 1e-3) > vcf_gain(fs, iabc, res, exp(d), 1e-3))
            b = d;
        else
            a = c;
        c = b - gr * (b - a);
        d = a + gr * (b - a);
    }
    *gain = vcf_gain(fs, iabc, res, exp(0.5 * (a + b)), 1e-3);
    return exp(0.5 * (a + b));
}

static void test_filter(void)
{
    static const double IABC[] = { 1.2e-6, 12e-6, 50e-6, 150e-6 };
    static const double RES[] = { 0.0, 0.5, 1.0 };
    const double fs = 192000.0;
    int i, j;
    printf("Band-pass, 1 mV sine, 192 kHz (model vs formula):\n");
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 3; ++j) {
            Vcf v;
            double g, f, q_th, g_th, glo, ghi, flo, fhi;
            const double f_th = vcf_f0(IABC[i]);
            vcf_init(&v, fs, RES[j]);
            q_th = sqrt(B_DIV * C_INT1 / C_INT2) / v.kf;
            g_th = (R_FB + POT_RES * RES[j]) / R_IN;
            f = vcf_peak(fs, IABC[i], RES[j], f_th, &g);
            /* -3 dB points from the formula's bandwidth, as a Q check */
            flo = f_th * (sqrt(1.0 + 1.0 / (4.0 * q_th * q_th)) - 1.0 / (2.0 * q_th));
            fhi = f_th * (sqrt(1.0 + 1.0 / (4.0 * q_th * q_th)) + 1.0 / (2.0 * q_th));
            glo = vcf_gain(fs, IABC[i], RES[j], flo, 1e-3) / g;
            ghi = vcf_gain(fs, IABC[i], RES[j], fhi, 1e-3) / g;
            printf("  Iabc %6.1f uA  res %.1f: peak %8.1f Hz (formula %8.1f)  gain %5.2f (formula %5.2f)  Q %4.1f  "
                   "edges %.2f / %.2f\n",
                   IABC[i] * 1e6, RES[j], f, f_th, g, g_th, q_th, glo, ghi);
            CHECK(fabs(f / f_th - 1.0) < 0.03, "centre frequency off at %.1f uA, res %.1f", IABC[i] * 1e6, RES[j]);
            CHECK(fabs(g / g_th - 1.0) < 0.05, "peak gain off at %.1f uA, res %.1f", IABC[i] * 1e6, RES[j]);
            CHECK(fabs(glo - 0.7071) < 0.06 && fabs(ghi - 0.7071) < 0.06, "bandwidth (Q) off at %.1f uA, res %.1f",
                  IABC[i] * 1e6, RES[j]);
        }
    }
    {
        const double lo = vcf_f0(0.5 * iabc_total(-9.0, R_SEL_ENV)), hi = vcf_f0(0.5 * iabc_total(V_RAIL, R_SEL_ENV));
        printf("  sweep range: %.0f Hz (control at rest) to %.0f Hz (control at the rail)\n", lo, hi);
        CHECK(lo > 60.0 && lo < 250.0, "lowest frequency");
        CHECK(hi > 8000.0 && hi < 25000.0, "highest frequency");
    }
    /* The OTA limits loud signals: gain at the peak falls with level, and stays bounded. */
    {
        const double fs2 = 192000.0, ia = 20e-6, f0 = vcf_f0(ia);
        const double g_small = vcf_gain(fs2, ia, 1.0, f0, 1e-3), g_big = vcf_gain(fs2, ia, 1.0, f0, 2.0);
        printf("  full resonance at %.0f Hz: gain %.1f at 1 mV, %.1f at 2 V\n", f0, g_small, g_big);
        CHECK(g_big < 0.8 * g_small, "no OTA limiting on loud input");
        CHECK(g_big * 2.0 <= V_RAIL * 1.01, "band-pass node beyond the rails");
    }
}

/* ---- 2. Envelope and clock timing against their RC formulas ---- */

static void test_timing(void)
{
    const double fs = 48000.0, dt = 1.0 / fs;
    Envelope e;
    double t;
    int i;
    printf("Envelope and clock:\n");
    /* Attack: a steady 5 V drive charges 4.7 uF through 330R + 5k with decay at max. */
    {
        const double ra = R_ATT + POT_ATT, rd = R_DEC + POT_DEC;
        const double veq = (5.0 / ra + V_NEG / rd) / (1.0 / ra + 1.0 / rd);
        const double tau = C_ENV / (1.0 / ra + 1.0 / rd);
        e.v = 0.0;
        for (i = 0; e.v < 0.632 * veq && i < (int)fs; ++i)
            env_run(&e, -5.0 / 45.4545, R_RECT_FB / R_RECT, 1.0, 1.0, dt);
        t = i * dt;
        printf("  attack (5k): 63%% in %.2f ms (RC %.2f ms)\n", t * 1e3, tau * 1e3);
        CHECK(fabs(t / tau - 1.0) < 0.05, "attack time constant");
    }
    /* Decay: from 6 V, no drive, towards -9 V through 10k + 1M (in parallel with the leak).
     * The rectifier stops it at 0 V, so time the fall to 1 V. */
    {
        const double g = 1.0 / (R_ATT + POT_ATT * 0.0 + R_RECT_FB) + 1.0 / (R_DEC + POT_DEC);
        const double veq = (V_NEG / (R_DEC + POT_DEC)) / g, tau = C_ENV / g;
        const double t_th = tau * log((6.0 - veq) / (1.0 - veq));
        e.v = 6.0;
        for (i = 0; e.v > 1.0 && i < 20 * (int)fs; ++i)
            env_run(&e, 0.0, R_RECT_FB / R_RECT, 0.0, 1.0, dt);
        t = i * dt;
        printf("  decay (1M): 6 V to 1 V in %.2f s (RC formula %.2f s)\n", t, t_th);
        CHECK(fabs(t / t_th - 1.0) < 0.05, "decay time constant");
    }
    /* At rest the rectifier holds the cap just below 0 V: the filter is parked low. */
    {
        e.v = 0.0;
        for (i = 0; i < 5 * (int)fs; ++i)
            env_run(&e, 0.0, 45.0, 0.5, 0.5, dt);
        printf("  rest: envelope %.3f V, up-sweep control %.2f V, down-sweep %.2f V\n", e.v, env_cv(&e, 0), env_cv(&e, 1));
        CHECK(e.v < 0.0 && e.v > -1.0, "resting envelope voltage");
    }
    /* Clock: ticks per second at the ends and the middle of the Speed pot. */
    {
        static const double SP[] = { 0.0, 0.5, 1.0 };
        int s;
        for (s = 0; s < 3; ++s) {
            SampleHold sh;
            int ticks = 0;
            sh_init(&sh, 1);
            for (i = 0; i < 20 * (int)fs; ++i)
                ticks += sh_run(&sh, SP[s], 1.0, dt);
            printf("  speed %.1f: %.2f Hz (formula %.2f Hz)\n", SP[s] * 10, ticks / 20.0, 1.0 / sh_period(SP[s]));
            CHECK(fabs(ticks / 20.0 * sh_period(SP[s]) - 1.0) < 0.05, "clock rate at speed %.1f", SP[s]);
        }
        CHECK(1.0 / sh_period(0.0) > 1.0 && 1.0 / sh_period(0.0) < 2.0, "slowest clock");
        CHECK(1.0 / sh_period(1.0) > 8.0 && 1.0 / sh_period(1.0) < 12.0, "fastest clock");
    }
}

/* ---- 3. The whole plugin ---- */

typedef struct {
    Plugin* p;
    float ctl[PORT_COUNT];
} Host;

static void host_open(Host* h, double rate)
{
    int i;
    h->p = (Plugin*)instantiate(&descriptor, rate, "", NULL);
    h->ctl[PORT_RANGE] = 0.6f;
    h->ctl[PORT_RESONANCE] = 0.4f;
    h->ctl[PORT_ATTACK] = 0.2f;
    h->ctl[PORT_DECAY] = 0.3f;
    h->ctl[PORT_SPEED] = 0.5f;
    h->ctl[PORT_DIRECTION] = 0.0f;
    h->ctl[PORT_MODE] = 0.0f;
    h->ctl[PORT_SH_DEPTH] = 0.7f;
    h->ctl[PORT_SH_OFFSET] = 0.6f;
    h->ctl[PORT_VOLTS] = 1.0f;
    h->ctl[PORT_OUTPUT] = 0.0f;
    h->ctl[PORT_ENABLED] = 1.0f;
    for (i = PORT_RANGE; i < PORT_COUNT; ++i)
        connect_port(h->p, (uint32_t)i, &h->ctl[i]);
}

/* Runs a decaying plucked note (a 110 Hz sawtooth) and records the bias current every
 * 10 ms into iabc[] (up to n_rec). Returns the output peak. */
static double pluck_run(Host* h, double rate, double amp, double secs, double* iabc, int n_rec)
{
    const int n = (int)(rate * secs), step = (int)(rate * 0.01);
    float* in = (float*)malloc(sizeof(float) * n);
    float* out = (float*)malloc(sizeof(float) * n);
    double peak = 0.0;
    int i, r = 0;
    for (i = 0; i < n; ++i) {
        const double t = i / rate, ph = fmod(110.0 * t, 1.0);
        in[i] = (float)(amp * exp(-t / 0.6) * (2.0 * ph - 1.0));
    }
    connect_port(h->p, PORT_IN, in);
    connect_port(h->p, PORT_OUT, out);
    activate(h->p);
    for (i = 0; i < n; i += step) {
        const int m = n - i < step ? n - i : step;
        connect_port(h->p, PORT_IN, in + i);
        connect_port(h->p, PORT_OUT, out + i);
        run(h->p, (uint32_t)m);
        if (r < n_rec)
            iabc[r++] = h->p->iabc;
    }
    for (i = 0; i < n; ++i) {
        if (!isfinite(out[i])) { peak = 1e9; break; }
        if (fabs(out[i]) > peak) peak = fabs(out[i]);
    }
    free(in);
    free(out);
    return peak;
}

static double max_of(const double* a, int n)
{
    double m = a[0];
    int i;
    for (i = 1; i < n; ++i) if (a[i] > m) m = a[i];
    return m;
}

static double min_of(const double* a, int n)
{
    double m = a[0];
    int i;
    for (i = 1; i < n; ++i) if (a[i] < m) m = a[i];
    return m;
}

static void test_plugin(void)
{
    static const double RATES[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    double rec[300];
    Host h;
    int r;
    printf("Plugin, plucked 110 Hz note, 0.5 V:\n");
    for (r = 0; r < 5; ++r) {
        double peak, f_hi, f_end;
        host_open(&h, RATES[r]);
        peak = pluck_run(&h, RATES[r], 0.5, 3.0, rec, 300);
        f_hi = vcf_f0(0.5 * max_of(rec, 300));
        f_end = vcf_f0(0.5 * rec[299]);
        printf("  %6.0f Hz: up sweep to %6.0f Hz, back to %4.0f Hz after 3 s, output peak %.2f\n", RATES[r], f_hi,
               f_end, peak);
        CHECK(isfinite(peak) && peak < 20.0 && peak > 0.05, "output level at %.0f Hz", RATES[r]);
        CHECK(f_hi > 2000.0, "envelope does not open the filter at %.0f Hz", RATES[r]);
        CHECK(f_end < 400.0, "filter does not close again at %.0f Hz", RATES[r]);
        CHECK(*h.p->port[PORT_LATENCY] == (float)(h.p->os > 1 ? OS_LATENCY : 0), "latency port");
        cleanup(h.p);
    }
    /* Range sets the sweep depth; Direction turns the sweep over. */
    {
        double lo, hi, dn_rest, dn_min;
        host_open(&h, 48000.0);
        h.ctl[PORT_RANGE] = 0.15f;
        pluck_run(&h, 48000.0, 0.5, 1.0, rec, 100);
        lo = vcf_f0(0.5 * max_of(rec, 100));
        h.ctl[PORT_RANGE] = 1.0f;
        pluck_run(&h, 48000.0, 0.5, 1.0, rec, 100);
        hi = vcf_f0(0.5 * max_of(rec, 100));
        printf("  range 1.5 -> 10: sweep top %.0f Hz -> %.0f Hz\n", lo, hi);
        CHECK(hi > 1.5 * lo, "range does not deepen the sweep");
        h.ctl[PORT_RANGE] = 0.6f;
        h.ctl[PORT_DIRECTION] = 1.0f;
        pluck_run(&h, 48000.0, 0.5, 3.0, rec, 300);
        dn_rest = vcf_f0(0.5 * rec[299]);
        dn_min = vcf_f0(0.5 * min_of(rec, 300));
        printf("  down sweep: from %.0f Hz at rest down to %.0f Hz on the note\n", dn_rest, dn_min);
        CHECK(dn_rest > 8000.0 && dn_min < 0.3 * dn_rest, "down sweep");
        cleanup(h.p);
    }
    /* Sample and hold: steps at the clock rate, independent of the playing. */
    {
        double steps = 0.0, prev;
        int i, changes = 0;
        host_open(&h, 48000.0);
        h.ctl[PORT_MODE] = 1.0f;
        h.ctl[PORT_SPEED] = 1.0f;
        pluck_run(&h, 48000.0, 0.0, 3.0, rec, 300);
        prev = rec[10];
        for (i = 11; i < 300; ++i) {
            if (fabs(rec[i] - prev) > 0.05 * prev) ++changes;
            steps += fabs(rec[i] - prev);
            prev = rec[i];
        }
        printf("  sample and hold at speed 10: %d level changes in 2.9 s (clock %.1f Hz), %.0f to %.0f Hz\n", changes,
               1.0 / sh_period(1.0), vcf_f0(0.5 * min_of(rec + 10, 290)), vcf_f0(0.5 * max_of(rec + 10, 290)));
        CHECK(changes > 15 && changes < 45, "sample and hold steps");
        CHECK(vcf_f0(0.5 * max_of(rec + 10, 290)) > 3.0 * vcf_f0(0.5 * min_of(rec + 10, 290)), "sample and hold spread");
        h.ctl[PORT_SH_DEPTH] = 0.0f;
        pluck_run(&h, 48000.0, 0.0, 1.0, rec, 100);
        CHECK(max_of(rec + 10, 90) - min_of(rec + 10, 90) < 1e-9, "depth 0 still moves the filter");
        cleanup(h.p);
    }
    /* Silence in, silence out; bypass passes the input delayed by the latency. */
    {
        float in[4096] = { 0 }, out[4096];
        double m = 0.0;
        int i;
        host_open(&h, 48000.0);
        h.ctl[PORT_MODE] = 1.0f; /* the clock keeps stepping the filter */
        connect_port(h.p, PORT_IN, in);
        connect_port(h.p, PORT_OUT, out);
        activate(h.p);
        for (i = 0; i < 40; ++i) run(h.p, 4096);
        for (i = 0; i < 4096; ++i) if (fabs(out[i]) > m) m = fabs(out[i]);
        printf("  silence: output peak %.2e\n", m);
        CHECK(m < 1e-6, "output with no input");
        h.ctl[PORT_ENABLED] = 0.0f;
        activate(h.p);
        in[10] = 1.0f;
        run(h.p, 4096);
        printf("  bypass: impulse at 10 comes out at %d\n", out[10 + OS_LATENCY] == 1.0f ? 10 + OS_LATENCY : -1);
        CHECK(out[10 + OS_LATENCY] == 1.0f, "bypass delay is not the reported latency");
        cleanup(h.p);
    }
    /* Extreme settings and hot input stay finite and bounded. */
    {
        double peak;
        host_open(&h, 44100.0);
        h.ctl[PORT_RESONANCE] = 1.0f;
        h.ctl[PORT_RANGE] = 1.0f;
        h.ctl[PORT_ATTACK] = 0.0f;
        h.ctl[PORT_DECAY] = 0.0f;
        h.ctl[PORT_VOLTS] = 10.0f;
        peak = pluck_run(&h, 44100.0, 1.0, 2.0, rec, 200);
        printf("  full resonance, 10 V in: output peak %.2f\n", peak);
        CHECK(isfinite(peak) && peak < 2.0, "hot input");
        h.ctl[PORT_VOLTS] = 0.1f;
        peak = pluck_run(&h, 44100.0, 1.0, 2.0, rec, 200);
        printf("  full resonance, 0.1 V in: output peak %.2f\n", peak);
        CHECK(isfinite(peak) && peak < 80.0, "quiet input");
        cleanup(h.p);
    }
}

/* ---- 4. Cost ---- */

static void test_cpu(void)
{
    Host h;
    const int n = 48000 * 10;
    float* in = (float*)malloc(sizeof(float) * n);
    float* out = (float*)malloc(sizeof(float) * n);
    clock_t t0;
    double secs;
    int i;
    for (i = 0; i < n; ++i)
        in[i] = (float)(0.3 * sin(2.0 * PI * 110.0 * i / 48000.0));
    host_open(&h, 48000.0);
    h.ctl[PORT_RESONANCE] = 1.0f;
    activate(h.p);
    t0 = clock();
    for (i = 0; i < n; i += 256) {
        connect_port(h.p, PORT_IN, in + i);
        connect_port(h.p, PORT_OUT, out + i);
        run(h.p, 256);
    }
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("CPU: 10 s of audio at 48 kHz (4x oversampled) in %.2f s = %.1f%% of one core\n", secs, 10.0 * secs);
    CHECK(secs < 3.0, "too slow for real time");
    cleanup(h.p);
    free(in);
    free(out);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_filter();
    test_timing();
    test_plugin();
    test_cpu();
    printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
