#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp')
s = p.read_text()
anchor = '''void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
'''
helper = r'''static void mithril_probe_index_stream(GLsizei count, GLenum type, const void* indices) {
    static unsigned seq = 0;
    if (seq >= 240 || count <= 0) return;
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao || vao->elementArrayBuffer == 0) return;
    mithril::Buffer* ib = mithril::state_get_buffer(vao->elementArrayBuffer);
    if (!ib) return;
    const size_t off = (size_t)(uintptr_t)indices;
    const size_t elem = type == GL_UNSIGNED_INT ? 4u : type == GL_UNSIGNED_BYTE ? 1u : 2u;
    const size_t bytes = (size_t)count * elem;
    if (off > ib->data.size() || bytes > ib->data.size() - off) {
        MITHRIL_LOG_WARN("vk", "INDEX_PROBE seq=%u prog=%u vao=%u ib=%u off=%zu count=%d elem=%zu shadow=%zu OOB=1",
                         seq++, (unsigned)g_state->currentProgram, (unsigned)g_state->currentVAO,
                         (unsigned)vao->elementArrayBuffer, off, (int)count, elem, ib->data.size());
        return;
    }
    uint32_t minIndex = 0xffffffffu, maxIndex = 0;
    const uint8_t* base = ib->data.data() + off;
    for (GLsizei i = 0; i < count; ++i) {
        uint32_t idx = 0;
        if (elem == 1) idx = base[i];
        else if (elem == 2) { uint16_t v; std::memcpy(&v, base + (size_t)i * 2u, 2); idx = v; }
        else { uint32_t v; std::memcpy(&v, base + (size_t)i * 4u, 4); idx = v; }
        minIndex = std::min(minIndex, idx);
        maxIndex = std::max(maxIndex, idx);
    }
    size_t posVertices = 0;
    int posStride = 0;
    size_t posBase = 0;
    if (vao->attribs[0].enabled && vao->attribs[0].bindingIndex < (GLuint)mithril::kMaxVertexBindings) {
        const mithril::VertexBinding& vbnd = vao->bindings[vao->attribs[0].bindingIndex];
        mithril::Buffer* vb = mithril::state_get_buffer(vbnd.buffer);
        posStride = vbnd.stride;
        posBase = (size_t)vbnd.offset + (size_t)vao->attribs[0].relativeOffset;
        if (vb && posStride > 0 && posBase < vb->data.size())
            posVertices = (vb->data.size() - posBase) / (size_t)posStride;
    }
    const long long baseVertex = (long long)g_state->currentBaseVertex;
    const long long effectiveMax = (long long)maxIndex + baseVertex;
    const int invalid = posVertices > 0 && (effectiveMax < 0 || (uint64_t)effectiveMax >= posVertices);
    MITHRIL_LOG_WARN("vk", "INDEX_PROBE seq=%u prog=%u vao=%u ib=%u off=%zu count=%d elem=%zu min=%u max=%u baseVertex=%lld effectiveMax=%lld posVertices=%zu posStride=%d posBase=%zu INVALID=%d",
                     seq++, (unsigned)g_state->currentProgram, (unsigned)g_state->currentVAO,
                     (unsigned)vao->elementArrayBuffer, off, (int)count, elem,
                     minIndex, maxIndex, baseVertex, effectiveMax, posVertices, posStride, posBase, invalid);
}

'''
assert s.count(anchor) == 1
s = s.replace(anchor, helper + anchor, 1)
old = '''    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    VkBuffer ib = backend_get_buffer(ib_name);
'''
new = '''    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    mithril_probe_index_stream(count, type, indices);
    VkBuffer ib = backend_get_buffer(ib_name);
'''
# Occurs in direct and instanced paths; instrument both.
assert s.count(old) >= 2, s.count(old)
s = s.replace(old, new)
p.write_text(s)
