#!/usr/bin/env python3
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

require("return mithril::state_take_error();" in (root / "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp").read_text(),
        "glGetError must expose the context error queue instead of swallowing it")

_getter = (root / "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp").read_text()
for _pname in ("GL_TEXTURE_BINDING_1D", "GL_TEXTURE_BINDING_2D", "GL_TEXTURE_BINDING_3D",
               "GL_TEXTURE_BINDING_CUBE_MAP", "GL_TEXTURE_BINDING_RECTANGLE",
               "GL_TEXTURE_BINDING_2D_MULTISAMPLE", "GL_TEXTURE_BINDING_BUFFER",
               "GL_TEXTURE_BINDING_1D_ARRAY", "GL_TEXTURE_BINDING_2D_ARRAY",
               "GL_TEXTURE_BINDING_CUBE_MAP_ARRAY", "GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY"):
    require(_getter.count("case " + _pname + ":") == 1,
            "texture binding getter must appear exactly once: " + _pname)

require("gl46_active_uniforms" in gl and "u.blockIndex" in gl and "u.matrixStride" in gl,
        "uniform reflection APIs must expose Program/SPIR-V metadata")
require("Set all indices to GL_INVALID_INDEX" not in gl,
        "glGetUniformIndices must not be a facade default")
require("\x00" not in gl, "GL46 compatibility source must not contain embedded NUL bytes")

require("gl46_get_sampler_scalar" in gl and "gl46_set_sampler_scalar" in gl,
        "sampler getters/setters must expose tracked sampler state")
require("Sampler object getters (GL 3.3): return the GL defaults" not in gl,
        "sampler queries must not return facade defaults")
require("glSamplerParameteri(sampler, pname" not in gl,
        "GL46 sampler integer setters must not depend on undeclared cross-TU entry points")

require("gl46_dsa_texture_target" in gl and "textureTargetFromGL" in gl,
        "texture DSA parameter calls must use the object's tracked target")
require("sampler_default_params(pname" not in gl,
        "texture DSA integer getters must not return sampler facade defaults")

_metal_backend=(root/"Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalBackend.mm").read_text()
require("out_pixels, readDefaultFramebuffer ? 1 : 0" not in _metal_backend,
        "DirectMetal glReadPixels must not vertically reverse the already GL-oriented default framebuffer")
require("out_pixels, 0);" in _metal_backend,
        "DirectMetal glReadPixels must preserve GL bottom-left row order")
