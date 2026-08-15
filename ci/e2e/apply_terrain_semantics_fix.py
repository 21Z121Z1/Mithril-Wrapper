#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

def replace_once(path, old, new):
    p = ROOT / path
    s = p.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected one exact match, found {n}")
    p.write_text(s.replace(old, new, 1))


def regex_once(path, pattern, repl):
    p = ROOT / path
    s = p.read_text()
    out, n = re.subn(pattern, repl, s, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: expected one regex match, found {n}")
    p.write_text(out)

# ---------------------------------------------------------------------------
# Four vertex variants are required by ARB_clip_control.  The two independent
# semantic bits are clip-depth convention (NEGATIVE_ONE_TO_ONE vs ZERO_TO_ONE)
# and Y origin (LOWER_LEFT vs UPPER_LEFT / presentation orientation).
# ---------------------------------------------------------------------------
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.h",
    """struct ShaderLinkOutput {
    std::vector<uint32_t> vertexSpirv;        // non-flipped (user-created FBOs)
    std::vector<uint32_t> vertexSpirvFlipped; // Y-flipped (default framebuffer)
    std::vector<uint32_t> fragmentSpirv;
};""",
    """struct ShaderLinkOutput {
    // NEGATIVE_ONE_TO_ONE: GL clip-Z [-w,+w] is remapped to Metal/Vulkan [0,w].
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> vertexSpirvFlipped;
    // ZERO_TO_ONE: clip-Z is already [0,w], so no Z remap is legal.
    std::vector<uint32_t> vertexSpirvZeroToOne;
    std::vector<uint32_t> vertexSpirvZeroToOneFlipped;
    std::vector<uint32_t> fragmentSpirv;
};""")

replace_once(
    "Mithril-Wrapper-cpp/MG_State/State.h",
    """    // SPIR-V for each linked stage (consumed by backend_get_or_create_pipeline).
    // vertexSpirv:         Z remap, NO Y flip — for user-created FBOs.
    // vertexSpirvYFlipped: Z remap + Y flip — for default framebuffer (FBO 0).
    // (Red/black screen fix — must be preserved.)
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> vertexSpirvYFlipped;
    std::vector<uint32_t> fragmentSpirv;""",
    """    // SPIR-V variants consumed by backend_get_or_create_pipeline().
    // The first pair implements GL_NEGATIVE_ONE_TO_ONE by remapping clip Z;
    // the second pair implements GL_ZERO_TO_ONE without that remap.  Each pair
    // has non-flipped / Y-flipped forms so GL_UPPER_LEFT composes with the
    // framebuffer presentation orientation without changing viewport/scissor.
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> vertexSpirvYFlipped;
    std::vector<uint32_t> vertexSpirvZeroToOne;
    std::vector<uint32_t> vertexSpirvZeroToOneYFlipped;
    std::vector<uint32_t> fragmentSpirv;""")

