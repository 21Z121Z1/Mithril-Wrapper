// Mithril-Wrapper - MG_Test/StateTest.cpp
// Unit tests for the pure-logic GL state machine helpers in MG_State/State.cpp.
//
// Covers state_create / state_destroy, the pre-populated default VAO/FBO,
// the name allocator (state_gen_names), the object-table getters, and the
// error FIFO (state_set_error / state_take_error). The functions under test
// operate on the global mithril::g_state pointer, so a per-test fixture
// installs a fresh GLState in SetUp and releases it in TearDown.
#include <gtest/gtest.h>

#include "../MG_State/State.h"

#include <GL/gl.h>

namespace {

// Each test gets a fresh, independent GLState installed as the global pointer.
// state_create() pre-populates VAO 0 and FBO 0; TearDown releases the state
// and clears g_state so leftover pointers never leak across tests.
class StateTest : public ::testing::Test {
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

// state_create returns a non-null pointer; state_destroy must accept nullptr
// without crashing (the EGL layer relies on this for half-initialised paths).
TEST_F(StateTest, CreateReturnsNonNullAndDestroyAcceptsNullptr) {
    mithril::GLState* s = mithril::state_create();
    ASSERT_NE(s, nullptr);
    mithril::state_destroy(s);
    // Safe no-op on nullptr.
    mithril::state_destroy(nullptr);
}

// state_create pre-populates the default VAO (name 0) and default framebuffer
// (name 0) so GL commands issued before any glGen* call have valid targets.
TEST_F(StateTest, CreatePrePopulatesDefaultVaoAndFbo) {
    mithril::VertexArray* vao = mithril::state_get_vao(0);
    ASSERT_NE(vao, nullptr);
    EXPECT_EQ(vao->id, 0u);

    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(0);
    ASSERT_NE(fbo, nullptr);
    EXPECT_EQ(fbo->id, 0u);
}

// state_gen_names fills `out` with n strictly increasing, non-zero names.
TEST_F(StateTest, GenNamesProducesIncreasingNonZeroNames) {
    constexpr GLsizei kN = 5;
    GLuint names[kN] = {0};
    mithril::state_gen_names("buffer", kN, names);
    for (GLsizei i = 0; i < kN; ++i) {
        EXPECT_NE(names[i], 0u) << "name at index " << i << " must be non-zero";
        if (i > 0) {
            EXPECT_GT(names[i], names[i - 1]) << "names must be strictly increasing";
        }
    }
}

// state_gen_names is a no-op for n <= 0 and for a null out pointer, and must
// not crash. (The GL spec allows glGen*(0, ...) with a null pointer.)
TEST_F(StateTest, GenNamesSafeForZeroCountOrNullptr) {
    GLuint dummy[4] = {0};

    mithril::state_gen_names("buffer", 0, dummy);
    EXPECT_EQ(dummy[0], 0u);

    mithril::state_gen_names("buffer", -3, dummy);
    EXPECT_EQ(dummy[0], 0u);

    // null out pointer: must not dereference.
    mithril::state_gen_names("buffer", 4, nullptr);
}

// state_get_buffer / texture / shader / program return nullptr for id 0
// (0 is reserved for the default/unnamed object in each table).
TEST_F(StateTest, GettersReturnNullptrForIdZero) {
    EXPECT_EQ(mithril::state_get_buffer(0), nullptr);
    EXPECT_EQ(mithril::state_get_texture(0), nullptr);
    EXPECT_EQ(mithril::state_get_shader(0), nullptr);
    EXPECT_EQ(mithril::state_get_program(0), nullptr);
}

// state_get_buffer returns nullptr for an id that was never created (the table
// only holds entries inserted by glGen* / glBind*).
TEST_F(StateTest, GettersReturnNullptrForUncreatedId) {
    EXPECT_EQ(mithril::state_get_buffer(123u), nullptr);
    EXPECT_EQ(mithril::state_get_texture(456u), nullptr);
    EXPECT_EQ(mithril::state_get_shader(789u), nullptr);
    EXPECT_EQ(mithril::state_get_program(0xdeadu), nullptr);
}

// Error FIFO semantics: the first error set is retained until taken; a second
// set while an error is pending is dropped; take returns the pending error and
// clears the slot; a subsequent take returns GL_NO_ERROR.
TEST_F(StateTest, SetAndTakeErrorSemantics) {
    // Initially no error.
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);

    mithril::state_set_error(GL_INVALID_ENUM);
    // A second set while an error is pending must NOT overwrite the first.
    mithril::state_set_error(GL_INVALID_VALUE);

    EXPECT_EQ(mithril::state_take_error(), GL_INVALID_ENUM);
    // Taking clears the slot.
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);

    // After clearing, a fresh set/take round-trip works.
    mithril::state_set_error(GL_OUT_OF_MEMORY);
    EXPECT_EQ(mithril::state_take_error(), GL_OUT_OF_MEMORY);
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);
}
