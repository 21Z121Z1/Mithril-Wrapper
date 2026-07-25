// Mithril-Wrapper - MG_Test/LookupTest.cpp
// Unit tests for the dlsym-based symbol lookup in MG_Impl/lookup.cpp.
//
// glXGetProcAddress / glXGetProcAddressARB resolve a GL entry-point name
// against the test binary's own symbol table (dlsym(RTLD_DEFAULT)). The lookup
// is stateless — it touches no global GL state — so these tests use plain
// TEST() macros with no fixture. <GL/gl.h> pulls in glcorearb.h, which
// declares both entry points with the signature implemented in lookup.cpp:
//     void* glXGetProcAddress(const char* name);
//     void* glXGetProcAddressARB(const char* name);
#include <gtest/gtest.h>

#include <GL/gl.h>

// glXGetProcAddress resolves a known Core Profile entry point (glGenBuffers is
// exported by the linked Buffer.cpp TU) to a non-null function pointer.
TEST(Lookup, ResolvesKnownGlEntry) {
    void* p = glXGetProcAddress("glGenBuffers");
    EXPECT_NE(p, nullptr);
}

// Unknown names must return nullptr — the GLX spec requires that probing for an
// unsupported symbol yields NULL rather than a stub.
TEST(Lookup, UnknownReturnsNull) {
    void* p = glXGetProcAddress("glXDoesNotExist_xyz");
    EXPECT_EQ(p, nullptr);
}

// A null name must be guarded (lookup_symbol checks !name up front) and must
// not reach dlsym, which would otherwise crash on a null pointer.
TEST(Lookup, NullNameSafe) {
    void* p = glXGetProcAddress(nullptr);
    EXPECT_EQ(p, nullptr);
}

// glXGetProcAddressARB is an alias that routes through the same lookup_symbol
// path, so it must return the identical function pointer for a given name.
TEST(Lookup, ArbAlias) {
    void* a = glXGetProcAddressARB("glGenBuffers");
    void* b = glXGetProcAddress("glGenBuffers");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
}
