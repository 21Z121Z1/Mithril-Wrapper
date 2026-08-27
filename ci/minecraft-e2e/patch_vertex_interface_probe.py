#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.cpp')
s = p.read_text()
anchor = "    std::vector<uint32_t> shaderLocations =\n        reflect_vertex_input_locations(vertex_spirv, vertex_word_count);\n"
lines = [
    "    static unsigned mithril_vertex_iface_probe = 0;",
    "    if (mithril_vertex_iface_probe < 160) {",
    "        MITHRIL_LOG_WARN(\"vk\", \"VERTEX_IFACE_BEGIN seq=%u program=%u attrib_count=%d\",",
    "                         mithril_vertex_iface_probe, (unsigned)program, attrib_count);",
    "        for (int ai = 0; ai < attrib_count; ++ai) {",
    "            const MGVertexAttrib& pa = attribs[ai];",
    "            MITHRIL_LOG_WARN(\"vk\",",
    "                \"VERTEX_IFACE_VAO seq=%u program=%u slot=%d enabled=%d size=%d type=0x%x norm=%d integer=%d stride=%d offset=%d buffer=%u divisor=%d\",",
    "                mithril_vertex_iface_probe, (unsigned)program, pa.location, pa.enabled,",
    "                pa.size, (unsigned)pa.type, pa.normalized, pa.integer, pa.stride,",
    "                pa.offset, (unsigned)pa.buffer_name, pa.divisor);",
    "        }",
    "        try {",
    "            spirv_cross::Compiler ifaceCompiler(vertex_spirv, static_cast<size_t>(vertex_word_count));",
    "            auto ifaceResources = ifaceCompiler.get_shader_resources();",
    "            for (auto& input : ifaceResources.stage_inputs) {",
    "                const uint32_t loc = ifaceCompiler.get_decoration(input.id, spv::DecorationLocation);",
    "                std::string nm = ifaceCompiler.get_name(input.id);",
    "                if (nm.empty()) nm = input.name;",
    "                MITHRIL_LOG_WARN(\"vk\",",
    "                    \"VERTEX_IFACE_SPV seq=%u program=%u name=%s loc=%u id=%u\",",
    "                    mithril_vertex_iface_probe, (unsigned)program, nm.c_str(),",
    "                    loc, (unsigned)input.id);",
    "            }",
    "        } catch (const std::exception& e) {",
    "            MITHRIL_LOG_WARN(\"vk\", \"VERTEX_IFACE_REFLECT_FAIL seq=%u program=%u err=%s\",",
    "                             mithril_vertex_iface_probe, (unsigned)program, e.what());",
    "        }",
    "        MITHRIL_LOG_WARN(\"vk\", \"VERTEX_IFACE_END seq=%u program=%u\",",
    "                         mithril_vertex_iface_probe, (unsigned)program);",
    "        ++mithril_vertex_iface_probe;",
    "    }",
]
probe = anchor + "\n".join(lines) + "\n"
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, probe, 1)
p.write_text(s)