# Shader position fixup: Z remap is conditional, not unconditional.
regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    r"void inject_position_fixup\(std::string& src, GLenum gl_stage, bool flip_y\) \{.*?\n\}\n\n// ---------------------------------------------------------------------------\n// Custom IO resolver",
    """void inject_position_fixup(std::string& src, GLenum gl_stage, bool flip_y,
                           bool remap_z) {
    if (gl_stage != GL_VERTEX_SHADER) return;
    static const std::regex main_re(R"(\\bvoid\\s+main\\s*\\()");
    if (!std::regex_search(src, main_re)) return;
    src = std::regex_replace(src, main_re, "void _mithril_original_main(");
    src += "\\nvoid main() {\\n    _mithril_original_main();\\n";
    if (flip_y) {
        src += "    gl_Position.y = -gl_Position.y;\\n";
    }
    // Metal/Vulkan clip Z is [0,w].  GL_NEGATIVE_ONE_TO_ONE supplies [-w,w]
    // and therefore needs the affine remap before clipping. GL_ZERO_TO_ONE
    // already supplies [0,w] and MUST NOT be remapped (ARB_clip_control).
    if (remap_z) {
        src += "    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\\n";
    }
    src += "}\\n";
}

// ---------------------------------------------------------------------------
// Custom IO resolver""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    "inject_position_fixup(source, gl_stage, /*flip_y=*/false);",
    "inject_position_fixup(source, gl_stage, /*flip_y=*/false, /*remap_z=*/true);")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    """    // key -> (vertexSpirv, vertexSpirvFlipped, fragmentSpirv)
    struct Entry {
        std::vector<uint32_t> vs, vsFlip, fs;
    };""",
    """    // key -> four clip-control VS variants + fragment SPIR-V.
    struct Entry {
        std::vector<uint32_t> vs, vsFlip, vsZero, vsZeroFlip, fs;
    };""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    """            out.vertexSpirv = it->second.vs;
            out.vertexSpirvFlipped = it->second.vsFlip;
            out.fragmentSpirv = it->second.fs;""",
    """            out.vertexSpirv = it->second.vs;
            out.vertexSpirvFlipped = it->second.vsFlip;
            out.vertexSpirvZeroToOne = it->second.vsZero;
            out.vertexSpirvZeroToOneFlipped = it->second.vsZeroFlip;
            out.fragmentSpirv = it->second.fs;""")

regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    r"    // Non-flipped variant \(user FBOs\)\..*?    out\.fragmentSpirv = std::move\(fsSpv\);",
    """    // Compile all four ARB_clip_control vertex variants. Descriptor
    // resources are unchanged by the position wrapper, so each independent
    // deterministic remap produces the same descriptor binding assignment.
    std::vector<uint32_t> vsSpv, fsSpv;
    std::vector<uint32_t> vsFlipSpv, fsFlipSpv;
    std::vector<uint32_t> vsZeroSpv, fsZeroSpv;
    std::vector<uint32_t> vsZeroFlipSpv, fsZeroFlipSpv;

    std::string vs_plain = vs;
    inject_position_fixup(vs_plain, GL_VERTEX_SHADER,
                          /*flip_y=*/false, /*remap_z=*/true);
    if (!compile_program(vs_plain, fs, attrib_bindings, nullptr,
                         vsSpv, fsSpv, out_info_log)) {
        return false;
    }

    std::string vs_flip = vs;
    inject_position_fixup(vs_flip, GL_VERTEX_SHADER,
                          /*flip_y=*/true, /*remap_z=*/true);
    if (!compile_program(vs_flip, fs, attrib_bindings, nullptr,
                         vsFlipSpv, fsFlipSpv, out_info_log)) {
        MITHRIL_LOG_WARN("shader", "Y-flipped NEGATIVE_ONE_TO_ONE variant failed; "
                          "reusing non-flipped variant. Info: %s", out_info_log.c_str());
        vsFlipSpv = vsSpv;
    }

    std::string vs_zero = vs;
    inject_position_fixup(vs_zero, GL_VERTEX_SHADER,
                          /*flip_y=*/false, /*remap_z=*/false);
    if (!compile_program(vs_zero, fs, attrib_bindings, nullptr,
                         vsZeroSpv, fsZeroSpv, out_info_log)) {
        MITHRIL_LOG_WARN("shader", "ZERO_TO_ONE variant failed; reusing "
                          "NEGATIVE_ONE_TO_ONE variant. Info: %s", out_info_log.c_str());
        vsZeroSpv = vsSpv;
    }

    std::string vs_zero_flip = vs;
    inject_position_fixup(vs_zero_flip, GL_VERTEX_SHADER,
                          /*flip_y=*/true, /*remap_z=*/false);
    if (!compile_program(vs_zero_flip, fs, attrib_bindings, nullptr,
                         vsZeroFlipSpv, fsZeroFlipSpv, out_info_log)) {
        MITHRIL_LOG_WARN("shader", "Y-flipped ZERO_TO_ONE variant failed; "
                          "reusing non-flipped ZERO_TO_ONE variant. Info: %s",
                          out_info_log.c_str());
        vsZeroFlipSpv = vsZeroSpv;
    }

    out.vertexSpirv = std::move(vsSpv);
    out.vertexSpirvFlipped = std::move(vsFlipSpv);
    out.vertexSpirvZeroToOne = std::move(vsZeroSpv);
    out.vertexSpirvZeroToOneFlipped = std::move(vsZeroFlipSpv);
    out.fragmentSpirv = std::move(fsSpv);""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Shader.cpp",
    """    entry.vs = out.vertexSpirv;
    entry.vsFlip = out.vertexSpirvFlipped;
    entry.fs = out.fragmentSpirv;""",
    """    entry.vs = out.vertexSpirv;
    entry.vsFlip = out.vertexSpirvFlipped;
    entry.vsZero = out.vertexSpirvZeroToOne;
    entry.vsZeroFlip = out.vertexSpirvZeroToOneFlipped;
    entry.fs = out.fragmentSpirv;""")

# Program stores all four variants.  Fallback keeps the legacy visibility
# safety net; a clip-control regression is still caught by the GPU oracle.
replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Program.cpp",
    """    p->vertexSpirv.clear();
    p->vertexSpirvYFlipped.clear();
    p->fragmentSpirv.clear();""",
    """    p->vertexSpirv.clear();
    p->vertexSpirvYFlipped.clear();
    p->vertexSpirvZeroToOne.clear();
    p->vertexSpirvZeroToOneYFlipped.clear();
    p->fragmentSpirv.clear();""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Program.cpp",
    """        p->vertexSpirv = std::move(linkOut.vertexSpirv);
        p->vertexSpirvYFlipped = std::move(linkOut.vertexSpirvFlipped);
        p->fragmentSpirv = std::move(linkOut.fragmentSpirv);""",
    """        p->vertexSpirv = std::move(linkOut.vertexSpirv);
        p->vertexSpirvYFlipped = std::move(linkOut.vertexSpirvFlipped);
        p->vertexSpirvZeroToOne = std::move(linkOut.vertexSpirvZeroToOne);
        p->vertexSpirvZeroToOneYFlipped = std::move(linkOut.vertexSpirvZeroToOneFlipped);
        p->fragmentSpirv = std::move(linkOut.fragmentSpirv);""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Program.cpp",
    """        if (p->vertexSpirvYFlipped.empty() && !p->vertexSpirv.empty()) {
            p->vertexSpirvYFlipped = p->vertexSpirv;
            MITHRIL_LOG_WARN("program", "vertexSpirvYFlipped was empty for "
                              "program %u, falling back to non-flipped variant "
                              "(%zu words) — Y orientation will be wrong but "
                              "draws will not be skipped",
                              program, p->vertexSpirv.size());
        }""",
    """        if (p->vertexSpirvYFlipped.empty() && !p->vertexSpirv.empty()) {
            p->vertexSpirvYFlipped = p->vertexSpirv;
            MITHRIL_LOG_WARN("program", "vertexSpirvYFlipped missing for program %u; "
                              "using non-flipped fallback", program);
        }
        if (p->vertexSpirvZeroToOne.empty() && !p->vertexSpirv.empty()) {
            p->vertexSpirvZeroToOne = p->vertexSpirv;
            MITHRIL_LOG_WARN("program", "ZERO_TO_ONE VS missing for program %u; "
                              "using NEGATIVE_ONE_TO_ONE fallback", program);
        }
        if (p->vertexSpirvZeroToOneYFlipped.empty() &&
            !p->vertexSpirvZeroToOne.empty()) {
            p->vertexSpirvZeroToOneYFlipped = p->vertexSpirvZeroToOne;
            MITHRIL_LOG_WARN("program", "Y-flipped ZERO_TO_ONE VS missing for program %u; "
                              "using non-flipped ZERO_TO_ONE fallback", program);
        }""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Program.cpp",
    """            p->vertexSpirv = std::move(vs_fb);
            p->vertexSpirvYFlipped = std::move(vs_fb_flip);
            p->fragmentSpirv = std::move(fs_fb);""",
    """            p->vertexSpirv = std::move(vs_fb);
            p->vertexSpirvYFlipped = std::move(vs_fb_flip);
            p->vertexSpirvZeroToOne = p->vertexSpirv;
            p->vertexSpirvZeroToOneYFlipped = p->vertexSpirvYFlipped;
            p->fragmentSpirv = std::move(fs_fb);""")

