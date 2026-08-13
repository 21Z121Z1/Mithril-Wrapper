#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <stdio.h>

using surface_create_fn = void* (*)(void*, int*, int*);
using surface_get_size_fn = bool (*)(void*, int*, int*);
using surface_destroy_fn = void (*)(void*);

static int failures = 0;
#define CHECK(c, fmt, ...) do { if (c) printf("ok : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    auto surface_create = (surface_create_fn)dlsym(h, "surface_create");
    auto surface_get_size = (surface_get_size_fn)dlsym(h, "surface_get_size");
    auto surface_destroy = (surface_destroy_fn)dlsym(h, "surface_destroy");
    CHECK(surface_create && surface_get_size && surface_destroy, "surface Metal entry points exported");
    if (!surface_create || !surface_get_size || !surface_destroy) return 1;

    @autoreleasepool {
        CALayer* parent = [CALayer layer];
        parent.bounds = CGRectMake(0, 0, 100, 50);
        parent.contentsScale = 2.0;
        CHECK(![parent isKindOfClass:[CAMetalLayer class]], "control parent starts as plain CALayer");
        int w = 0, hgt = 0;
        void* out = surface_create((__bridge void*)parent, &w, &hgt);
        CAMetalLayer* metal = (__bridge CAMetalLayer*)out;
        CHECK(metal && [metal isKindOfClass:[CAMetalLayer class]], "plain CALayer gets a genuine CAMetalLayer target");
        CHECK(![parent isKindOfClass:[CAMetalLayer class]], "parent object class was not mutated");
        CHECK(metal != (CAMetalLayer*)parent, "fallback uses a child layer, not object_setClass");
        CHECK(metal.superlayer == parent, "fallback CAMetalLayer retained by parent layer");
        CHECK(w == 200 && hgt == 100, "drawableSize derives pixels from bounds*contentsScale (%dx%d)", w, hgt);
        int qw = 0, qh = 0;
        CHECK(surface_get_size((__bridge void*)metal, &qw, &qh) && qw == 200 && qh == 100,
              "surface_get_size reports pixel drawable size (%dx%d)", qw, qh);
        surface_destroy((__bridge void*)metal);
        CHECK(metal.superlayer == nil, "owned fallback CAMetalLayer detaches on surface_destroy");

        CAMetalLayer* existing = [CAMetalLayer layer];
        existing.bounds = CGRectMake(0, 0, 80, 40);
        existing.contentsScale = 3.0;
        existing.drawableSize = CGSizeMake(123, 77);  // host-controlled dynamic resolution
        int ew = 0, eh = 0;
        void* same = surface_create((__bridge void*)existing, &ew, &eh);
        CHECK((__bridge CAMetalLayer*)same == existing, "existing CAMetalLayer is reused");
        CHECK(ew == 123 && eh == 77, "existing nonzero drawableSize is preserved (%dx%d)", ew, eh);
        surface_destroy((__bridge void*)existing);
        CHECK((__bridge CAMetalLayer*)same == existing,
              "surface_destroy never replaces or invalidates host CAMetalLayer");
    }
    dlclose(h);
    printf("SURFACE METAL SMOKE: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}