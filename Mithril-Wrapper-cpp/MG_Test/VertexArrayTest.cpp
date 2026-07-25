// Mithril-Wrapper - MG_Test/VertexArrayTest.cpp
// Unit tests for the vertex-array / vertex-attrib entry points in
// MG_Impl/VertexArray.cpp. VertexArray.cpp is the only MG_Impl translation
// unit with zero backend_* calls — every function here is pure state-machine
// logic over mithril::GLState, so the tests drive the GL entry points and then
// read the resulting state back through the MG_State accessors.
//
// Each test gets a fresh, independent GLState installed as the global pointer
// (same fixture pattern as StateTest), so VAO/attribute state never leaks
// across cases.
#include <gtest/gtest.h>

#include "../MG_State/State.h"

#include <GL/gl.h>

namespace {

// Mirrors StateTest: install a fresh GLState (pre-populated with the default
// VAO/FBO name 0) in SetUp, release it in TearDown so leftover state never
// leaks across tests.
class VertexArrayTest : public ::testing::Test {
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

// glVertexAttribPointer must record size / type / normalized / stride / pointer
// on the current VAO's attrib slot, force integer=false, and snapshot the
// GL_ARRAY_BUFFER bound at call time into boundBuffer.
TEST_F(VertexArrayTest, VertexAttribPointerStoresAllFields) {
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_TRUE, 24, (void*)12);

    mithril::VertexArray* v = mithril::state_get_vao(mithril::g_state->currentVAO);
    ASSERT_NE(v, nullptr);
    const mithril::VertexAttrib& a = v->attribs[2];
    EXPECT_EQ(a.size, 3);
    EXPECT_EQ(a.type, GL_FLOAT);
    EXPECT_EQ(a.normalized, true);
    EXPECT_EQ(a.integer, false);
    EXPECT_EQ(a.stride, 24);
    EXPECT_EQ(a.pointer, (const void*)12);
    EXPECT_EQ(a.boundBuffer, vbo);
}

// glVertexAttribIPointer is the integer flavour: it must set integer=true and
// normalized=false (the GL spec forbids normalisation for pure-int attrs).
TEST_F(VertexArrayTest, IPointerSetsIntegerFlag) {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glVertexAttribIPointer(1, 4, GL_INT, 0, nullptr);

    mithril::VertexArray* v = mithril::state_get_vao(mithril::g_state->currentVAO);
    ASSERT_NE(v, nullptr);
    const mithril::VertexAttrib& a = v->attribs[1];
    EXPECT_EQ(a.integer, true);
    EXPECT_EQ(a.normalized, false);
}

// glEnableVertexAttribArray rejects indices >= kMaxVertexAttribs with
// GL_INVALID_VALUE; a valid index flips the enabled flag and sets no error.
TEST_F(VertexArrayTest, EnableIndexBounds) {
    // Out-of-range index (17 >= kMaxVertexAttribs=16): record GL_INVALID_VALUE
    // and leave attribute state untouched.
    glEnableVertexAttribArray(17);
    EXPECT_EQ(mithril::state_take_error(), GL_INVALID_VALUE);

    // Valid index 0: enables attrib 0 on the current (default) VAO, no error.
    glEnableVertexAttribArray(0);
    mithril::VertexArray* v = mithril::state_get_vao(mithril::g_state->currentVAO);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->attribs[0].enabled, true);
    EXPECT_EQ(mithril::state_take_error(), GL_NO_ERROR);
}

// glBindAttribLocation records name->location overrides on the program; a
// second call for the same name replaces the previous location (GL spec:
// bindings take effect at link time, last-write-wins per name).
TEST_F(VertexArrayTest, BindAttribLocationOverridesMap) {
    GLuint prog = glCreateProgram();
    ASSERT_NE(prog, 0u);

    glBindAttribLocation(prog, 5, "Position");
    mithril::Program* p = mithril::state_get_program(prog);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->attribBindings["Position"], 5u);

    // Re-binding the same name must overwrite, not insert a duplicate.
    glBindAttribLocation(prog, 7, "Position");
    EXPECT_EQ(p->attribBindings["Position"], 7u);
}

// glDeleteVertexArrays must reset currentVAO to 0 when the currently-bound VAO
// is among the deleted names, so subsequent attrib ops never target a dangling
// VAO.
TEST_F(VertexArrayTest, DeleteResetsCurrentVao) {
    GLuint v = 0;
    glGenVertexArrays(1, &v);
    ASSERT_NE(v, 0u);
    glBindVertexArray(v);
    EXPECT_EQ(mithril::g_state->currentVAO, v);

    glDeleteVertexArrays(1, &v);
    EXPECT_EQ(mithril::g_state->currentVAO, 0u);
}