# Select clip-control shader variant in prepare_draw.  UPPER_LEFT is composed
# with the existing presentation Y flip; viewport/scissor stay in GL numeric
# coordinates (the focused GPU oracle already proves those conversions).
regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    r"    const std::vector<uint32_t>& vs_spirv = \[&\]\(\) -> const std::vector<uint32_t>& \{.*?    \}\(\);",
    """    const bool clip_upper_left = (g_state->clipOrigin == GL_UPPER_LEFT);
    const bool zero_to_one = (g_state->clipDepthMode == GL_ZERO_TO_ONE);
    // Existing framebuffer convention: FBO0 needs one Y inversion relative to
    // user FBOs. ARB_clip_control UPPER_LEFT contributes one additional
    // inversion, therefore the two compose by XOR.
    const bool want_y_flip = is_default_fbo ^ clip_upper_left;
    const std::vector<uint32_t>& vs_spirv = [&]() -> const std::vector<uint32_t>& {
        if (zero_to_one) {
            const auto& preferred = want_y_flip
                ? prog->vertexSpirvZeroToOneYFlipped
                : prog->vertexSpirvZeroToOne;
            if (!preferred.empty()) return preferred;
            return want_y_flip ? prog->vertexSpirvYFlipped : prog->vertexSpirv;
        }
        const auto& preferred = want_y_flip ? prog->vertexSpirvYFlipped : prog->vertexSpirv;
        if (!preferred.empty()) return preferred;
        return prog->vertexSpirv;
    }();""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    """#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        invert_front_face = true;
