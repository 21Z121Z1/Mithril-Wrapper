#!/usr/bin/env python3
"""Deterministically harden high-value OpenGL 4.6 semantics.

This is intentionally a source-to-source migration with strict anchors.  It is
kept in-tree so the large GL46 compatibility translation unit can be changed
reproducibly and reviewed as a normal diff instead of by an opaque generated
replacement.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GL46 = ROOT / "Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp"
BRIDGE = ROOT / "ci/minecraft-e2e/native/glfw_mithril_bridge.mm"
AUDIT = ROOT / "verify/gl46_semantic_audit.py"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {n}")
    return text.replace(old, new, 1)


def replace_regex_once(text: str, pattern: str, new: str, label: str) -> str:
    out, n = re.subn(pattern, new, text, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{label}: expected exactly one regex anchor, found {n}")
    return out


def patch_gl46(text: str) -> str:
    old_scope = """static void program_uniform_begin(GLuint program) {
    if (!g_state) return;
    g_state->currentProgram = program;
}
"""
    new_scope = """struct ProgramUniformScope {
    GLuint previous = 0;
    bool armed = false;

    explicit ProgramUniformScope(GLuint program) noexcept {
        if (!g_state) return;
        previous = g_state->currentProgram;
        g_state->currentProgram = program;
        armed = true;
    }

    ProgramUniformScope(const ProgramUniformScope&) = delete;
    ProgramUniformScope& operator=(const ProgramUniformScope&) = delete;

    ~ProgramUniformScope() noexcept {
        if (armed && g_state) g_state->currentProgram = previous;
    }
};

static ProgramUniformScope program_uniform_begin(GLuint program) {
    return ProgramUniformScope(program);
}
"""
    if old_scope in text:
        text = replace_once(text, old_scope, new_scope, "program-uniform scope")
        calls = text.count("program_uniform_begin(program);")
        if calls < 20:
            raise SystemExit(f"program-uniform calls: suspiciously low count {calls}")
        text = text.replace(
            "program_uniform_begin(program);",
            "[[maybe_unused]] auto program_uniform_scope = program_uniform_begin(program);",
        )
    elif "struct ProgramUniformScope" not in text:
        raise SystemExit("program-uniform scope: neither old nor hardened implementation found")

    old_bind_unit = """/* 50. glBindTextureUnit - glActiveTexture + glBindTexture. */
void glBindTextureUnit(GLuint unit, GLuint texture) {
    MITHRIL_ENSURE_INIT();
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}
"""
    new_bind_unit = r"""/* DSA texture binding must use the texture object's immutable target and must
 * not perturb GL_ACTIVE_TEXTURE.  Binding name zero clears every target for
 * the selected unit, matching the GL 4.5/4.6 DSA contract. */
static const GLenum kDsaTextureTargets[] = {
    GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
    GL_TEXTURE_RECTANGLE, GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_BUFFER,
    GL_TEXTURE_1D_ARRAY, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_CUBE_MAP_ARRAY,
    GL_TEXTURE_2D_MULTISAMPLE_ARRAY
};

static GLenum texture_target_for_object(GLuint texture) {
    if (!g_state || texture == 0) return 0;
    auto it = g_state->textures.find(texture);
    return it == g_state->textures.end() ? 0 : it->second.target;
}

static bool texture_target_is_layered(GLenum target) {
    switch (target) {
        case GL_TEXTURE_3D:
        case GL_TEXTURE_1D_ARRAY:
        case GL_TEXTURE_2D_ARRAY:
        case GL_TEXTURE_CUBE_MAP:
        case GL_TEXTURE_CUBE_MAP_ARRAY:
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
            return true;
        default:
            return false;
    }
}

static GLenum image_format_for_object(GLuint texture) {
    if (!g_state || texture == 0) return GL_R8;
    auto it = g_state->textures.find(texture);
    if (it == g_state->textures.end()) return GL_RGBA8;
    GLenum fmt = static_cast<GLenum>(it->second.internalFormat);
    return fmt ? fmt : GL_RGBA8;
}

static void unbind_all_texture_targets_on_active_unit() {
    for (GLenum target : kDsaTextureTargets) glBindTexture(target, 0);
}

