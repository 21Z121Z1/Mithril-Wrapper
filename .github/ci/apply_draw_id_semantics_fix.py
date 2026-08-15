from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {n}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_first_with_count(path: str, old: str, new: str, expected: int) -> None:
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != expected:
        raise SystemExit(
            f"{path}: expected exactly {expected} matches before first replacement, "
            f"found {n}: {old[:80]!r}"
        )
    p.write_text(text.replace(old, new, 1))

# Carry the GL multi-draw ordinal through the frontend. Direct backends that
# lower a Multi* call to multiple native draws need this to preserve gl_DrawID.
replace_once(
    "Mithril-Wrapper-cpp/MG_State/State.h",
    "    int32_t  currentBaseVertex = 0;\n    uint32_t currentBaseInstance = 0;\n",
    "    int32_t  currentBaseVertex = 0;\n    uint32_t currentBaseInstance = 0;\n"
    "    // ARB_shader_draw_parameters: ordinal of the current sub-draw inside\n"
    "    // a Multi* command. Ordinary draws are zero. DirectMetal lowers\n"
    "    // SPIR-V DrawIndex to SPIRV-Cross' spvDrawIndex buffer(19), so the\n"
    "    // backend reads this value immediately before each native draw.\n"
    "    uint32_t currentDrawID = 0;\n"
)

p = "Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp"
replace_once(
    p,
    "        backend_draw_arrays((int)mode, (int)first[i], (int)count[i]);\n    }\n    end_draw();  // 一次 end_render_pass\n",
    "        g_state->currentDrawID = (uint32_t)i;\n"
    "        backend_draw_arrays((int)mode, (int)first[i], (int)count[i]);\n"
    "    }\n"
    "    g_state->currentDrawID = 0;\n"
    "    end_draw();  // 一次 end_render_pass\n"
)
replace_once(
    p,
    "            if (need_draw)\n                backend_draw_indexed((int)mode, (int)count[i], idx_type, ib,\n                                     (VkDeviceSize)(intptr_t)indices[i]);\n",
    "            if (need_draw) {\n"
    "                g_state->currentDrawID = (uint32_t)i;\n"
    "                backend_draw_indexed((int)mode, (int)count[i], idx_type, ib,\n"
    "                                     (VkDeviceSize)(intptr_t)indices[i]);\n"
    "            }\n"
)
# This exact staging snippet occurs once in glMultiDrawElements and once in
# glMultiDrawElementsBaseVertex. Replace only the first occurrence here; the
# BaseVertex branch gets its ordinal via the more specific context below.
replace_first_with_count(
    p,
    "                if (staged != VK_NULL_HANDLE)\n                    backend_draw_indexed((int)mode, (int)count[i], idx_type, staged, 0);\n",
    "                if (staged != VK_NULL_HANDLE) {\n"
    "                    g_state->currentDrawID = (uint32_t)i;\n"
    "                    backend_draw_indexed((int)mode, (int)count[i], idx_type, staged, 0);\n"
    "                }\n",
    2,
)
# The preceding client-pointer replacement occurs in glMultiDrawElements first;
# reset after both VBO/client branches at the shared end.
replace_once(
    p,
    "    }\n    end_draw();\n}\n\nvoid glMultiDrawElementsBaseVertex",
    "    }\n"
    "    g_state->currentDrawID = 0;\n"
    "    end_draw();\n"
    "}\n\nvoid glMultiDrawElementsBaseVertex"
)
replace_once(
    p,
    "            g_state->currentBaseVertex = basevertex[i];\n            backend_draw_indexed((int)mode, (int)count[i], idx_type, ib,\n",
    "            g_state->currentBaseVertex = basevertex[i];\n"
    "            g_state->currentDrawID = (uint32_t)i;\n"
    "            backend_draw_indexed((int)mode, (int)count[i], idx_type, ib,\n"
)
# Target the BaseVertex client-pointer branch via its baseVertex assignment.
replace_once(
    p,
    "                g_state->currentBaseVertex = basevertex[i];\n                GLuint transient = (GLuint)(uintptr_t)indices[i];\n",
    "                g_state->currentBaseVertex = basevertex[i];\n"
    "                g_state->currentDrawID = (uint32_t)i;\n"
    "                GLuint transient = (GLuint)(uintptr_t)indices[i];\n"
)
replace_once(
    p,
    "        g_state->currentBaseVertex = 0;\n    } else if (basevertex) {",
    "        g_state->currentBaseVertex = 0;\n"
    "        g_state->currentDrawID = 0;\n"
    "    } else if (basevertex) {"
)
replace_once(
    p,
    "        g_state->currentBaseVertex = 0;\n    }\n    end_draw();\n}\n\n/* =========================================================================\n * Indirect draw",
    "        g_state->currentBaseVertex = 0;\n"
    "        g_state->currentDrawID = 0;\n"
    "    }\n"
    "    end_draw();\n"
    "}\n\n/* =========================================================================\n * Indirect draw"
)

