from pathlib import Path

path = Path("Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalCommandStream.mm")
text = path.read_text()

old = '''    bool disabledU16MaxNeedsWiden = false;
    if (!restartCanMatch && index_type == 0 && stripPrimitive) {
        const NSUInteger sourceBytes = (NSUInteger)count * sizeof(uint16_t);
        if (index_buffer->contents == nullptr || index_offset > index_buffer->capacity ||
            sourceBytes > index_buffer->capacity - index_offset) {
            static uint32_t warned = 0;
            warn_limited(warned, "dmt", "U16 strip sentinel scan needs a valid CPU-readable index slice — draw dropped");
            return;
        }
        const uint16_t* s = (const uint16_t*)((const uint8_t*)index_buffer->contents + index_offset);
        for (uint32_t i = 0; i < count; ++i) {
            if (s[i] == 0xffffu) {
                disabledU16MaxNeedsWiden = true;
                break;
            }
        }
    }

    // Metal only accepts U16/U32 indices. U8 therefore always needs widening;
'''
new = '''    bool disabledU16MaxNeedsWiden = false;
    if (!restartCanMatch && index_type == 0 && stripPrimitive) {
        const NSUInteger sourceBytes = (NSUInteger)count * sizeof(uint16_t);
        if (index_buffer->contents == nullptr || index_offset > index_buffer->capacity ||
            sourceBytes > index_buffer->capacity - index_offset) {
            static uint32_t warned = 0;
            warn_limited(warned, "dmt", "U16 strip sentinel scan needs a valid CPU-readable index slice — draw dropped");
            return;
        }
        const uint16_t* s = (const uint16_t*)((const uint8_t*)index_buffer->contents + index_offset);
        for (uint32_t i = 0; i < count; ++i) {
            if (s[i] == 0xffffu) {
                disabledU16MaxNeedsWiden = true;
                break;
            }
        }
    }

    // U32 cannot be widened.  Metal's UInt32 maximum is a native strip restart
    // sentinel, while OpenGL allows 0xffffffff as an ordinary index when
    // restart is disabled, or when programmable restart selects a different
    // token.  Detect only the slices that contain that representability
    // conflict.  They are expanded below to triangle/line lists, where hosted
    // Metal GPU tests prove 0xffffffff reaches vertex_id as an ordinary index.
    bool u32StripMaxConflict = false;
    if (index_type == 1 && stripPrimitive && !nativeRestartToken) {
        const NSUInteger sourceBytes = (NSUInteger)count * sizeof(uint32_t);
        if (index_buffer->contents == nullptr || index_offset > index_buffer->capacity ||
            sourceBytes > index_buffer->capacity - index_offset) {
            static uint32_t warned = 0;
            warn_limited(warned, "dmt", "U32 strip sentinel scan needs a valid CPU-readable index slice — draw dropped");
            return;
        }
        const uint32_t* s = (const uint32_t*)((const uint8_t*)index_buffer->contents + index_offset);
        for (uint32_t i = 0; i < count; ++i) {
            if (s[i] == 0xffffffffu &&
                !(restartCanMatch && restartToken == 0xffffffffu)) {
                u32StripMaxConflict = true;
                break;
            }
        }
    }

    // Metal only accepts U16/U32 indices. U8 therefore always needs widening;
'''
if text.count(old) != 1:
    raise SystemExit(f"scan insertion expected once, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (index_type != 2 && !fan && !loop && !needsRestartRemap &&
        !disabledU16MaxNeedsWiden) {
'''
new = '''    if (index_type != 2 && !fan && !loop && !needsRestartRemap &&
        !disabledU16MaxNeedsWiden && !u32StripMaxConflict) {
'''
if text.count(old) != 1:
    raise SystemExit(f"fast path guard expected once, found {text.count(old)}")
text = text.replace(old, new, 1)

anchor = '''    // Fan and loop need explicit per-restart segmentation before their Metal
    // primitive expansion. Restart comparison deliberately precedes
    // baseVertex, matching OpenGL's indexed assembly semantics.
    if (fan) {
'''
insert = '''    // A U32 strip containing an ordinary 0xffffffff cannot be represented by
    // Metal's native strip topology because Metal consumes that value as a
    // restart sentinel.  Preserve OpenGL assembly exactly by segmenting on the
    // *actual* OpenGL restart token (if any) and expanding each strip segment
    // to a list.  For triangle strips, swap the first two indices of odd
    // triangles: this preserves strip winding while keeping the provoking
    // vertex (the third/last vertex) unchanged.  Line strips become line-list
    // pairs.  The index values themselves, including 0xffffffff, stay intact.
    if (u32StripMaxConflict) {
        static thread_local std::vector<uint32_t> expandedStrip;
        expandedStrip.clear();
        size_t begin = 0;
        while (begin < gather.size()) {
            size_t end = begin;
            while (end < gather.size() &&
                   !(restartCanMatch && gather[end] == restartToken)) ++end;
            const size_t n = end - begin;
            if (primitive == GL_TRIANGLE_STRIP && n >= 3) {
                if (n > (SIZE_MAX / 3u) + 2u) return;
                expandedStrip.reserve(expandedStrip.size() + (n - 2u) * 3u);
                for (size_t t = 0; t + 2u < n; ++t) {
                    const uint32_t a = gather[begin + t];
                    const uint32_t b = gather[begin + t + 1u];
                    const uint32_t c = gather[begin + t + 2u];
                    if ((t & 1u) == 0u) {
                        expandedStrip.push_back(a);
                        expandedStrip.push_back(b);
                    } else {
                        expandedStrip.push_back(b);
                        expandedStrip.push_back(a);
                    }
                    expandedStrip.push_back(c);
                }
            } else if (primitive == GL_LINE_STRIP && n >= 2) {
                if (n > (SIZE_MAX / 2u) + 1u) return;
                expandedStrip.reserve(expandedStrip.size() + (n - 1u) * 2u);
                for (size_t t = 0; t + 1u < n; ++t) {
                    expandedStrip.push_back(gather[begin + t]);
                    expandedStrip.push_back(gather[begin + t + 1u]);
                }
            }
            begin = end + (end < gather.size() ? 1u : 0u);
        }
        if (expandedStrip.empty()) return;
        if (expandedStrip.size() > SIZE_MAX / sizeof(uint32_t)) return;
        const NSUInteger bytes = (NSUInteger)(expandedStrip.size() * sizeof(uint32_t));
        id<MTLBuffer> sb; NSUInteger off; void* ptr;
        if (!scratch_alloc(bytes, sb, off, ptr)) return;
        std::memcpy(ptr, expandedStrip.data(), bytes);
        scratch_flush(sb, off, bytes);
        [r drawIndexedPrimitives:(primitive == GL_TRIANGLE_STRIP
                                      ? MTLPrimitiveTypeTriangle
                                      : MTLPrimitiveTypeLine)
                       indexCount:expandedStrip.size()
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:sb indexBufferOffset:off
                   instanceCount:instanceCount baseVertex:baseVertex
                    baseInstance:baseInstance];
        return;
    }

    // Fan and loop need explicit per-restart segmentation before their Metal
    // primitive expansion. Restart comparison deliberately precedes
    // baseVertex, matching OpenGL's indexed assembly semantics.
    if (fan) {
'''
if text.count(anchor) != 1:
    raise SystemExit(f"strip expansion anchor expected once, found {text.count(anchor)}")
text = text.replace(anchor, insert, 1)

old = '''    // Plain primitive adaptation. U8 widens to U16. A programmable U16
    // restart widens to U32 so ordinary 0xffff remains a vertex index while
    // the chosen token becomes Metal's 0xffffffff sentinel. U32 stays U32 and
    // remaps a custom token in-place; 0xffffffff cannot be represented as a
    // non-restart Metal index, but such a vertex index cannot address a real
    // Minecraft vertex buffer and is outside the practical terrain domain.
'''
new = '''    // Plain primitive adaptation. U8 widens to U16. A programmable U16
    // restart widens to U32 so ordinary 0xffff remains a vertex index while
    // the chosen token becomes Metal's 0xffffffff sentinel. U32 custom-token
    // strip collisions with an ordinary 0xffffffff were already expanded to
    // primitive lists above, so remapping here is now collision-free.
'''
if text.count(old) != 1:
    raise SystemExit(f"stale U32 limitation comment expected once, found {text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text)
print("U32 strip max-index semantic fix applied")
