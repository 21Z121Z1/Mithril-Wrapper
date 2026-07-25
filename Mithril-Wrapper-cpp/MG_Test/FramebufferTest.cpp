// Mithril-Wrapper - MG_Test/FramebufferTest.cpp
// Unit tests for the pure-logic framebuffer entry points in MG_Impl/Framebuffer.cpp.
//
// Covers glGenFramebuffers defaults, glFramebufferTexture2D attachment routing
// (color/depth/stencil/depth+stencil), glDrawBuffers draw-buffer counting,
// glDrawBuffer GL_BACK/GL_NONE mapping, and mithril::collect_draw_fbo_attachments
// for the EGL default framebuffer (FBO 0). The functions under test operate on
// the global mithril::g_state pointer, so a per-test fixture installs a fresh
// GLState in SetUp and releases it in TearDown — mirroring StateTest.
#include <gtest/gtest.h>

#include "../MG_State/State.h"
#include "../MG_Impl/Framebuffer.h"

#include <GL/gl.h>

namespace {

// Each test gets a fresh, independent GLState installed as the global pointer.
// state_create() pre-populates the default framebuffer (name 0); TearDown
// releases the state and clears g_state so leftover pointers never leak across
// tests. The gl* entry points see the global via MITHRIL_ENSURE_INIT(), which
// is a no-op once g_state is non-null.
class FramebufferTest : public ::testing::Test {
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

// glGenFramebuffers seeds a new FBO with the GL Core defaults: a single draw
// buffer (GL_COLOR_ATTACHMENT0) and read buffer GL_COLOR_ATTACHMENT0.
TEST_F(FramebufferTest, GenFramebufferHasDefaults) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    ASSERT_NE(fbo, 0u);

    mithril::Framebuffer* fb = mithril::state_get_framebuffer(fbo);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->drawBuffers[0], GL_COLOR_ATTACHMENT0);
    EXPECT_EQ(fb->drawBufferCount, 1);
    EXPECT_EQ(fb->readBuffer, GL_COLOR_ATTACHMENT0);
}

// glFramebufferTexture2D routes by attachment enum: GL_COLOR_ATTACHMENTi ->
// colors[i], GL_DEPTH_ATTACHMENT -> depth, GL_DEPTH_STENCIL_ATTACHMENT ->
// BOTH depth and stencil.
TEST_F(FramebufferTest, AttachmentRouting) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 100, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, 101, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 102, 0);
    // Verify depth routing before the depth+stencil call overwrites depth.
    EXPECT_EQ(mithril::state_get_framebuffer(fbo)->depth.texture, 102u);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 103, 0);

    mithril::Framebuffer* fb = mithril::state_get_framebuffer(fbo);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->colors[0].texture, 100u);
    EXPECT_EQ(fb->colors[3].texture, 101u);
    // GL_DEPTH_STENCIL_ATTACHMENT writes BOTH depth and stencil.
    EXPECT_EQ(fb->depth.texture, 103u);
    EXPECT_EQ(fb->stencil.texture, 103u);
}

// glDrawBuffers copies the buffer list verbatim and sets drawBufferCount to the
// index of the last non-GL_NONE entry + 1 — so a middle NONE slot is counted as
// long as a later slot is non-NONE.
TEST_F(FramebufferTest, DrawBuffersCounting) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, bufs);

    mithril::Framebuffer* fb = mithril::state_get_framebuffer(fbo);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->drawBuffers[0], GL_COLOR_ATTACHMENT0);
    EXPECT_EQ(fb->drawBuffers[1], GL_NONE);
    EXPECT_EQ(fb->drawBuffers[2], GL_COLOR_ATTACHMENT2);
    EXPECT_EQ(fb->drawBufferCount, 3);
}

// glDrawBuffer maps the legacy single-buffer selectors: GL_BACK ->
// GL_COLOR_ATTACHMENT0 (count 1); GL_NONE clears the draw buffer set (count 0).
TEST_F(FramebufferTest, DrawBufferMapping) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glDrawBuffer(GL_BACK);
    mithril::Framebuffer* fb = mithril::state_get_framebuffer(fbo);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->drawBuffers[0], GL_COLOR_ATTACHMENT0);
    EXPECT_EQ(fb->drawBufferCount, 1);

    glDrawBuffer(GL_NONE);
    EXPECT_EQ(fb->drawBufferCount, 0);
}

// collect_draw_fbo_attachments, when the current draw FBO is 0 (the EGL default
// framebuffer), returns the swapchain VkImageViews installed on g_state and the
// EGL surface dimensions, reporting exactly one valid color attachment.
TEST_F(FramebufferTest, CollectAttachmentsFboZero) {
    mithril::g_state->eglDefaultColor = (VkImageView)0x1234;
    mithril::g_state->eglDefaultDepth = (VkImageView)0x5678;
    mithril::g_state->eglDefaultWidth  = 800;
    mithril::g_state->eglDefaultHeight = 600;
    mithril::g_state->currentDrawFBO = 0;

    VkImageView out_color[8] = {};
    VkImageView out_depth = VK_NULL_HANDLE;
    int out_w = 0, out_h = 0;
    int count = mithril::collect_draw_fbo_attachments(out_color, &out_depth, &out_w, &out_h);

    EXPECT_EQ(count, 1);
    EXPECT_EQ(out_color[0], (VkImageView)0x1234);
    EXPECT_EQ(out_depth, (VkImageView)0x5678);
    EXPECT_EQ(out_w, 800);
    EXPECT_EQ(out_h, 600);
}
