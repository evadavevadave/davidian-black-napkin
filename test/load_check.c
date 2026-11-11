/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian. See LICENSE and NOTICE.md. */
/* Loads the built plugin (and optionally the GUI) from the bundle with dlopen, the way a
 * Linux or macOS host does, and runs each through its life cycle. On Linux run it under
 * a virtual display (xvfb-run) to exercise the X11 GUI.
 * Usage: load_check <bundle folder> <.so|.dylib> [--no-ui] */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/lv2_abi.h"
#include "../src/ports.h"

typedef void (*WriteFn)(void*, uint32_t, uint32_t, uint32_t, const void*);
typedef struct {
    const char* URI;
    void* (*instantiate)(const void*, const char*, const char*, WriteFn, void*, void**, const LV2_Feature* const*);
    void (*cleanup)(void*);
    void (*port_event)(void*, uint32_t, uint32_t, uint32_t, const void*);
    const void* (*extension_data)(const char*);
} UiDesc;
typedef struct { int (*idle)(void* ui); } IdleIface;

static int failures;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* c, uint32_t p, uint32_t s, uint32_t f, const void* b)
{
    (void)c; (void)p; (void)s; (void)f; (void)b;
}

/* The defaults in black_napkin.ttl. */
static const float DEFAULTS[PORT_COUNT] = {
    0, 0, 0.6f, 0.4f, 0.2f, 0.3f, 0.5f, 0, 0, 0.7f, 0.6f, 1.0f, 0, 1, 0
};

static void check_plugin(const char* bundle, const char* ext)
{
    char path[1024];
    void* lib;
    const LV2_Descriptor* (*get)(uint32_t);
    const LV2_Descriptor* d;
    LV2_Handle h;
    float ctl[PORT_COUNT];
    float in[512], out[512];
    int i, block, finite = 1;
    double peak_clean = 0, peak_od = 0;

    snprintf(path, sizeof path, "%s/black_napkin%s", bundle, ext);
    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    check("plugin library loads", lib != NULL);
    if (!lib) {
        printf("  %s\n", dlerror());
        return;
    }
    *(void**)&get = dlsym(lib, "lv2_descriptor");
    check("exports lv2_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor has the plugin's URI", d && strcmp(d->URI, BN_URI) == 0);
    check("only one plugin in the library", get(1) == NULL);
    h = d->instantiate(d, 48000.0, bundle, NULL);
    check("instantiates at 48 kHz", h != NULL);
    if (!h)
        return;
    memcpy(ctl, DEFAULTS, sizeof ctl);
    for (i = 2; i < PORT_COUNT; ++i)
        d->connect_port(h, (uint32_t)i, &ctl[i]);
    d->connect_port(h, PORT_IN, in);
    d->connect_port(h, PORT_OUT, out);
    d->activate(h);
    for (block = 0; block < 200; ++block) {
        if (block == 100)
            ctl[PORT_MODE] = 1; /* switch to sample and hold halfway */
        for (i = 0; i < 512; ++i)
            in[i] = 0.2f * (float)sin(2 * 3.14159265 * 110.0 * (block * 512 + i) / 48000.0);
        d->run(h, 512);
        for (i = 0; i < 512; ++i) {
            if (!isfinite(out[i]))
                finite = 0;
            if (block >= 100)
                peak_od = fmax(peak_od, fabs(out[i]));
            else
                peak_clean = fmax(peak_clean, fabs(out[i]));
        }
    }
    printf("         output peak: envelope %.3f, sample and hold %.3f\n", peak_clean, peak_od);
    check("runs 2 s of a 110 Hz tone in both modes with finite, non-silent output",
          finite && peak_clean > 1e-3 && peak_od > 1e-3 && peak_clean < 4.0 && peak_od < 4.0);
    printf("         latency port reports %.0f samples\n", ctl[PORT_LATENCY]);
    check("reports its latency", ctl[PORT_LATENCY] == 32.0f);
    d->deactivate(h);
    d->cleanup(h);
    dlclose(lib);
}

static void check_ui(const char* bundle, const char* ext)
{
    char path[1024];
    void* lib;
    const UiDesc* (*get)(uint32_t);
    const UiDesc* d;
    const IdleIface* idle;
    void* ui;
    void* widget = NULL;
    int i;

    snprintf(path, sizeof path, "%s/black_napkin_ui%s", bundle, ext);
    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    check("GUI library loads", lib != NULL);
    if (!lib) {
        printf("  %s\n", dlerror());
        return;
    }
    *(void**)&get = dlsym(lib, "lv2ui_descriptor");
    check("exports lv2ui_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor has the GUI's URI", d && strcmp(d->URI, BN_UI_URI) == 0);
    check("refuses another plugin's URI",
          d->instantiate(d, "urn:other", bundle, on_write, NULL, &widget, NULL) == NULL);
    ui = d->instantiate(d, BN_URI, bundle, on_write, NULL, &widget, NULL);
    check("instantiates a top-level window (no parent given)", ui != NULL && widget != NULL);
    if (!ui)
        return;
    for (i = PORT_RANGE; i < PORT_COUNT; ++i)
        d->port_event(ui, (uint32_t)i, sizeof(float), 0, &DEFAULTS[i]);
    idle = (const IdleIface*)d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface");
    check("offers the idle interface", idle != NULL);
    if (idle) {
        int ok = 1;
        for (i = 0; i < 20; ++i)
            ok &= idle->idle(ui) == 0;
        check("idle runs and keeps the window open", ok);
    }
    d->cleanup(ui);
    check("cleanup returns", 1);
    dlclose(lib);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("usage: load_check <bundle folder> <.so|.dylib> [--no-ui]\n");
        return 2;
    }
    check_plugin(argv[1], argv[2]);
    if (argc < 4 || strcmp(argv[3], "--no-ui") != 0)
        check_ui(argv[1], argv[2]);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