m = "Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalCommandStream.mm"
replace_once(
    m,
    "NSInteger current_base_vertex() {\n    return mithril::g_state ? (NSInteger)mithril::g_state->currentBaseVertex : 0;\n}\n\n/* ---- Primitive expansion",
    "NSInteger current_base_vertex() {\n"
    "    return mithril::g_state ? (NSInteger)mithril::g_state->currentBaseVertex : 0;\n"
    "}\n"
    "uint32_t current_draw_id() {\n"
    "    return mithril::g_state ? mithril::g_state->currentDrawID : 0u;\n"
    "}\n"
    "// SPIRV-Cross lowers SPIR-V DrawIndex to a constant uint pointer at its\n"
    "// reserved draw_id_buffer_index (19 by default). Metal has no native\n"
    "// multi-draw ordinal for the CPU-expanded paths below, so bind the exact\n"
    "// OpenGL sub-draw ordinal as an inline constant before every native draw.\n"
    "// Vertex attributes occupy only slots 0..15 in this backend, leaving 19\n"
    "// reserved exactly as required by our pinned SPIRV-Cross ABI.\n"
    "void bind_draw_id(uint32_t drawID) {\n"
    "    if (g_renderEncoder != nil) {\n"
    "        [g_renderEncoder setVertexBytes:&drawID length:sizeof(drawID) atIndex:19];\n"
    "    }\n"
    "}\n\n/* ---- Primitive expansion"
)
replace_once(
    m,
    "    id<MTLRenderCommandEncoder> r = g_renderEncoder;\n    if (count == 0) return;\n\n    if (primitive == GL_TRIANGLE_FAN)",
    "    id<MTLRenderCommandEncoder> r = g_renderEncoder;\n"
    "    if (count == 0) return;\n"
    "    bind_draw_id(current_draw_id());\n\n"
    "    if (primitive == GL_TRIANGLE_FAN)"
)
replace_once(
    m,
    "    id<MTLRenderCommandEncoder> r = g_renderEncoder;\n    if (count == 0 || !index_buffer || index_buffer->buf == nil) return;\n    const bool fan",
    "    id<MTLRenderCommandEncoder> r = g_renderEncoder;\n"
    "    if (count == 0 || !index_buffer || index_buffer->buf == nil) return;\n"
    "    bind_draw_id(current_draw_id());\n"
    "    const bool fan"
)
# Non-indexed indirect: bind 0/saved ordinal on single record, and i on each
# record of CPU-expanded/non-default-stride multi-draw.
replace_once(
    m,
    "        if (count == 1 && effStride == 16) {\n            [g_renderEncoder drawPrimitives:primitive_from_gl(primitive)\n",
    "        if (count == 1 && effStride == 16) {\n"
    "            bind_draw_id(current_draw_id());\n"
    "            [g_renderEncoder drawPrimitives:primitive_from_gl(primitive)\n"
)
replace_once(
    m,
    "    for (int i = 0; i < count; ++i) {\n        MtlDrawPrimitivesArgs args;",
    "    const uint32_t savedDrawID = current_draw_id();\n"
    "    for (int i = 0; i < count; ++i) {\n"
    "        if (mithril::g_state) mithril::g_state->currentDrawID = (uint32_t)i;\n"
    "        MtlDrawPrimitivesArgs args;"
)
replace_once(
    m,
    "        } else {\n            [g_renderEncoder drawPrimitives:primitive_from_gl(primitive)\n                              indirectBuffer:indirect->buf\n                        indirectBufferOffset:offset + (NSUInteger)i * effStride];\n        }\n    }\n}\n\nvoid draw_indexed_indirect",
    "        } else {\n"
    "            bind_draw_id(current_draw_id());\n"
    "            [g_renderEncoder drawPrimitives:primitive_from_gl(primitive)\n"
    "                              indirectBuffer:indirect->buf\n"
    "                        indirectBufferOffset:offset + (NSUInteger)i * effStride];\n"
    "        }\n"
    "    }\n"
    "    if (mithril::g_state) mithril::g_state->currentDrawID = savedDrawID;\n"
    "}\n\nvoid draw_indexed_indirect"
)
# Indexed indirect, same ordinal policy.
replace_once(
    m,
    "        // Fast path: one record, plain primitive, app index buffer as-is.\n        [g_renderEncoder drawIndexedPrimitives:primitive_from_gl(primitive)\n",
    "        // Fast path: one record, plain primitive, app index buffer as-is.\n"
    "        bind_draw_id(current_draw_id());\n"
    "        [g_renderEncoder drawIndexedPrimitives:primitive_from_gl(primitive)\n"
)
replace_once(
    m,
    "    for (int i = 0; i < count; ++i) {\n        MtlDrawIndexedPrimitivesArgs args;",
    "    const uint32_t savedDrawID = current_draw_id();\n"
    "    for (int i = 0; i < count; ++i) {\n"
    "        if (mithril::g_state) mithril::g_state->currentDrawID = (uint32_t)i;\n"
    "        MtlDrawIndexedPrimitivesArgs args;"
)
replace_once(
    m,
    "        } else {\n            [g_renderEncoder drawIndexedPrimitives:primitive_from_gl(primitive)\n                                         indexType:index_type_from_int(index_type)\n                                       indexBuffer:index_buffer->buf\n                                 indexBufferOffset:index_offset\n                                    indirectBuffer:indirect->buf\n                              indirectBufferOffset:offset + (NSUInteger)i * effStride];\n        }\n    }\n}\n\n/* ---- GL 4.6 ARB_indirect_parameters",
    "        } else {\n"
    "            bind_draw_id(current_draw_id());\n"
    "            [g_renderEncoder drawIndexedPrimitives:primitive_from_gl(primitive)\n"
    "                                         indexType:index_type_from_int(index_type)\n"
    "                                       indexBuffer:index_buffer->buf\n"
    "                                 indexBufferOffset:index_offset\n"
    "                                    indirectBuffer:indirect->buf\n"
    "                              indirectBufferOffset:offset + (NSUInteger)i * effStride];\n"
    "        }\n"
    "    }\n"
    "    if (mithril::g_state) mithril::g_state->currentDrawID = savedDrawID;\n"
    "}\n\n/* ---- GL 4.6 ARB_indirect_parameters"
)

print("DrawID semantics patch applied")
