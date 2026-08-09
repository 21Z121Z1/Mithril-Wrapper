#!/usr/bin/env python3
"""Regenerate src/gl/exports.cpp (GL 3.3 core stub exports).

Reads docs/gl33_core_list.md for the function set and real prototypes from
third_party/GL/glcorearb.h. Functions listed in MGL_IMPL are implemented for
real in src/gl/ and are skipped here.

Run: python3 scripts/gen_gl_stubs.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HDR = ROOT / "third_party" / "GL" / "glcorearb.h"
GL_LIST = ROOT / "docs" / "gl33_core_list.md"
OUT = ROOT / "src" / "gl" / "exports.cpp"

# Functions that have real implementations in src/gl/.
MGL_IMPL = frozenset({
    "glBlendColor", "glBlendEquation", "glBlendEquationSeparate", "glBlendFunc",
    "glBlendFuncSeparate", "glClear", "glClearBufferfi", "glClearBufferfv",
    "glClearBufferiv", "glClearBufferuiv", "glClearColor", "glClearDepth", "glClearStencil",
    "glColorMask", "glColorMaski", "glCullFace", "glDepthFunc", "glDepthMask",
    "glDepthRange", "glDisable", "glDisablei", "glEnable", "glEnablei",
    "glFinish", "glFlush", "glFrontFace", "glGetBooleanv", "glGetDoublev",
    "glGetError", "glGetFloatv", "glGetIntegerv", "glGetInteger64v",
    "glGetString", "glGetStringi", "glHint", "glIsEnabled", "glIsEnabledi",
    "glLineWidth", "glLogicOp", "glPixelStorei", "glPointSize", "glPolygonMode",
    "glPolygonOffset", "glSampleCoverage", "glSampleMaski", "glScissor",
    "glStencilFunc", "glStencilFuncSeparate", "glStencilMask",
    "glStencilMaskSeparate", "glStencilOp", "glStencilOpSeparate", "glViewport",
    "glGetPointerv", "glClampColor",
    # GL 3.2 execution sync objects.
    "glClientWaitSync", "glDeleteSync", "glFenceSync", "glGetSynciv",
    "glIsSync", "glPrimitiveRestartIndex", "glWaitSync",
    # S2 shaders/programs/uniforms implemented in src/gl/.
    "glAttachShader", "glBindAttribLocation", "glCompileShader", "glCreateProgram",
    "glCreateShader", "glDeleteProgram", "glDeleteShader", "glDetachShader",
    "glGetActiveAttrib", "glGetActiveUniform", "glGetActiveUniformName",
    "glGetActiveUniformsiv", "glGetActiveUniformBlockName",
    "glGetActiveUniformBlockiv", "glGetAttachedShaders",
    "glGetAttribLocation", "glGetProgramInfoLog", "glGetProgramiv",
    "glGetShaderInfoLog", "glGetShaderSource", "glGetShaderiv",
    "glGetUniformBlockIndex", "glGetUniformIndices", "glGetUniformLocation",
    "glGetUniformfv", "glGetUniformiv", "glGetUniformuiv",
    "glIsProgram", "glIsShader", "glLinkProgram", "glShaderSource",
    "glUniform1f", "glUniform1fv", "glUniform1i", "glUniform1iv", "glUniform1ui",
    "glUniform1uiv", "glUniform2f", "glUniform2fv", "glUniform2i", "glUniform2iv",
    "glUniform2ui", "glUniform2uiv", "glUniform3f", "glUniform3fv", "glUniform3i",
    "glUniform3iv", "glUniform3ui", "glUniform3uiv", "glUniform4f", "glUniform4fv",
    "glUniform4i", "glUniform4iv", "glUniform4ui", "glUniform4uiv",
    "glUniformMatrix2fv", "glUniformMatrix2x3fv", "glUniformMatrix2x4fv",
    "glUniformMatrix3fv", "glUniformMatrix3x2fv", "glUniformMatrix3x4fv",
    "glUniformMatrix4fv", "glUniformMatrix4x2fv", "glUniformMatrix4x3fv",
    "glUniformBlockBinding", "glUseProgram", "glValidateProgram",
    # S3 subset (M2-VK): buffer objects / VAOs / vertex attrib / draw.
    "glGenBuffers", "glDeleteBuffers", "glBindBuffer", "glBindBufferBase",
    "glBindBufferRange", "glIsBuffer",
    "glBufferData", "glBufferSubData",
    "glGenVertexArrays", "glDeleteVertexArrays", "glBindVertexArray",
    "glIsVertexArray",
    "glEnableVertexAttribArray", "glDisableVertexAttribArray",
    "glVertexAttribPointer", "glDrawArrays",
    "glReadPixels",
    # S3 (M3-VK): full vertex/draw/buffer-query family.
    "glVertexAttribDivisor",
    "glVertexAttribIPointer",
    "glVertexAttrib1d", "glVertexAttrib1dv", "glVertexAttrib1f", "glVertexAttrib1fv",
    "glVertexAttrib1s", "glVertexAttrib1sv", "glVertexAttrib2d", "glVertexAttrib2dv",
    "glVertexAttrib2f", "glVertexAttrib2fv", "glVertexAttrib2s", "glVertexAttrib2sv",
    "glVertexAttrib3d", "glVertexAttrib3dv", "glVertexAttrib3f", "glVertexAttrib3fv",
    "glVertexAttrib3s", "glVertexAttrib3sv", "glVertexAttrib4bv", "glVertexAttrib4d",
    "glVertexAttrib4dv", "glVertexAttrib4f", "glVertexAttrib4fv", "glVertexAttrib4iv",
    "glVertexAttrib4s", "glVertexAttrib4sv", "glVertexAttrib4ubv", "glVertexAttrib4usv",
    "glVertexAttrib4uiv", "glVertexAttrib1i", "glVertexAttrib1iv", "glVertexAttrib2i",
    "glVertexAttrib2iv", "glVertexAttrib3i", "glVertexAttrib3iv", "glVertexAttrib4i",
    "glVertexAttrib4iv", "glVertexAttrib1ui", "glVertexAttrib1uiv", "glVertexAttrib2ui",
    "glVertexAttrib2uiv", "glVertexAttrib3ui", "glVertexAttrib3uiv", "glVertexAttrib4ui",
    "glVertexAttrib4uiv",
    "glVertexAttrib4Nbv", "glVertexAttrib4Niv", "glVertexAttrib4Nsv",
    "glVertexAttrib4Nub", "glVertexAttrib4Nubv", "glVertexAttrib4Nuiv",
    "glVertexAttrib4Nusv",
    "glGetVertexAttribdv", "glGetVertexAttribfv", "glGetVertexAttribiv",
    "glGetVertexAttribIiv", "glGetVertexAttribIuiv", "glGetVertexAttribPointerv",
    "glGetBufferParameteriv", "glGetBufferParameteri64v", "glGetBufferPointerv",
    "glGetBufferSubData", "glMapBuffer", "glMapBufferRange", "glUnmapBuffer",
    "glFlushMappedBufferRange", "glCopyBufferSubData",
    "glDrawArraysInstanced",
    "glDrawElements", "glDrawRangeElements", "glDrawElementsBaseVertex",
    "glDrawElementsInstanced", "glDrawElementsInstancedBaseVertex",
    "glDrawRangeElementsBaseVertex",
    "glMultiDrawArrays", "glMultiDrawElements", "glMultiDrawElementsBaseVertex",
    # S4 textures (M4): object table + 2D upload + sampler parameters + mips.
    "glActiveTexture", "glBindTexture", "glDeleteTextures", "glGenTextures",
    "glIsTexture", "glTexImage1D", "glTexImage2D", "glTexSubImage1D",
    "glTexSubImage2D", "glGenerateMipmap",
    "glTexParameteri", "glTexParameteriv", "glTexParameterf", "glTexParameterfv",
    "glTexParameterIiv", "glTexParameterIuiv",
    "glGetTexParameteriv", "glGetTexParameterfv", "glGetTexParameterIiv",
    "glGetTexParameterIuiv", "glGetTexLevelParameteriv", "glGetTexLevelParameterfv",
    "glBindSampler", "glDeleteSamplers", "glGenSamplers", "glIsSampler",
    "glSamplerParameteri", "glSamplerParameteriv", "glSamplerParameterf",
    "glSamplerParameterfv", "glSamplerParameterIiv", "glSamplerParameterIuiv",
    "glGetSamplerParameteriv", "glGetSamplerParameterfv",
    "glGetSamplerParameterIiv", "glGetSamplerParameterIuiv",
    # S4 (M4 remainder): 3D / arrays / cubemap / compressed / copy / readback.
    "glTexImage3D", "glTexSubImage3D", "glGetTexImage",
    "glCompressedTexImage1D", "glCompressedTexImage2D", "glCompressedTexImage3D",
    "glCompressedTexSubImage1D", "glCompressedTexSubImage2D",
    "glCompressedTexSubImage3D", "glGetCompressedTexImage",
    "glCopyTexImage1D", "glCopyTexImage2D", "glCopyTexSubImage1D",
    "glCopyTexSubImage2D", "glCopyTexSubImage3D", "glTexBuffer",
    "glPixelStoref",
    # S5 FBO/renderbuffer (M5): object tables + attachment + status + blit.
    "glBindFramebuffer", "glBindRenderbuffer", "glBlitFramebuffer",
    "glCheckFramebufferStatus", "glDeleteFramebuffers", "glDeleteRenderbuffers",
    "glDrawBuffer", "glDrawBuffers", "glReadBuffer",
    "glFramebufferRenderbuffer", "glFramebufferTexture", "glFramebufferTexture1D",
    "glFramebufferTexture2D", "glFramebufferTexture3D", "glFramebufferTextureLayer",
    "glGenFramebuffers", "glGenRenderbuffers",
    "glGetFramebufferAttachmentParameteriv", "glGetRenderbufferParameteriv",
    "glIsFramebuffer", "glIsRenderbuffer",
    "glRenderbufferStorage", "glRenderbufferStorageMultisample",
})


def parse_list():
    txt = GL_LIST.read_text()
    names = set()
    for line in txt.splitlines():
        line = line.strip()
        if line.startswith("gl"):
            names.update(line.split())
    return sorted(names)


def parse_decls(hdr_text):
    # Every prototype lives in its own ;-terminated unit; strip comments and
    # preprocessor lines first so '#define GLAPI extern' cannot pollute a match.
    hdr = re.sub(r"/\*.*?\*/", " ", hdr_text, flags=re.S)
    hdr = re.sub(r"(?m)^#.*$", "", hdr)
    pat = re.compile(r"\bGLAPI\s+(.*?)\s*APIENTRY\s+(gl[A-Za-z0-9]+)\s*\((.*?)\)\s*$",
                     re.S)
    decls = {}
    for unit in hdr.split(";"):
        m = pat.search(unit)
        if m:
            decls[m.group(2)] = (re.sub(r"\s+", " ", m.group(1)).strip(),
                                 m.group(3).strip())
    return decls


def safe_expr(rtype):
    rt = rtype.rstrip()
    if rt == "void":
        return None
    if rt in ("GLintptr", "GLsizeiptr", "GLsync") or rt.endswith("*"):
        return "nullptr"
    if rt in ("GLfloat", "GLclampf"):
        return "0.0f"
    if rt in ("GLdouble", "GLclampd"):
        return "0.0"
    return "0"


def main():
    wanted = parse_list()
    decls = parse_decls(HDR.read_text())
    missing = set(wanted) - set(decls)
    if missing:
        print("missing prototypes:", sorted(missing))
        sys.exit(1)

    emit = [n for n in wanted if n not in MGL_IMPL]
    lines = [
        "// Mithril-Wrapper GL entry points -- stub table.",
        "// GENERATED by scripts/gen_gl_stubs.py -- DO NOT EDIT BY HAND.",
        "#include <GL/glcorearb.h>",
        "",
        "namespace mithril {",
        "void GlStubCalled(const char* name);",
        "} // namespace mithril",
        "",
        'extern "C" {',
        "",
    ]
    for name in emit:
        rtype, args = decls[name]
        ret = safe_expr(rtype)
        if ret is None:
            lines.append(
                f"  {rtype} APIENTRY {name}({args}) {{\n"
                f'    mithril::GlStubCalled("{name}");\n  }}')
        else:
            lines.append(
                f"  {rtype} APIENTRY {name}({args}) {{\n"
                f'    mithril::GlStubCalled("{name}");\n'
                f"    return {ret};\n  }}")
        lines.append("")
    lines.append('} // extern "C"')
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT}: {len(emit)} stubs, "
          f"{len(wanted) - len(emit)} implemented in src/gl/")


if __name__ == "__main__":
    main()
