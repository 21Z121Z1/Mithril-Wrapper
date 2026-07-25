// Mithril-Wrapper - MG_Test/BufferTest.cpp
// Unit tests for the pure-logic buffer object paths in MG_Impl/Buffer.cpp.
//
// Covers glGenBuffers / glBindBuffer / glBufferData / glBufferSubData /
// glGetBufferParameteriv / glGetBufferSubData / glMapBufferRange, plus the
// GL_ELEMENT_ARRAY_BUFFER -> VAO binding rule. The Vulkan backend_* calls
// inside Buffer.cpp are stubbed (BackendStub.cpp) so only the CPU-side shadow
// storage in mithril::Buffer::data and the GL state machine transitions are
// exercised. A per-test fixture installs a fresh GLState (via state_create) on
// mithril::g_state so each case is independent; TearDown releases it and
// nulls g_state so leftover pointers never leak across tests.
#include <gtest/gtest.h>

#include "../MG_State/State.h"

#include <GL/gl.h>

#include <cstdint>
#include <cstring>

namespace {

// Each test gets a fresh, independent GLState installed as the global pointer,
// mirroring the StateTest fixture. state_create() pre-populates VAO 0 / FBO 0;
// TearDown releases the state and clears g_state so leftover pointers never
// leak across tests.
class BufferTest : public ::testing::Test {
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

// glGenBuffers + glBindBuffer(GL_ARRAY_BUFFER) + glBufferData should install a
// shadow copy in mithril::Buffer::data, record size/usage, and round-trip
// back through glGetBufferParameteriv and glGetBufferSubData.
TEST_F(BufferTest, GenBindDataRoundTrip) {
    GLuint id = 0;
    glGenBuffers(1, &id);
    ASSERT_NE(id, 0u);

    glBindBuffer(GL_ARRAY_BUFFER, id);

    const uint8_t payload[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    glBufferData(GL_ARRAY_BUFFER, 16, payload, GL_STATIC_DRAW);

    GLint sizeParam = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &sizeParam);
    EXPECT_EQ(sizeParam, 16);

    GLint usageParam = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &usageParam);
    EXPECT_EQ(usageParam, static_cast<GLint>(GL_STATIC_DRAW));

    uint8_t readback[16] = {};
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 16, readback);
    EXPECT_EQ(std::memcmp(readback, payload, 16), 0);
}

// glBufferSubData with offset + size > buffer size must raise GL_INVALID_VALUE
// (observable via state_take_error) and leave the shadow untouched; an
// in-range write updates the shadow readable through glGetBufferSubData.
TEST_F(BufferTest, SubDataBoundsAndShadow) {
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);

    const uint8_t payload[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    glBufferData(GL_ARRAY_BUFFER, 8, payload, GL_STATIC_DRAW);

    // Drain any pending error to start the assertion from a clean slate.
    mithril::state_take_error();

    // Out-of-bounds write: offset 6 + size 4 = 10 > 8.
    const uint8_t oob[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    glBufferSubData(GL_ARRAY_BUFFER, 6, 4, oob);
    EXPECT_EQ(mithril::state_take_error(), GL_INVALID_VALUE);

    // Shadow data must be unchanged.
    uint8_t readback[8] = {};
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 8, readback);
    EXPECT_EQ(std::memcmp(readback, payload, 8), 0);

    // A valid in-range write updates the shadow at the right offset.
    const uint8_t patch[2] = {0xEE, 0xFF};
    glBufferSubData(GL_ARRAY_BUFFER, 2, 2, patch);
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);

    uint8_t patchReadback[2] = {};
    glGetBufferSubData(GL_ARRAY_BUFFER, 2, 2, patchReadback);
    EXPECT_EQ(patchReadback[0], 0xEE);
    EXPECT_EQ(patchReadback[1], 0xFF);
}

// glMapBufferRange with out-of-bounds offset/length returns nullptr and raises
// GL_INVALID_VALUE; GL_MAP_INVALIDATE_BUFFER_BIT zeroes the shadow; the
// returned pointer addresses the requested offset within the shadow.
TEST_F(BufferTest, MapRangeBoundsAndInvalidate) {
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);

    const uint8_t payload[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    glBufferData(GL_ARRAY_BUFFER, 8, payload, GL_STATIC_DRAW);

    mithril::state_take_error(); // drain

    // Out-of-bounds map: offset 4 + length 8 = 12 > 8.
    void* p = glMapBufferRange(GL_ARRAY_BUFFER, 4, 8, GL_MAP_READ_BIT);
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(mithril::state_take_error(), GL_INVALID_VALUE);

    // GL_MAP_INVALIDATE_BUFFER_BIT must zero the entire shadow.
    p = glMapBufferRange(GL_ARRAY_BUFFER, 0, 8,
                         GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);

    uint8_t zeroed[8] = {};
    std::memset(zeroed, 0xFF, sizeof(zeroed)); // poison, then read back
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 8, zeroed);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(zeroed[i], 0u) << "byte " << i << " should be zeroed";
    }

    // Reload a known pattern and verify the returned pointer is at offset.
    glBufferData(GL_ARRAY_BUFFER, 8, payload, GL_STATIC_DRAW);
    uint8_t* mapped = static_cast<uint8_t*>(
        glMapBufferRange(GL_ARRAY_BUFFER, 4, 4, GL_MAP_READ_BIT));
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(mapped[0], 4u); // payload[4]
    EXPECT_EQ(mapped[1], 5u); // payload[5]
}

// glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib) must record ib on the currently
// bound VAO's elementArrayBuffer slot AND mirror it on
// g_state->currentIndexBuffer.
TEST_F(BufferTest, ElementArrayBindsToVao) {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    ASSERT_NE(vao, 0u);
    glBindVertexArray(vao);

    GLuint ib = 0;
    glGenBuffers(1, &ib);
    ASSERT_NE(ib, 0u);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);

    mithril::VertexArray* vaoObj = mithril::state_get_vao(vao);
    ASSERT_NE(vaoObj, nullptr);
    EXPECT_EQ(vaoObj->elementArrayBuffer, ib);
    EXPECT_EQ(mithril::g_state->currentIndexBuffer, ib);
}
