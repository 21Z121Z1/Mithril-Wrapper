#!/usr/bin/env python3
from pathlib import Path
import json
import re


def replace_once(path, old, new):
    p = Path(path)
    s = p.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, got {n}: {old[:80]!r}")
    p.write_text(s.replace(old, new, 1))


def inject_after_init(path, signature_re, statement):
    p = Path(path)
    s = p.read_text()
    rx = re.compile(r"(" + signature_re + r"\s*\{\s*\n\s*MITHRIL_ENSURE_INIT\(\);)", re.S)
    matches = list(rx.finditer(s))
    if len(matches) != 1:
        raise SystemExit(f"{path}: signature {signature_re!r} matched {len(matches)} times")
    s = rx.sub(lambda m: m.group(1) + "\n    " + statement, s, count=1)
    p.write_text(s)


header = "Mithril-Wrapper-cpp/MG_State/State.h"
old_decl = (
    "void semantic_trace_eventf(const char* domain, const char* semantic,\n"
    "                           const char* api, const char* fmt, ...);\n"
)
new_decl = old_decl + (
    "void semantic_trace_event_oncef(const char* domain, const char* semantic,\n"
    "                                const char* api, const char* fmt, ...);\n"
)
replace_once(header, old_decl, new_decl)

state = "Mithril-Wrapper-cpp/MG_State/State.cpp"
marker = "void semantic_trace_external_api_call(const char* api, const void* caller) {"
helper = "\n".join([
    "void semantic_trace_event_oncef(const char* domain, const char* semantic,",
    "                                const char* api, const char* fmt, ...) {",
    "    static const bool enabled = [] {",
    "        const char* path = std::getenv(\"MITHRIL_GL_SEMANTIC_TRACE\");",
    "        return path && *path;",
    "    }();",
    "    if (!enabled) return;",
    "",
    "    char details[768];",
    "    va_list args;",
    "    va_start(args, fmt);",
    "    std::vsnprintf(details, sizeof(details), fmt ? fmt : \"\", args);",
    "    va_end(args);",
    "",
    "    // Preserve each distinct production parameter class once per thread.",
    "    thread_local std::unordered_set<std::string> seen;",
    "    std::string key = std::string(domain ? domain : \"\") + \"\\t\" +",
    "                      (semantic ? semantic : \"\") + \"\\t\" +",
    "                      (api ? api : \"\") + \"\\t\" + details;",
    "    if (!seen.insert(key).second) return;",
    "    semantic_trace_eventf(domain, semantic, api, \"%s\", details);",
    "}",
    "",
    "",
])
replace_once(state, marker, helper + marker)

B = "Mithril-Wrapper-cpp/MG_Impl/Buffer.cpp"
inject_after_init(B, r"void glBindBuffer\(GLenum target, GLuint buffer\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glBindBuffer", "target=0x%x object=%s", target, buffer ? "nonzero" : "zero");')
inject_after_init(B, r"void glBufferData\(GLenum target, GLsizeiptr size, const void\* data, GLenum usage\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glBufferData", "target=0x%x usage=0x%x size=%s data=%s", target, usage, size <= 256 ? "tiny" : size <= 65536 ? "small" : size <= 1048576 ? "medium" : "large", data ? "host" : "null");')
inject_after_init(B, r"void\* glMapBufferRange\(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glMapBufferRange", "target=0x%x offset=%s length=%s access=0x%x", target, offset ? "nonzero" : "zero", length <= 256 ? "tiny" : length <= 65536 ? "small" : length <= 1048576 ? "medium" : "large", access);')
inject_after_init(B, r"void glFlushMappedBufferRange\(GLenum target, GLintptr offset, GLsizeiptr length\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glFlushMappedBufferRange", "target=0x%x offset=%s length=%s", target, offset ? "nonzero" : "zero", length <= 256 ? "tiny" : length <= 65536 ? "small" : length <= 1048576 ? "medium" : "large");')
inject_after_init(B, r"GLboolean glUnmapBuffer\(GLenum target\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glUnmapBuffer", "target=0x%x", target);')
inject_after_init(B, r"void glCopyBufferSubData\(GLenum readTarget, GLenum writeTarget,\s*GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glCopyBufferSubData", "read=0x%x write=0x%x offsets=%s size=%s", readTarget, writeTarget, (readOffset || writeOffset) ? "nonzero" : "zero", size <= 256 ? "tiny" : size <= 65536 ? "small" : size <= 1048576 ? "medium" : "large");')
inject_after_init(B, r"void glBindBufferRange\(GLenum target, GLuint index, GLuint buffer,\s*GLintptr offset, GLsizeiptr size\)",
    'mithril::semantic_trace_event_oncef("buffer_mapping_storage", "buffer.mapping_ubo", "glBindBufferRange", "target=0x%x index=%u object=%s offset=%s size=%s", target, index, buffer ? "nonzero" : "zero", offset ? "nonzero" : "zero", size <= 256 ? "tiny" : size <= 65536 ? "small" : size <= 1048576 ? "medium" : "large");')