/* 50. glBindTextureUnit - target-aware DSA binding with active-unit restore. */
void glBindTextureUnit(GLuint unit, GLuint texture) {
    MITHRIL_ENSURE_INIT();
    GLint previous = g_state ? g_state->activeTextureUnit : 0;
    glActiveTexture(GL_TEXTURE0 + unit);
    if (texture == 0) {
        unbind_all_texture_targets_on_active_unit();
    } else {
        GLenum target = texture_target_for_object(texture);
        if (target != 0) glBindTexture(target, texture);
    }
    glActiveTexture(GL_TEXTURE0 + previous);
}
"""
    if old_bind_unit in text:
        text = replace_once(text, old_bind_unit, new_bind_unit, "glBindTextureUnit")
    elif "target-aware DSA binding" not in text:
        raise SystemExit("glBindTextureUnit: neither old nor hardened implementation found")

    text = replace_regex_once(
        text,
        r"/\* 103\. glBindBuffersRange.*?(?=/\* 104\.)",
        r"""/* 103. glBindBuffersRange - GL 4.4 multi-bind semantics. */
void glBindBuffersRange(GLenum target, GLuint first, GLsizei count,
                        const GLuint* buffers, const GLintptr* offsets,
                        const GLsizeiptr* sizes) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    if (!buffers) {
        /* A null buffers array resets every indexed binding in the range;
         * offsets/sizes are ignored in this case. */
        for (GLsizei i = 0; i < count; ++i) glBindBufferBase(target, first + i, 0);
        return;
    }
    for (GLsizei i = 0; i < count; ++i) {
        glBindBufferRange(target, first + i, buffers[i],
                          offsets ? offsets[i] : 0,
                          sizes ? sizes[i] : 0);
    }
}

""",
        "glBindBuffersRange",
    )

    text = replace_regex_once(
        text,
        r"/\* 104\. glBindTextures.*?(?=/\* 105\.)",
        r"""/* 104. glBindTextures - target-aware GL 4.4 multi-bind semantics. */
void glBindTextures(GLuint first, GLsizei count, const GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    GLint previous = g_state ? g_state->activeTextureUnit : 0;
    for (GLsizei i = 0; i < count; ++i) {
        glActiveTexture(GL_TEXTURE0 + first + static_cast<GLuint>(i));
        GLuint texture = textures ? textures[i] : 0;
        if (texture == 0) {
            unbind_all_texture_targets_on_active_unit();
        } else {
            GLenum target = texture_target_for_object(texture);
            if (target != 0) glBindTexture(target, texture);
        }
    }
    glActiveTexture(GL_TEXTURE0 + previous);
}

""",
        "glBindTextures",
    )

    text = replace_regex_once(
        text,
        r"/\* 106\. glBindImageTextures.*?(?=/\* 107\.)",
        r"""/* 106. glBindImageTextures - derive layered/format state from each texture. */
void glBindImageTextures(GLuint first, GLsizei count, const GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (count <= 0) return;
    for (GLsizei i = 0; i < count; ++i) {
        GLuint texture = textures ? textures[i] : 0;
        if (texture == 0) {
            glBindImageTexture(first + static_cast<GLuint>(i), 0, 0, GL_FALSE, 0,
                               GL_READ_ONLY, GL_R8);
            continue;
        }
        GLenum target = texture_target_for_object(texture);
        GLboolean layered = texture_target_is_layered(target) ? GL_TRUE : GL_FALSE;
        glBindImageTexture(first + static_cast<GLuint>(i), texture, 0, layered, 0,
                           GL_READ_WRITE, image_format_for_object(texture));
    }
}

""",
        "glBindImageTextures",
    )
    return text


def patch_bridge(text: str) -> str:
    atomics = """std::atomic<unsigned long long> g_context_count{0};
std::atomic<unsigned long long> g_swap_count{0};
std::atomic<unsigned long long> g_proc_count{0};
"""
    persistent = atomics + """std::mutex g_identity_mutex;
std::string g_identity_vendor;
std::string g_identity_renderer;
std::string g_identity_version;
"""
    if "g_identity_vendor" not in text:
        text = replace_once(text, atomics, persistent, "persistent bridge identity globals")

    old = """    const char* vendor = "";
    const char* renderer = "";
    const char* version = "";
    auto& m = mithril();
    if (g_current && m.getString) {
        vendor = reinterpret_cast<const char*>(m.getString(GL_VENDOR));
        renderer = reinterpret_cast<const char*>(m.getString(GL_RENDERER));
        version = reinterpret_cast<const char*>(m.getString(GL_VERSION));
    }
