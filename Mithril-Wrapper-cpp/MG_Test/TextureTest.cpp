// Mithril-Wrapper - MG_Test/TextureTest.cpp
// Unit tests for the pure-logic GL entry points in MG_Impl/Texture.cpp.
//
// Covers the GL_PROXY_TEXTURE_2D size probe in glTexImage2D (valid/invalid
// branches around the 16384 max-texture-size threshold), immutable-storage
// metadata pinned by glTexStorage2D, glTexParameter* routing into the Texture
// struct (min filter / border colour), and glBindTexture tracking of the
// active texture unit. The functions under test operate on the global
// mithril::g_state pointer and delegate to backend_* stubs (BackendStub.cpp)
// for the Vulkan side, so a per-test fixture installs a fresh GLState in SetUp
// and releases it in TearDown — mirroring StateTest.cpp.
#include <gtest/gtest.h>

#include "../MG_State/State.h"

#include <GL/gl.h>

namespace {

// Each test gets a fresh, independent GLState installed as the global pointer.
// Texture.cpp's entry points reach into g_state directly (object tables, the
// active texture unit, the proxy-texture probe slot), so a clean state per test
// is required to keep cases independent.
class TextureTest : public ::testing::Test {
protected:
    void SetUp() override {
        mithril::g_state = mithril::state_create();
        ASSERT_NE(mithril::g_state, nullptr);
    }
    void TearDown() override {
        mithril::state_destroy(mithril::g_state);
        mithril::g_state = nullptr;
    }
};

} // namespace

// glTexImage2D(GL_PROXY_TEXTURE_2D, ...) doesn't create a real texture; it
// records the requested dimensions on g_state->proxyTexture2D so a subsequent
// glGetTexLevelParameteriv query can report them (Minecraft probes max texture
// size this way). A size within the reported GL_MAX_TEXTURE_SIZE (16384) is
// accepted; an oversized probe is rejected and width/height collapse to 0
// (the GL "unsupported" sentinel).
TEST_F(TextureTest, ProxyTextureSizeProbe) {
    // Within the 16384 threshold: probe is accepted.
    glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA8,
                 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    EXPECT_TRUE(mithril::g_state->proxyTexture2D.valid);
    EXPECT_EQ(mithril::g_state->proxyTexture2D.width, 512);
    EXPECT_EQ(mithril::g_state->proxyTexture2D.height, 512);
    EXPECT_EQ(mithril::g_state->proxyTexture2D.internalFormat, GL_RGBA8);

    // Oversized (32768 > 16384): probe is rejected, dimensions collapse to 0.
    glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA8,
                 32768, 32768, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    EXPECT_FALSE(mithril::g_state->proxyTexture2D.valid);
    EXPECT_EQ(mithril::g_state->proxyTexture2D.width, 0);
    EXPECT_EQ(mithril::g_state->proxyTexture2D.height, 0);
}

// glTexStorage2D allocates immutable storage and pins the GL-level metadata
// (internalFormat, dimensions, level count) on the currently-bound texture.
// depth is forced to 1 for a 2D texture. The backend_* call is stubbed, so we
// only assert the pure-logic state mutations.
TEST_F(TextureTest, TexStorageSetsImmutableMetadata) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    ASSERT_NE(tex, 0u);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexStorage2D(GL_TEXTURE_2D, 3, GL_RGBA8, 256, 128);

    mithril::Texture* t = mithril::state_get_texture(tex);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->levels, 3);
    EXPECT_EQ(t->width, 256);
    EXPECT_EQ(t->height, 128);
    EXPECT_EQ(t->depth, 1);
    EXPECT_EQ(t->internalFormat, GL_RGBA8);
}

// glTexParameteri routes through glTexParameterf and dispatches on pname into
// the bound texture's filter/wrap fields; glTexParameterfv with
// GL_TEXTURE_BORDER_COLOR copies all four floats verbatim. The backend_*
// parameter sync is stubbed, so we only assert the in-state routing.
TEST_F(TextureTest, TexParameterRouting) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    const GLfloat border[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    mithril::Texture* t = mithril::state_get_texture(tex);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->minFilter, GL_LINEAR);
    EXPECT_FLOAT_EQ(t->borderColor[0], 0.1f);
    EXPECT_FLOAT_EQ(t->borderColor[1], 0.2f);
    EXPECT_FLOAT_EQ(t->borderColor[2], 0.3f);
    EXPECT_FLOAT_EQ(t->borderColor[3], 0.4f);
}

// glBindTexture records the binding on the active texture unit's slot
// (boundTextures[unit] / boundTextureTargets[unit]) and stamps the target onto
// the texture object. Setting activeTextureUnit = 5 before the bind must route
// the binding to slot 5, not the default slot 0.
TEST_F(TextureTest, BindTextureTracksActiveUnit) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    ASSERT_NE(tex, 0u);

    mithril::g_state->activeTextureUnit = 5;
    glBindTexture(GL_TEXTURE_2D, tex);

    EXPECT_EQ(mithril::g_state->boundTextures[5], tex);
    EXPECT_EQ(mithril::g_state->boundTextureTargets[5], GL_TEXTURE_2D);

    mithril::Texture* t = mithril::state_get_texture(tex);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->target, GL_TEXTURE_2D);
}
