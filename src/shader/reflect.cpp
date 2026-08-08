// Mithril-Wrapper shader module -- program/uniform/attribute reflection
// (M2-S2). SPIRV-Cross reflects the linked stage SPIR-V so
// glGetUniformLocation/glGetAttribLocation and the uniform getters answer
// honestly.

#include <shader/shader.h>

#include <GL/glcorearb.h>

#include <spirv_cross.hpp>

#include <util/log.h>

#include <algorithm>
#include <string>
#include <vector>

namespace mithril::shader {

void ReflectProgram(Program& prog) {
    prog.uniforms.clear();
    prog.uniform_by_name.clear();
    prog.uniform_by_location.clear();
    prog.attrib_locations.clear();
    prog.samplers.clear();

    auto reflect_stage = [&](const std::vector<uint32_t>& words) {
        if (words.empty()) return;
        try {
            spirv_cross::Compiler compiler(words.data(), words.size());
            spirv_cross::ShaderResources res = compiler.get_shader_resources();

            // Uniform block members ($Global wrap -> original GL uniform names).
            for (auto& r : res.uniform_buffers) {
                const spirv_cross::SPIRType& t = compiler.get_type(r.base_type_id);
                for (uint32_t i = 0; i < t.member_types.size(); ++i) {
                    std::string name = compiler.get_member_name(r.base_type_id, i);
                    if (name.empty()) continue;
                    prog.uniform_by_name[name] = -1;  // placeholder; location below
                }
            }
            // Samplers / standalone uniforms.
            auto add_sampler = [&](spirv_cross::Resource& r) {
                std::string name = r.name;
                if (name.empty()) return;
                if (prog.uniform_by_name.find(name) == prog.uniform_by_name.end())
                    prog.uniform_by_name[name] = -1;
                uint32_t binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                bool dup = false;
                for (auto& s : prog.samplers)
                    if (s.name == name) { dup = true; break; }
                if (!dup)
                    prog.samplers.push_back({name, GL_SAMPLER_2D, binding, -1});
            };
            for (auto& r : res.sampled_images) add_sampler(r);
            for (auto& r : res.separate_images) add_sampler(r);
            for (auto& r : res.separate_samplers) add_sampler(r);

            // Attributes: location + name.
            for (auto& r : res.stage_inputs) {
                if (r.name.empty()) continue;
                int loc = static_cast<int>(compiler.get_decoration(r.id, spv::DecorationLocation));
                prog.attrib_locations[r.name] = loc;
            }
        } catch (const std::exception& e) {
            ML_LOG_WARN("SPIRV-Cross reflection failed: %s", e.what());
        }
    };

    reflect_stage(prog.vertex_spirv);
    reflect_stage(prog.fragment_spirv);

    // Assign stable locations in a deterministic order (alphabetical).
    std::vector<std::string> names;
    names.reserve(prog.uniform_by_name.size());
    for (auto& kv : prog.uniform_by_name) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size(); ++i) {
        Uniform u;
        u.name = names[i];
        u.location = static_cast<GLint>(i);
        prog.uniform_by_location[static_cast<GLint>(i)] = prog.uniforms.size();
        prog.uniform_by_name[names[i]] = static_cast<GLint>(i);
        prog.uniforms.push_back(std::move(u));
    }
    for (auto& s : prog.samplers) {
        auto it = prog.uniform_by_name.find(s.name);
        s.location = it == prog.uniform_by_name.end() ? -1 : it->second;
        for (auto& u : prog.uniforms)
            if (u.name == s.name) u.type = s.type;
    }
}

} // namespace mithril::shader
