#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp')
s = p.read_text()
anchor = '''    // Get-or-create the VkGraphicsPipeline. Blend state + colorWriteMask are\n'''
probe = r'''    // EXPERIMENT: byte-level vertex-input probe for the programs that dominate
    // world + GUI rendering in the real Minecraft E2E.  We inspect the GL
    // authoritative CPU shadow before any backend translation so a constant UV
    // stream can be separated from a Vulkan location/format bug.
    if (prog->id == 34 || prog->id == 58 || prog->id == 88) {
        static int uvProbeCount = 0;
        if (uvProbeCount < 48) {
            MITHRIL_LOG_WARN("probe", "UV_PROBE draw=%d prog=%u vao=%u attribs=%d fbo=%u",
                             uvProbeCount, (unsigned)prog->id,
                             (unsigned)g_state->currentVAO, attrib_count,
                             (unsigned)g_state->currentDrawFBO);
            for (int ai = 0; ai < attrib_count; ++ai) {
                const MGVertexAttrib& m = attribs[ai];
                const mithril::VertexAttrib& srcA = vao->attribs[m.location];
                const mithril::VertexBinding& vb = vao->bindings[srcA.bindingIndex];
                mithril::Buffer* buf = mithril::state_get_buffer(vb.buffer);
                MITHRIL_LOG_WARN("probe",
                    "UV_ATTR prog=%u loc=%d bindIndex=%u buf=%u type=0x%x size=%d norm=%d int=%d stride=%d bindOff=%lld relOff=%u shadow=%zu",
                    (unsigned)prog->id, m.location, (unsigned)srcA.bindingIndex,
                    (unsigned)vb.buffer, (unsigned)m.type, m.size, m.normalized,
                    m.integer, m.stride, (long long)vb.offset,
                    (unsigned)srcA.relativeOffset, buf ? buf->data.size() : 0u);
                if (!buf || buf->data.empty() || m.stride <= 0) continue;
                for (int vi = 0; vi < 4; ++vi) {
                    size_t off = (size_t)vb.offset + (size_t)vi * (size_t)m.stride
                               + (size_t)srcA.relativeOffset;
                    if (off >= buf->data.size()) continue;
                    const size_t avail = std::min<size_t>(16, buf->data.size() - off);
                    char hex[16 * 3 + 1] = {};
                    size_t hp = 0;
                    for (size_t bi = 0; bi < avail && hp + 3 < sizeof(hex); ++bi) {
                        hp += (size_t)std::snprintf(hex + hp, sizeof(hex) - hp, "%02x ",
                                                   (unsigned)buf->data[off + bi]);
                    }
                    if (m.type == GL_FLOAT) {
                        float f[4] = {0,0,0,0};
                        const int n = std::min(m.size, 4);
                        const size_t need = (size_t)n * sizeof(float);
                        if (off + need <= buf->data.size())
                            std::memcpy(f, buf->data.data() + off, need);
                        MITHRIL_LOG_WARN("probe",
                            "UV_VERT prog=%u loc=%d v=%d off=%zu float=(%.7g,%.7g,%.7g,%.7g) hex=%s",
                            (unsigned)prog->id, m.location, vi, off,
                            f[0], f[1], f[2], f[3], hex);
                    } else {
                        MITHRIL_LOG_WARN("probe",
                            "UV_VERT prog=%u loc=%d v=%d off=%zu hex=%s",
                            (unsigned)prog->id, m.location, vi, off, hex);
                    }
                }
            }
            ++uvProbeCount;
        }
    }

'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, probe + anchor, 1)
p.write_text(s)

p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.cpp')
s = p.read_text()
anchor = '''    if (shaderLocations.empty()) {\n        for (uint32_t loc = 0; loc < 16; ++loc) shaderLocations.push_back(loc);\n    }\n'''
insert = r'''    if (program == 34 || program == 58 || program == 88) {
        static std::unordered_set<GLuint> loggedPrograms;
        if (loggedPrograms.insert(program).second) {
            std::string locs;
            for (uint32_t loc : shaderLocations) {
                if (!locs.empty()) locs += ",";
                locs += std::to_string(loc);
            }
            MITHRIL_LOG_WARN("probe", "VK_INPUT_SHADER prog=%u locations=[%s] attribCount=%d",
                             (unsigned)program, locs.c_str(), attrib_count);
            for (int i = 0; i < attrib_count; ++i) {
                const auto& a = attribs[i];
                VkFormat pf = attrib_type_to_vk_format(a.type, a.size,
                                                       a.normalized != 0,
                                                       a.integer != 0);
                MITHRIL_LOG_WARN("probe",
                    "VK_INPUT_ATTR prog=%u loc=%d fmt=%d type=0x%x size=%d stride=%d relOff=%d divisor=%u",
                    (unsigned)program, a.location, (int)pf, (unsigned)a.type,
                    a.size, a.stride, a.offset, attrib_divisor(a.location));
            }
        }
    }
'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, anchor + insert, 1)
p.write_text(s)