"""
    new = """    std::string vendor;
    std::string renderer;
    std::string version;
    auto& m = mithril();
    {
        std::lock_guard<std::mutex> identityLock(g_identity_mutex);
        if (g_current && m.getString) {
            const char* v = reinterpret_cast<const char*>(m.getString(GL_VENDOR));
            const char* r = reinterpret_cast<const char*>(m.getString(GL_RENDERER));
            const char* ver = reinterpret_cast<const char*>(m.getString(GL_VERSION));
            if (v && *v) g_identity_vendor = v;
            if (r && *r) g_identity_renderer = r;
            if (ver && *ver) g_identity_version = ver;
        }
        /* Keep the last authoritative identity after glfwMakeContextCurrent(NULL)
         * and glfwDestroyWindow().  The post-process oracle runs after JVM exit. */
        vendor = g_identity_vendor;
        renderer = g_identity_renderer;
        version = g_identity_version;
    }
"""
    if old in text:
        text = replace_once(text, old, new, "bridge identity snapshot")
        text = text.replace(
            "json_escape(vendor).c_str(), json_escape(renderer).c_str(), json_escape(version).c_str());",
            "json_escape(vendor.c_str()).c_str(), json_escape(renderer.c_str()).c_str(), json_escape(version.c_str()).c_str());",
            1,
        )
    elif "Keep the last authoritative identity" not in text:
        raise SystemExit("bridge identity snapshot: neither old nor hardened implementation found")
    return text


AUDIT_SOURCE = r'''#!/usr/bin/env python3
"""Fast source-level regression guard for semantics fixed by the GL 4.6 hardening lane.

This is not a substitute for Khronos CTS.  It prevents known facade/stub
implementations from silently returning while GPU/CTS lanes provide behavioral
validation.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
gl = (root / "Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp").read_text()
bridge = (root / "ci/minecraft-e2e/native/glfw_mithril_bridge.mm").read_text()
errors = []

def require(cond, message):
    if not cond:
        errors.append(message)

require("struct ProgramUniformScope" in gl, "glProgramUniform* must preserve GL_CURRENT_PROGRAM")
require(gl.count("program_uniform_scope = program_uniform_begin(program)") >= 20,
        "all glProgramUniform* entry points must use scoped program selection")
require("target-aware DSA binding with active-unit restore" in gl,
        "glBindTextureUnit must derive the object's target and restore GL_ACTIVE_TEXTURE")
require("target-aware GL 4.4 multi-bind semantics" in gl,
        "glBindTextures must be target-aware")
require("if (count <= 0 || !buffers) return;" not in gl,
        "glBindBuffersRange(NULL) must reset indexed bindings")
require("derive layered/format state from each texture" in gl,
        "glBindImageTextures must derive image binding metadata")
require("g_identity_vendor" in bridge and "Keep the last authoritative identity" in bridge,
        "Minecraft bridge must persist GL identity through context teardown")

if errors:
    for e in errors:
        print(f"FAIL: {e}", file=sys.stderr)
    raise SystemExit(1)
print("GL46 semantic source audit: PASS")
'''


def verify() -> None:
    gl = GL46.read_text()
    bridge = BRIDGE.read_text()
    if "struct ProgramUniformScope" not in gl:
        raise SystemExit("verify: ProgramUniformScope missing")
    if gl.count("program_uniform_scope = program_uniform_begin(program)") < 20:
        raise SystemExit("verify: not all glProgramUniform variants were scoped")
    for needle in (
        "target-aware DSA binding with active-unit restore",
        "target-aware GL 4.4 multi-bind semantics",
        "derive layered/format state from each texture",
    ):
        if needle not in gl:
            raise SystemExit(f"verify: missing {needle}")
    if "Keep the last authoritative identity" not in bridge:
        raise SystemExit("verify: persistent bridge identity missing")
    compile(AUDIT.read_text(), str(AUDIT), "exec")
    print("deterministic GL46 hardening invariants: PASS")


def apply() -> None:
    gl_before = GL46.read_text()
    bridge_before = BRIDGE.read_text()
    gl_after = patch_gl46(gl_before)
    bridge_after = patch_bridge(bridge_before)
    if gl_after != gl_before:
        GL46.write_text(gl_after)
    if bridge_after != bridge_before:
        BRIDGE.write_text(bridge_after)
    AUDIT.write_text(AUDIT_SOURCE)
    verify()


def main() -> None:
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--apply", action="store_true")
    g.add_argument("--verify", action="store_true")
    a = p.parse_args()
    if a.apply:
        apply()
    else:
        verify()


if __name__ == "__main__":
    main()
