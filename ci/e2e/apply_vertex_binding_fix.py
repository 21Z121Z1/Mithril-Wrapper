#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path, old, new):
    p = ROOT / path
    s = p.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{path}: exact match count {n}, expected 1")
    p.write_text(s.replace(old, new, 1))


def regex_once(path, pattern, repl):
    p = ROOT / path
    s = p.read_text()
    out, n = re.subn(pattern, lambda _m: repl, s, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: regex match count {n}, expected 1")
    p.write_text(out)

# State model: format-relative offset and binding stride are distinct GL state.
replace_once(
    "Mithril-Wrapper-cpp/MG_State/State.h",
    """    GLsizei   stride     = 0;
    const void* pointer  = nullptr;      // offset when a VBO is bound
    GLuint    boundBuffer = 0;           // GL_ARRAY_BUFFER at bind time
    GLuint    divisor    = 0;
    GLuint    bindingIndex = 0;          // vertex buffer binding this attrib reads from
};""",
    """    // Legacy VertexAttribPointer query state. Separate-format commands do
    // not overload these fields: format-relative offset and buffer binding are
    // represented explicitly below/by VertexBinding.
    GLsizei   stride     = 0;
    const void* pointer  = nullptr;
    GLuint    boundBuffer = 0;
    GLuint    divisor    = 0;
    GLuint    bindingIndex = 0;
    GLuint    relativeOffset = 0;
};""")

replace_once(
    "Mithril-Wrapper-cpp/MG_State/State.h",
    """    GLsizei  stride  = 0;   // byte stride between elements""",
    """    GLsizei  stride  = 16;  // ARB_vertex_attrib_binding initial value""")

# Query tokens and tight-stride helper.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    """#ifndef GL_VERTEX_ATTRIB_ARRAY_DIVISOR
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR         0x88FE
#endif

extern \"C\" {""",
    """#ifndef GL_VERTEX_ATTRIB_ARRAY_DIVISOR
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR         0x88FE
#endif
#ifndef GL_VERTEX_ATTRIB_BINDING
#define GL_VERTEX_ATTRIB_BINDING               0x82D4
#endif
#ifndef GL_VERTEX_ATTRIB_RELATIVE_OFFSET
#define GL_VERTEX_ATTRIB_RELATIVE_OFFSET       0x82D5
#endif

namespace {
GLsizei vertex_element_bytes(GLint size, GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:  return (GLsizei)size;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_HALF_FLOAT:     return (GLsizei)(size * 2);
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:          return (GLsizei)(size * 4);
#ifdef GL_FIXED
        case GL_FIXED:          return (GLsizei)(size * 4);
#endif
#ifdef GL_DOUBLE
        case GL_DOUBLE:         return (GLsizei)(size * 8);
#endif
#ifdef GL_INT_2_10_10_10_REV
        case GL_INT_2_10_10_10_REV: return 4;
#endif
#ifdef GL_UNSIGNED_INT_2_10_10_10_REV
        case GL_UNSIGNED_INT_2_10_10_10_REV: return 4;
#endif
#ifdef GL_UNSIGNED_INT_10F_11F_11F_REV
        case GL_UNSIGNED_INT_10F_11F_11F_REV: return 4;
#endif
        default: return 0;
    }
}

GLsizei effective_vertex_stride(GLint size, GLenum type, GLsizei stride) {
    return stride != 0 ? stride : vertex_element_bytes(size, type);
}
} // namespace

extern \"C\" {""")

# Legacy pointer APIs must update BOTH legacy query state and the equivalent
# separate binding state, per ARB_vertex_attrib_binding section 2.8.
regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    r"void glVertexAttribPointer\(GLuint index, GLint size, GLenum type,.*?\n\}\n\nvoid glVertexAttribIPointer",
    """void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs || stride < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    const GLuint buffer = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    if (buffer == 0 && pointer != nullptr) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    const GLsizei effective = effective_vertex_stride(size, type, stride);
    if (effective <= 0) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::VertexAttrib& a = vao->attribs[index];
    a.size = size;
    a.type = type;
    a.normalized = (normalized != 0);
    a.integer = false;
    a.stride = stride;                  // legacy query state keeps caller value
    a.pointer = pointer;                // legacy pointer query state
    a.boundBuffer = buffer;
    a.bindingIndex = index;             // required legacy->separate equivalence
    a.relativeOffset = 0;
    mithril::VertexBinding& binding = vao->bindings[index];
    binding.buffer = buffer;
    binding.offset = (GLintptr)(uintptr_t)pointer;
    binding.stride = effective;
    a.divisor = binding.divisor;
    ++vao->attribVersions[index];
    ++vao->configVersion;
}

void glVertexAttribIPointer""")

regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    r"void glVertexAttribIPointer\(GLuint index, GLint size, GLenum type,.*?\n\}\n\nvoid glVertexAttribDivisor",
    """void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, const void* pointer) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs || stride < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    const GLuint buffer = g_state->bufferBindings[(int)mithril::BufferTarget::Array].name;
    if (buffer == 0 && pointer != nullptr) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    const GLsizei effective = effective_vertex_stride(size, type, stride);
    if (effective <= 0) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::VertexAttrib& a = vao->attribs[index];
    a.size = size;
    a.type = type;
    a.normalized = false;
    a.integer = true;
    a.stride = stride;
    a.pointer = pointer;
    a.boundBuffer = buffer;
    a.bindingIndex = index;
    a.relativeOffset = 0;
    mithril::VertexBinding& binding = vao->bindings[index];
    binding.buffer = buffer;
    binding.offset = (GLintptr)(uintptr_t)pointer;
    binding.stride = effective;
    a.divisor = binding.divisor;
    ++vao->attribVersions[index];
    ++vao->configVersion;
}

void glVertexAttribDivisor""")

regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    r"void glVertexAttribDivisor\(GLuint index, GLuint divisor\) \{.*?\n\}\n\n// ---------------------------------------------------------------------------",
    """void glVertexAttribDivisor(GLuint index, GLuint divisor) {
    MITHRIL_ENSURE_INIT();
    if (index >= mithril::kMaxVertexAttribs) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) return;
    // ARB_vertex_attrib_binding explicitly defines this legacy entry point as:
    //   VertexAttribBinding(index,index); VertexBindingDivisor(index,divisor)
    // It does NOT apply to the attribute's previously selected binding.
    vao->attribs[index].bindingIndex = index;
    vao->attribs[index].divisor = divisor;
    vao->bindings[index].divisor = divisor;
    ++vao->attribVersions[index];
    ++vao->configVersion;
}

// ---------------------------------------------------------------------------""")

# Binding API validation + true relative-offset state.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    """    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }""",
    """    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings ||
        offset < 0 || stride < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    """    // relativeoffset is the byte offset within the vertex buffer binding's
    // stride where this attribute's data begins. Stored in pointer field
    // (same as glVertexAttribPointer's pointer-as-offset convention).
    a.pointer = (const void*)(uintptr_t)relativeoffset;""",
    """    // Format-relative state is independent of the binding's base offset
    // and of legacy GL_VERTEX_ATTRIB_ARRAY_POINTER query state.
    a.relativeOffset = relativeoffset;""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    """    a.integer = true;
    a.pointer = (const void*)(uintptr_t)relativeoffset;
    vao->attribVersions[attribindex]++;""",
    """    a.integer = true;
    a.relativeOffset = relativeoffset;
    vao->attribVersions[attribindex]++;""")

# GetVertexAttrib queries must traverse attrib->binding where the extension
# requires it.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",
    """    const mithril::VertexAttrib& a = vao->attribs[index];
    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:        *params = a.enabled ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:           *params = a.size; break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:           *params = (GLint)a.type; break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:     *params = a.normalized ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:         *params = a.stride; break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *params = (GLint)a.boundBuffer; break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:        *params = (GLint)a.divisor; break;
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER:        *params = a.integer ? GL_TRUE : GL_FALSE; break;
        default:                                    *params = 0; break;
    }""",
    """    const mithril::VertexAttrib& a = vao->attribs[index];
    const GLuint bi = a.bindingIndex < (GLuint)mithril::kMaxVertexBindings
                        ? a.bindingIndex : 0;
    const mithril::VertexBinding& binding = vao->bindings[bi];
    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:        *params = a.enabled ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:           *params = a.size; break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:           *params = (GLint)a.type; break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:     *params = a.normalized ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:         *params = a.stride; break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *params = (GLint)binding.buffer; break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:        *params = (GLint)binding.divisor; break;
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER:        *params = a.integer ? GL_TRUE : GL_FALSE; break;
        case GL_VERTEX_ATTRIB_BINDING:              *params = (GLint)a.bindingIndex; break;
        case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:      *params = (GLint)a.relativeOffset; break;
        default:                                    *params = 0; break;
    }""")

# Draw-time flattening: each GL attribute remains one backend attribute slot,
# but its source buffer/stride/divisor come from the selected binding and its
# format offset is the attribute-relative offset. This preserves existing
# Metal/Vulkan pipeline layouts while implementing shared GL bindings exactly.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    """        MGVertexAttrib& m = attribs[attrib_count++];
        m.location     = i;
        m.size         = a.size;
        m.type         = a.type;
        m.normalized   = a.normalized ? 1 : 0;
        m.integer      = a.integer ? 1 : 0;
        m.stride       = a.stride;
        m.offset       = (int)(intptr_t)a.pointer;
        m.enabled      = 1;
        m.buffer_name  = a.boundBuffer;
        m.divisor      = a.divisor;""",
    """        if (a.bindingIndex >= (GLuint)mithril::kMaxVertexBindings) {
            mithril::state_set_error(GL_INVALID_OPERATION);
            continue;
        }
        const mithril::VertexBinding& binding = vao->bindings[a.bindingIndex];
        MGVertexAttrib& m = attribs[attrib_count++];
        m.location     = i;
        m.size         = a.size;
        m.type         = a.type;
        m.normalized   = a.normalized ? 1 : 0;
        m.integer      = a.integer ? 1 : 0;
        m.stride       = binding.stride;
        m.offset       = (int)a.relativeOffset;
        m.enabled      = 1;
        m.buffer_name  = binding.buffer;
        m.divisor      = (int)binding.divisor;""")

regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    r"            backend_set_vertex_buffer\(m\.location, buf, 0\);",
    """            const mithril::VertexAttrib& sourceAttrib = vao->attribs[m.location];
            const mithril::VertexBinding& sourceBinding =
                vao->bindings[sourceAttrib.bindingIndex];
            // GL fetch address = binding.offset + vertex*binding.stride +
            // attribute.relativeOffset. The latter is in the pipeline vertex
            // descriptor; only binding.offset belongs in the dynamic buffer bind.
            backend_set_vertex_buffer(m.location, buf,
                                      (VkDeviceSize)sourceBinding.offset);""")

print("ARB vertex binding source edits materialized")