#endif
        backend_set_front_face(""",
    """#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        invert_front_face = true;
#endif
        // ARB_clip_control: changing to UPPER_LEFT negates the signed polygon
        // area used for front-face determination. Toggle the existing backend
        // orientation compensation exactly once.
        if (g_state->clipOrigin == GL_UPPER_LEFT) invert_front_face = !invert_front_face;
        backend_set_front_face(""")

# ARB_indirect_parameters uses GL_PARAMETER_BUFFER, never the command buffer.
regex_once(
    "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp",
    r"void glMultiDrawArraysIndirectCount\(GLenum mode, const void\* indirect,.*?\n\}\n\n/\* =========================================================================\n \* Compute dispatch",
    """void glMultiDrawArraysIndirectCount(GLenum mode, const void* indirect,
                                    GLintptr drawcount, GLint maxdrawcount,
                                    GLsizei stride) {
    MITHRIL_ENSURE_INIT();
    if (maxdrawcount < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    if (maxdrawcount == 0) return;
    if (drawcount < 0 || (drawcount & 3) != 0 || stride < 0 ||
        (stride != 0 && (stride < 16 || (stride & 3) != 0))) {
        mithril::state_set_error(GL_INVALID_VALUE); return;
    }
    GLuint indirect_name = g_state->bufferBindings[(int)mithril::BufferTarget::DrawIndirect].name;
    GLuint count_name = g_state->bufferBindings[(int)mithril::BufferTarget::Parameter].name;
    if (!indirect_name || !count_name) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    VkBuffer indirect_buf = backend_get_buffer(indirect_name);
    VkBuffer count_buf = backend_get_buffer(count_name);
    mithril::Buffer* count_state = mithril::state_get_buffer(count_name);
    if (indirect_buf == VK_NULL_HANDLE || count_buf == VK_NULL_HANDLE || !count_state ||
        (uint64_t)drawcount + sizeof(uint32_t) > (uint64_t)count_state->size) {
        mithril::state_set_error(GL_INVALID_OPERATION); return;
    }
    if (!prepare_draw(mode)) return;
    int s = stride ? stride : 16;
    backend_draw_indirect_count((int)mode, indirect_buf,
                                (VkDeviceSize)(intptr_t)indirect,
                                count_buf, (VkDeviceSize)drawcount,
                                maxdrawcount, s);
    end_draw();
}

void glMultiDrawElementsIndirectCount(GLenum mode, GLenum type,
                                      const void* indirect, GLintptr drawcount,
                                      GLint maxdrawcount, GLsizei stride) {
    MITHRIL_ENSURE_INIT();
    if (maxdrawcount < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    if (maxdrawcount == 0) return;
    if (drawcount < 0 || (drawcount & 3) != 0 || stride < 0 ||
        (stride != 0 && (stride < 20 || (stride & 3) != 0))) {
        mithril::state_set_error(GL_INVALID_VALUE); return;
    }
    GLuint indirect_name = g_state->bufferBindings[(int)mithril::BufferTarget::DrawIndirect].name;
    GLuint count_name = g_state->bufferBindings[(int)mithril::BufferTarget::Parameter].name;
    if (!indirect_name || !count_name) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    VkBuffer indirect_buf = backend_get_buffer(indirect_name);
    VkBuffer count_buf = backend_get_buffer(count_name);
    mithril::Buffer* count_state = mithril::state_get_buffer(count_name);
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    VkBuffer ib = backend_get_buffer(ib_name);
    if (indirect_buf == VK_NULL_HANDLE || count_buf == VK_NULL_HANDLE ||
        ib == VK_NULL_HANDLE || !count_state ||
        (uint64_t)drawcount + sizeof(uint32_t) > (uint64_t)count_state->size) {
        mithril::state_set_error(GL_INVALID_OPERATION); return;
    }
    if (!prepare_draw(mode)) return;
    int s = stride ? stride : 20;
    backend_draw_indexed_indirect_count((int)mode, index_type_to_int(type),
                                        ib, 0,
                                        indirect_buf, (VkDeviceSize)(intptr_t)indirect,
                                        count_buf, (VkDeviceSize)drawcount,
                                        maxdrawcount, s);
    end_draw();
}

/* =========================================================================
 * Compute dispatch""")

# DirectMetal must adapt GL restart tokens to Metal's mandatory max-index
# sentinel.  Comparison is done on the ORIGINAL index before baseVertex.
regex_once(
    "Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalCommandStream.mm",
    r"void issue_indexed_draw\(GLenum primitive, uint32_t count, int index_type,.*?\n\}\n\n/\* Indirect-draw argument records",
    """void issue_indexed_draw(GLenum primitive, uint32_t count, int index_type,
                        MetalBuffer* index_buffer, NSUInteger index_offset,
                        uint32_t instanceCount, NSInteger baseVertex,
                        uint32_t baseInstance) {
    id<MTLRenderCommandEncoder> r = g_renderEncoder;
    if (count == 0 || !index_buffer || index_buffer->buf == nil) return;
    const bool fan = (primitive == GL_TRIANGLE_FAN);
    const bool loop = (primitive == GL_LINE_LOOP);

    const uint32_t sourceMax = index_type == 1 ? 0xffffffffu
                               : (index_type == 2 ? 0xffu : 0xffffu);
    const bool fixedRestart = mithril::g_state && mithril::g_state->primitiveRestartFixedIndex;
    const bool programmableRestart = mithril::g_state && mithril::g_state->primitiveRestart &&
                                     !fixedRestart;
    const uint32_t restartToken = fixedRestart ? sourceMax
                                  : (programmableRestart
                                         ? mithril::g_state->primitiveRestartIndex
                                         : 0u);
    const bool restartCanMatch = fixedRestart ||
        (programmableRestart && restartToken <= sourceMax);
    const bool nativeRestartToken = restartCanMatch && restartToken == sourceMax;

    // Metal only accepts U16/U32 indices and ALWAYS reserves that type's
    // maximum value as a primitive-restart sentinel. U8 therefore always
    // needs widening; programmable restart needs remapping unless its token
    // already equals the native maximum. Fixed U16/U32 can stay zero-copy.
    const bool needsRestartRemap = restartCanMatch && !nativeRestartToken;
    if (index_type != 2 && !fan && !loop && !needsRestartRemap) {
        [r drawIndexedPrimitives:primitive_from_gl(primitive)
                       indexCount:(NSUInteger)count
                        indexType:index_type_from_int(index_type)
                      indexBuffer:index_buffer->buf
                indexBufferOffset:index_offset
                   instanceCount:instanceCount
                      baseVertex:baseVertex
                    baseInstance:baseInstance];
        return;
    }

    if (index_buffer->contents == nullptr) {
        static uint32_t warned = 0;
        warn_limited(warned, "dmt", "indexed semantic adaptation needs CPU-readable indices "
                                  "(private storage?) — draw dropped");
        return;
    }
    const uint8_t* src = (const uint8_t*)index_buffer->contents + index_offset;

    static thread_local std::vector<uint32_t> gather;
    gather.clear();
    gather.reserve(count);
    if (index_type == 1) {
        const uint32_t* s = (const uint32_t*)(const void*)src;
        for (uint32_t i = 0; i < count; ++i) gather.push_back(s[i]);
    } else if (index_type == 2) {
        for (uint32_t i = 0; i < count; ++i) gather.push_back((uint8_t)src[i]);
    } else {
        const uint16_t* s = (const uint16_t*)(const void*)src;
        for (uint32_t i = 0; i < count; ++i) gather.push_back(s[i]);
    }

    // Fan and loop need explicit per-restart segmentation before their Metal
    // primitive expansion. Restart comparison deliberately precedes
    // baseVertex, matching OpenGL's indexed assembly semantics.
    if (fan) {
        static thread_local std::vector<uint32_t> expanded;
        expanded.clear();
        size_t begin = 0;
        while (begin < gather.size()) {
            size_t end = begin;
            while (end < gather.size() &&
                   !(restartCanMatch && gather[end] == restartToken)) ++end;
            if (end - begin >= 3) {
                for (size_t t = begin + 1; t + 1 < end; ++t) {
                    expanded.push_back(gather[begin]);
                    expanded.push_back(gather[t]);
                    expanded.push_back(gather[t + 1]);
                }
            }
            begin = end + (end < gather.size() ? 1 : 0);
        }
        if (expanded.empty()) return;
        // U32 only when required by source width; fixed/program restart tokens
        // have already been removed by expansion, so no synthetic sentinel is
        // needed in the triangle-list output.
        const bool use32 = index_type == 1;
        const NSUInteger bytes = expanded.size() * (use32 ? 4u : 2u);
        id<MTLBuffer> sb; NSUInteger off; void* ptr;
        if (!scratch_alloc(bytes, sb, off, ptr)) return;
        if (use32) {
            std::memcpy(ptr, expanded.data(), bytes);
        } else {
            uint16_t* d = (uint16_t*)ptr;
            for (size_t i = 0; i < expanded.size(); ++i) d[i] = (uint16_t)expanded[i];
        }
        scratch_flush(sb, off, bytes);
        [r drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:expanded.size()
                        indexType:(use32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16)
                      indexBuffer:sb indexBufferOffset:off
                   instanceCount:instanceCount baseVertex:baseVertex
                    baseInstance:baseInstance];
        return;
    }

    if (loop) {
        // A restart creates independent line loops. Issue each segment as its
        // own line strip with the first index appended; no sentinel is needed.
        size_t begin = 0;
        while (begin < gather.size()) {
            size_t end = begin;
            while (end < gather.size() &&
                   !(restartCanMatch && gather[end] == restartToken)) ++end;
            const size_t n = end - begin;
            if (n >= 2) {
                const bool use32 = index_type == 1;
                const NSUInteger bytes = (n + 1) * (use32 ? 4u : 2u);
                id<MTLBuffer> sb; NSUInteger off; void* ptr;
                if (!scratch_alloc(bytes, sb, off, ptr)) return;
                if (use32) {
                    uint32_t* d = (uint32_t*)ptr;
                    for (size_t i = 0; i < n; ++i) d[i] = gather[begin + i];
                    d[n] = gather[begin];
                } else {
                    uint16_t* d = (uint16_t*)ptr;
                    for (size_t i = 0; i < n; ++i) d[i] = (uint16_t)gather[begin + i];
                    d[n] = (uint16_t)gather[begin];
                }
                scratch_flush(sb, off, bytes);
                [r drawIndexedPrimitives:MTLPrimitiveTypeLineStrip
                               indexCount:n + 1
                                indexType:(use32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16)
                              indexBuffer:sb indexBufferOffset:off
                           instanceCount:instanceCount baseVertex:baseVertex
                            baseInstance:baseInstance];
            }
            begin = end + (end < gather.size() ? 1 : 0);
        }
        return;
    }

    // Plain primitive adaptation. U8 widens to U16. A programmable U16
    // restart widens to U32 so ordinary 0xffff remains a vertex index while
    // the chosen token becomes Metal's 0xffffffff sentinel. U32 stays U32 and
    // remaps a custom token in-place; 0xffffffff cannot be represented as a
    // non-restart Metal index, but such a vertex index cannot address a real
    // Minecraft vertex buffer and is outside the practical terrain domain.
    const bool use32 = (index_type == 1) || (index_type == 0 && needsRestartRemap);
    const NSUInteger bytes = (NSUInteger)count * (use32 ? 4u : 2u);
    id<MTLBuffer> sb; NSUInteger off; void* ptr;
    if (!scratch_alloc(bytes, sb, off, ptr)) return;
    if (use32) {
        uint32_t* dst = (uint32_t*)ptr;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t v = gather[i];
            dst[i] = (restartCanMatch && v == restartToken) ? 0xffffffffu : v;
        }
    } else {
        uint16_t* dst = (uint16_t*)ptr;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t v = gather[i];
            dst[i] = (restartCanMatch && v == restartToken) ? 0xffffu : (uint16_t)v;
        }
    }
    scratch_flush(sb, off, bytes);
    [r drawIndexedPrimitives:primitive_from_gl(primitive)
                   indexCount:(NSUInteger)count
                    indexType:(use32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16)
                  indexBuffer:sb indexBufferOffset:off
               instanceCount:instanceCount baseVertex:baseVertex
                baseInstance:baseInstance];
}

/* Indirect-draw argument records""")

# Force indirect indexed records through issue_indexed_draw whenever restart
# state needs index-domain adaptation. Fixed U16/U32 remains Metal-native.
replace_once(
    "Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalCommandStream.mm",
    """    const bool expanded = (primitive == GL_TRIANGLE_FAN || primitive == GL_LINE_LOOP);
    const bool widenU8 = (index_type == 2); // Metal has no U8 index type

    if (!expanded && !widenU8 && indirect->contents == nullptr &&
        count == 1 && effStride == 20) {""",
    """    const bool expanded = (primitive == GL_TRIANGLE_FAN || primitive == GL_LINE_LOOP);
    const bool widenU8 = (index_type == 2); // Metal has no U8 index type
    const uint32_t sourceMax = index_type == 1 ? 0xffffffffu
                               : (index_type == 2 ? 0xffu : 0xffffu);
    const bool fixedRestart = mithril::g_state && mithril::g_state->primitiveRestartFixedIndex;
    const bool programmableRestart = mithril::g_state && mithril::g_state->primitiveRestart &&
                                     !fixedRestart;
    const bool restartNeedsAdaptation = widenU8 ||
        (programmableRestart && mithril::g_state->primitiveRestartIndex <= sourceMax &&
         mithril::g_state->primitiveRestartIndex != sourceMax);

    if (!expanded && !restartNeedsAdaptation && indirect->contents == nullptr &&
        count == 1 && effStride == 20) {""")

replace_once(
    "Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalCommandStream.mm",
    """        if (expanded || widenU8) {
            // indexStart is the first-index offset in ELEMENTS (GL/Vulkan""",
    """        if (expanded || restartNeedsAdaptation) {
            // indexStart is the first-index offset in ELEMENTS (GL/Vulkan""")

print("terrain semantics source edits materialized")