T = "Mithril-Wrapper-cpp/MG_Impl/Texture.cpp"
inject_after_init(T, r"void glBindTexture\(GLenum target, GLuint texture\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "core33.resource_roundtrip", "glBindTexture", "target=0x%x object=%s", target, texture ? "nonzero" : "zero");')
inject_after_init(T, r"void glTexImage2D\(GLenum target, GLint level, GLint internalFormat,\s*GLsizei width, GLsizei height, GLint border,\s*GLenum format, GLenum type, const void\* pixels\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "core33.resource_roundtrip", "glTexImage2D", "target=0x%x level=%d internal=0x%x external=0x%x type=0x%x extent=%s pixels=%s", target, level, internalFormat, format, type, (width * height <= 256) ? "tiny" : (width * height <= 65536) ? "small" : (width * height <= 1048576) ? "medium" : "large", pixels ? "host_or_offset" : "null");')
inject_after_init(T, r"void glTexSubImage2D\(GLenum target, GLint level, GLint xoffset, GLint yoffset,\s*GLsizei width, GLsizei height,\s*GLenum format, GLenum type, const void\* pixels\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "core33.resource_roundtrip", "glTexSubImage2D", "target=0x%x level=%d offsets=%s external=0x%x type=0x%x extent=%s pixels=%s", target, level, (xoffset || yoffset) ? "nonzero" : "zero", format, type, (width * height <= 256) ? "tiny" : (width * height <= 65536) ? "small" : (width * height <= 1048576) ? "medium" : "large", pixels ? "host_or_offset" : "null");')
inject_after_init(T, r"void glReadPixels\(GLint x, GLint y, GLsizei width, GLsizei height,\s*GLenum format, GLenum type, void\* pixels\)",
    'mithril::semantic_trace_event_oncef("pixel_pack_unpack", "pixel.pack_unpack", "glReadPixels", "origin=%s extent=%s format=0x%x type=0x%x dst=%s", (x || y) ? "nonzero" : "zero", (width * height <= 256) ? "tiny" : (width * height <= 65536) ? "small" : (width * height <= 1048576) ? "medium" : "large", format, type, pixels ? "host_or_offset" : "null");')

S = "Mithril-Wrapper-cpp/MG_Impl/Stubs.cpp"
inject_after_init(S, r"void glBindSampler\(GLuint unit, GLuint sampler\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "sampler.object_override", "glBindSampler", "unit=%u object=%s", unit, sampler ? "nonzero" : "zero");')
inject_after_init(S, r"void glSamplerParameteri\(GLuint sampler, GLenum pname, GLint param\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "sampler.object_override", "glSamplerParameteri", "object=%s pname=0x%x param=%d", sampler ? "nonzero" : "zero", pname, param);')
inject_after_init(S, r"void glSamplerParameterf\(GLuint sampler, GLenum pname, GLfloat param\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "sampler.object_override", "glSamplerParameterf", "object=%s pname=0x%x param_class=%s", sampler ? "nonzero" : "zero", pname, param == 0.0f ? "zero" : param == 1.0f ? "one" : "other");')

F = "Mithril-Wrapper-cpp/MG_Impl/Framebuffer.cpp"
inject_after_init(F, r"void glBindFramebuffer\(GLenum target, GLuint framebuffer\)",
    'mithril::semantic_trace_event_oncef("framebuffer_mrt", "framebuffer.mrt_indexed_clear", "glBindFramebuffer", "target=0x%x object=%s", target, framebuffer ? "nonzero" : "zero");')
inject_after_init(F, r"void glFramebufferTexture2D\(GLenum target, GLenum attachment, GLenum textarget,\s*GLuint texture, GLint level\)",
    'mithril::semantic_trace_event_oncef("framebuffer_mrt", "framebuffer.mrt_indexed_clear", "glFramebufferTexture2D", "target=0x%x attachment=0x%x textarget=0x%x object=%s level=%d", target, attachment, textarget, texture ? "nonzero" : "zero", level);')
inject_after_init(F, r"void glBlitFramebuffer\(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,\s*GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,\s*GLbitfield mask, GLenum filter\)",
    'mithril::semantic_trace_event_oncef("framebuffer_mrt", "framebuffer.mrt_indexed_clear", "glBlitFramebuffer", "mask=0x%x filter=0x%x scaled=%s src_origin=%s dst_origin=%s", mask, filter, ((srcX1-srcX0)!=(dstX1-dstX0) || (srcY1-srcY0)!=(dstY1-dstY0)) ? "yes" : "no", (srcX0 || srcY0) ? "nonzero" : "zero", (dstX0 || dstY0) ? "nonzero" : "zero");')

G = "Mithril-Wrapper-cpp/MG_Impl/gl.cpp"
inject_after_init(G, r"void glEnable\(GLenum cap\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glEnable", "cap=0x%x transition=on", cap);')
inject_after_init(G, r"void glDisable\(GLenum cap\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glDisable", "cap=0x%x transition=off", cap);')
inject_after_init(G, r"void glDepthFunc\(GLenum func\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glDepthFunc", "func=0x%x", func);')
inject_after_init(G, r"void glDepthMask\(GLboolean flag\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glDepthMask", "write=%s", flag ? "on" : "off");')
inject_after_init(G, r"void glBlendFuncSeparate\(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glBlendFuncSeparate", "rgb=0x%x/0x%x alpha=0x%x/0x%x", srcRGB, dstRGB, srcAlpha, dstAlpha);')
inject_after_init(G, r"void glColorMaski\(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glColorMaski", "index=%u mask=%u%u%u%u", index, !!r, !!g, !!b, !!a);')
inject_after_init(G, r"void glPolygonMode\(GLenum face, GLenum mode\)",
    'mithril::semantic_trace_event_oncef("blend_depth_stencil", "raster.state", "glPolygonMode", "face=0x%x mode=0x%x", face, mode);')
inject_after_init(G, r"void glPixelStorei\(GLenum pname, GLint param\)",
    'mithril::semantic_trace_event_oncef("pixel_pack_unpack", "pixel.pack_unpack", "glPixelStorei", "pname=0x%x param=%d", pname, param);')
inject_after_init(G, r"void glActiveTexture\(GLenum texture\)",
    'mithril::semantic_trace_event_oncef("textures_samplers", "sampler.object_override", "glActiveTexture", "selector=0x%x", texture);')

manifest = Path("ci/e2e/gl_semantic_contract.json")
doc = json.loads(manifest.read_text())
enforced = doc.setdefault("trace_coverage", {}).setdefault("enforced_domains", [])
for domain in [
    "buffer_mapping_storage",
    "textures_samplers",
    "framebuffer_mrt",
    "blend_depth_stencil",
    "pixel_pack_unpack",
]:
    if domain not in enforced:
        enforced.append(domain)
doc["schema_version"] = "1.5"
manifest.write_text(json.dumps(doc, indent=2, sort_keys=False) + "\n")
