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

// state_gen_names with n == 0 must be a no-op: it must not crash and must not
// touch `out`. The implementation takes an early-return branch for n <= 0 and,
// because the inner zeroing guard is gated on `n > 0`, leaves `out` untouched
// (unlike the null-g_state path which would zero it). Pre-fill `out` with
// non-zero sentinels so any accidental write is detectable.
TEST_F(StateTest, GenNamesZeroCountIsSafe) {
    GLuint out[4] = {0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u, 0x9ABCDEF0u};

    // n == 0: contract is "do nothing, leave out unchanged".
    mithril::state_gen_names("texture", 0, out);
    EXPECT_EQ(out[0], 0xDEADBEEFu);
    EXPECT_EQ(out[1], 0xCAFEBABEu);
    EXPECT_EQ(out[2], 0x12345678u);
    EXPECT_EQ(out[3], 0x9ABCDEF0u);

    // The allocator counter must not advance for a zero-count request.
    GLuint probe[1] = {0};
    mithril::state_gen_names("texture", 1, probe);
    EXPECT_EQ(probe[0], 1u) << "nextName should still start at 1 after a zero-count call";
}

// The error slot is a single sticky flag, not an unbounded FIFO: when an error
// is already pending, state_set_error drops every subsequent error. Pushing
// 1000 errors therefore yields exactly one retrievable error (the first),
// after which the slot is empty. This matches GL's sticky-error semantics and
// confirms the queue does NOT grow without bound.
TEST_F(StateTest, ErrorQueueDoesNotOverflowUnbounded) {
    // Flood the slot with 1000 distinct error codes while one is pending.
    mithril::state_set_error(GL_INVALID_OPERATION);
    for (int i = 0; i < 1000; ++i) {
        // Cycle through a handful of valid GL error codes; all but the first
        // must be dropped because the slot is already occupied.
        GLenum codes[4] = {GL_INVALID_ENUM, GL_INVALID_VALUE,
                           GL_OUT_OF_MEMORY, GL_STACK_OVERFLOW};
        mithril::state_set_error(codes[i % 4]);
    }

    // Only the first error is retrievable — the queue never grew past 1.
    EXPECT_EQ(mithril::state_take_error(), GL_INVALID_OPERATION);
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);

    // After draining, the slot accepts a fresh error (capacity is restored to
    // 1, not exhausted by the flood).
    mithril::state_set_error(GL_OUT_OF_MEMORY);
    EXPECT_EQ(mithril::state_take_error(), GL_OUT_OF_MEMORY);
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);
}

// state_gen_names draws from a single shared monotonically-increasing counter
// (the `kind` argument is ignored by the implementation), so two consecutive
// allocations for different target types must never produce colliding names.
TEST_F(StateTest, GenNamesProducesDistinctValues) {
    constexpr GLsizei kN = 4;
    GLuint bufNames[kN] = {0};
    GLuint texNames[kN] = {0};

    mithril::state_gen_names("buffer", kN, bufNames);
    mithril::state_gen_names("texture", kN, texNames);

    // Within each batch, names are strictly increasing and non-zero.
    for (GLsizei i = 0; i < kN; ++i) {
        EXPECT_NE(bufNames[i], 0u) << "buffer name " << i << " is zero";
        EXPECT_NE(texNames[i], 0u) << "texture name " << i << " is zero";
        if (i > 0) {
            EXPECT_GT(bufNames[i], bufNames[i - 1]) << "buffer names must increase";
            EXPECT_GT(texNames[i], texNames[i - 1]) << "texture names must increase";
        }
    }

    // No buffer name may equal any texture name (no cross-target collision).
    for (GLsizei i = 0; i < kN; ++i) {
        for (GLsizei j = 0; j < kN; ++j) {
            EXPECT_NE(bufNames[i], texNames[j])
                << "collision: bufNames[" << i << "] == texNames[" << j << "] == " << bufNames[i];
        }
    }

    // Because the counter is shared, every texture name must be strictly
    // greater than every buffer name (texture batch was allocated second).
    for (GLsizei j = 0; j < kN; ++j) {
        EXPECT_GT(texNames[j], bufNames[kN - 1])
            << "texNames[" << j << "] should exceed the last buffer name";
    }
}
