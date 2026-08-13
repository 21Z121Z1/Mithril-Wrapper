#!/usr/bin/env python3
"""
Apply critical fixes to Mithril-Wrapper for Minecraft 1.21.1 compatibility.

Fixes:
1. GL46_Compat.cpp: Implement glCreateProgramPipelines, glCreateQueries, 
   glCreateTransformFeedbacks (were returning zeros, causing MC capability check failures)
2. Getter.cpp: Remove GL_EXT_direct_state_access (not fully implemented)
3. State.h: Set reasonable viewport/scissor defaults (were 0,0,0,0)
"""
import os
import re

BASE = os.path.dirname(os.path.abspath(__file__))
IMPL = os.path.join(BASE, "Mithril-Wrapper-cpp", "MG_Impl")
STATE = os.path.join(BASE, "Mithril-Wrapper-cpp", "MG_State")

# =====================================================================
# Fix 1: GL46_Compat.cpp - Replace empty create functions with real implementations
# =====================================================================
gl46_path = os.path.join(IMPL, "GL46_Compat.cpp")
with open(gl46_path, "r") as f:
    gl46 = f.read()

# Fix glCreateProgramPipelines - delegate to glGenProgramPipelines
old_pp = '''/* 32. glCreateProgramPipelines - Return zeros (no program pipeline support). */
void glCreateProgramPipelines(GLsizei n, GLuint* pipelines) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !pipelines) return;
    for (GLsizei i = 0; i < n; ++i) pipelines[i] = 0;
}'''
new_pp = '''/* 32. glCreateProgramPipelines - DSA: generate pipeline names.
 * Program pipelines are not fully supported on MoltenVK (no separate shader
 * stages), but we return valid names so LWJGL capability checks succeed and
 * Minecraft can start. glBindProgramPipeline / glUseProgramStages are no-ops. */
void glCreateProgramPipelines(GLsizei n, GLuint* pipelines) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !pipelines) return;
    glGenProgramPipelines(n, pipelines);
}'''
assert old_pp in gl46, "Could not find glCreateProgramPipelines"
gl46 = gl46.replace(old_pp, new_pp)

# Fix glCreateQueries - delegate to glGenQueries
old_cq = '''/* 33. glCreateQueries - Return zeros. */
void glCreateQueries(GLenum target, GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
}'''
new_cq = '''/* 33. glCreateQueries - DSA: generate query names.
 * Delegates to glGenQueries which creates real Query objects in the state map.
 * This ensures glBeginQuery/glEndQuery work correctly for occlusion queries
 * (used by Iris/Sodium for culling). */
void glCreateQueries(GLenum target, GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    if (n <= 0 || !ids) return;
    glGenQueries(n, ids);
}'''
assert old_cq in gl46, "Could not find glCreateQueries"
gl46 = gl46.replace(old_cq, new_cq)

# Fix glCreateTransformFeedbacks - delegate to glGenTransformFeedbacks
old_tf = '''/* 35. glCreateTransformFeedbacks - Return zeros. */
void glCreateTransformFeedbacks(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
}'''
new_tf = '''/* 35. glCreateTransformFeedbacks - DSA: generate transform feedback names.
 * Delegates to glGenTransformFeedbacks which creates real TransformFeedback
 * objects in the state map. */
void glCreateTransformFeedbacks(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    glGenTransformFeedbacks(n, ids);
}'''
assert old_tf in gl46, "Could not find glCreateTransformFeedbacks"
gl46 = gl46.replace(old_tf, new_tf)

with open(gl46_path, "w") as f:
    f.write(gl46)
print("✓ GL46_Compat.cpp patched")

# =====================================================================
# Fix 2: Getter.cpp - Remove GL_EXT_direct_state_access (not fully implemented)
# Keep GL_ARB_direct_state_access since most DSA functions ARE implemented
# =====================================================================
getter_path = os.path.join(IMPL, "Getter.cpp")
with open(getter_path, "r") as f:
    getter = f.read()

# Remove GL_EXT_direct_state_access line
# This extension is the EXT variant with different function signatures
# that are NOT implemented. GL_ARB_direct_state_access is the one we support.
getter = getter.replace('    "GL_EXT_direct_state_access",\n', '')

# Also remove GL_ARB_transform_feedback2 and GL_ARB_transform_feedback3
# since transform feedback is not fully functional on MoltenVK
# Actually, let's keep them since glGenTransformFeedbacks etc. exist.
# The key issue is only GL_EXT_direct_state_access.

with open(getter_path, "w") as f:
    f.write(getter)
print("✓ Getter.cpp patched (removed GL_EXT_direct_state_access)")

# =====================================================================
# Fix 3: State.h - Set reasonable viewport/scissor defaults
# =====================================================================
state_path = os.path.join(STATE, "State.h")
with open(state_path, "r") as f:
    state = f.read()

# Fix viewport defaults - use 1280x720 as safe fallback
# These will be overwritten by the first glViewport call, but MC needs
# non-zero values before that happens
old_viewport = '''    GLint   viewportX = 0, viewportY = 0;
    GLsizei viewportW = 0, viewportH = 0;
    GLint   scissorX = 0, scissorY = 0;
    GLsizei scissorW = 0, scissorH = 0;'''
new_viewport = '''    // Default viewport to 1280x720 so MC has a valid render area before
    // the first glViewport call. Without this, viewportW/H=0 causes the
    // Vulkan render area to be 0x0 → nothing draws → red/black screen.
    GLint   viewportX = 0, viewportY = 0;
    GLsizei viewportW = 1280, viewportH = 720;
    GLint   scissorX = 0, scissorY = 0;
    GLsizei scissorW = 1280, scissorH = 720;'''
assert old_viewport in state, "Could not find viewport defaults"
state = state.replace(old_viewport, new_viewport)

with open(state_path, "w") as f:
    f.write(state)
print("✓ State.h patched (viewport defaults 1280x720)")

print("\nAll fixes applied successfully!")
