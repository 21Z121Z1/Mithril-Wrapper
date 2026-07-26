// Mithril-Wrapper - MG_Test/EGLSyncTest.cpp
// Unit tests for the EGL 1.5 Sync API shadow implementation in egl/egl.cpp.
//
// The sync objects here are a *shadow* (state-layer) implementation: no real
// VkFence / VkSemaphore is created. eglCreateSync mints a process-local
// handle recorded in g_syncs; eglClientWaitSync / eglWaitSync return
// immediately (the sync is always EGL_SIGNALED); eglGetSyncAttrib reports
// the recorded type / condition / status. These tests exercise the public
// egl* C entry points through <EGL/egl.h>, checking the success paths and
// the EGL error codes (queried via eglGetError) for each failure path.
//
// Note on display handling: eglGetDisplay(EGL_DEFAULT_DISPLAY) returns the
// singleton Vulkan-backed display. valid_display() in egl.cpp only checks
// pointer identity against that singleton — it does NOT require the display
// to be eglInitialize'd. The test environment stubs the Vulkan backend
// (BackendStub.cpp returns backend_available()==0), so eglInitialize would
// fail with EGL_NOT_INITIALIZED; the tests therefore use the raw
// eglGetDisplay handle directly, which is sufficient for the shadow sync
// paths that only consult valid_display().
#include <gtest/gtest.h>

#include <EGL/egl.h>

#include <cstdint>

namespace {

// Fixture that hands each test the singleton EGLDisplay. There is no per-test
// sync table to reset: g_syncs lives in an anonymous namespace inside egl.cpp
// and is not directly accessible, but sync handles are unique monotonically
// increasing integers, so leftover entries from a failed test cannot collide
// with handles minted later. Each test creates and destroys its own sync(s).
class EGLSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        ASSERT_NE(dpy_, EGL_NO_DISPLAY);
    }
    EGLDisplay dpy() const { return dpy_; }

private:
    EGLDisplay dpy_ = EGL_NO_DISPLAY;
};

} // namespace

// Scenario 1: eglCreateSync(EGL_SYNC_FENCE) mints a non-null handle that
// eglDestroySync tears down. Destroying the same handle again must fail with
// EGL_BAD_SYNC_KHR (the handle is no longer present in g_syncs).
TEST_F(EGLSyncTest, CreateAndDestroyFenceSync) {
    EGLSync sync = eglCreateSync(dpy(), EGL_SYNC_FENCE, nullptr);
    EXPECT_NE(sync, EGL_NO_SYNC);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    EXPECT_EQ(eglDestroySync(dpy(), sync), EGL_TRUE);
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    // Second destroy: handle is gone -> EGL_FALSE + EGL_BAD_SYNC_KHR.
    EXPECT_EQ(eglDestroySync(dpy(), sync), EGL_FALSE);
    EXPECT_EQ(eglGetError(), EGL_BAD_SYNC_KHR);
}

// Scenario 2: shadow syncs are always signaled, so eglClientWaitSync returns
// EGL_CONDITION_SATISFIED immediately even with an EGL_FOREVER_KHR timeout
// and no flush flags.
TEST_F(EGLSyncTest, ClientWaitReturnsImmediately) {
    EGLSync sync = eglCreateSync(dpy(), EGL_SYNC_FENCE, nullptr);
    ASSERT_NE(sync, EGL_NO_SYNC);

    EGLint result = eglClientWaitSync(dpy(), sync, 0, EGL_FOREVER_KHR);
    EXPECT_EQ(result, static_cast<EGLint>(EGL_CONDITION_SATISFIED));
    EXPECT_EQ(eglGetError(), EGL_SUCCESS);

    eglDestroySync(dpy(), sync);
}

// Scenario 3: eglGetSyncAttrib reports the recorded shadow state — status is
// EGL_SIGNALED, type is EGL_SYNC_FENCE, condition is
// EGL_SYNC_PRIOR_COMMANDS_COMPLETE.
TEST_F(EGLSyncTest, QuerySyncAttrib) {
    EGLSync sync = eglCreateSync(dpy(), EGL_SYNC_FENCE, nullptr);
    ASSERT_NE(sync, EGL_NO_SYNC);

    EGLAttrib value = 0;

    EXPECT_EQ(eglGetSyncAttrib(dpy(), sync, EGL_SYNC_STATUS, &value), EGL_TRUE);
    EXPECT_EQ(value, static_cast<EGLAttrib>(EGL_SIGNALED));

    value = 0;
    EXPECT_EQ(eglGetSyncAttrib(dpy(), sync, EGL_SYNC_TYPE, &value), EGL_TRUE);
    EXPECT_EQ(value, static_cast<EGLAttrib>(EGL_SYNC_FENCE));

    value = 0;
    EXPECT_EQ(eglGetSyncAttrib(dpy(), sync, EGL_SYNC_CONDITION, &value), EGL_TRUE);
    EXPECT_EQ(value, static_cast<EGLAttrib>(EGL_SYNC_PRIOR_COMMANDS_COMPLETE));

    EXPECT_EQ(eglGetError(), EGL_SUCCESS);
    eglDestroySync(dpy(), sync);
}

// Scenario 4: only EGL_SYNC_FENCE is supported. EGL_SYNC_REUSABLE_KHR is
// rejected at create time with EGL_BAD_ATTRIBUTE and no handle is returned.
TEST_F(EGLSyncTest, RejectNonFenceType) {
    EGLSync sync = eglCreateSync(dpy(), EGL_SYNC_REUSABLE_KHR, nullptr);
    EXPECT_EQ(sync, EGL_NO_SYNC);
    EXPECT_EQ(eglGetError(), EGL_BAD_ATTRIBUTE);
}

// Scenario 5: an invalid display (EGL_NO_DISPLAY) is rejected with
// EGL_BAD_DISPLAY by every sync entry point; an unknown sync handle on a
// valid display is rejected with EGL_BAD_SYNC_KHR by eglClientWaitSync.
TEST_F(EGLSyncTest, InvalidDisplayAndSyncHandle) {
    // EGL_NO_DISPLAY is not the singleton -> EGL_BAD_DISPLAY on every path.
    // eglCreateSync returns EGL_NO_SYNC, boolean-returning entry points
    // return EGL_FALSE.
    EGLSync sync = eglCreateSync(EGL_NO_DISPLAY, EGL_SYNC_FENCE, nullptr);
    EXPECT_EQ(sync, EGL_NO_SYNC);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);

    // The sync handle argument is irrelevant here: valid_display() short-
    // circuits before g_syncs is consulted, so the call never looks at it.
    EGLSync dummy = reinterpret_cast<EGLSync>(static_cast<uintptr_t>(1));
    EXPECT_EQ(eglClientWaitSync(EGL_NO_DISPLAY, dummy, 0, 0), EGL_FALSE);
    EXPECT_EQ(eglGetError(), EGL_BAD_DISPLAY);

    // A handle never returned by eglCreateSync is unknown to g_syncs. Real
    // handles are small sequential integers, so a large bogus pointer cannot
    // collide with a live entry.
    EGLSync bogus = reinterpret_cast<EGLSync>(static_cast<uintptr_t>(0xDEADBEEF));
    EXPECT_EQ(eglClientWaitSync(dpy(), bogus, 0, EGL_FOREVER_KHR), EGL_FALSE);
    EXPECT_EQ(eglGetError(), EGL_BAD_SYNC_KHR);
}
