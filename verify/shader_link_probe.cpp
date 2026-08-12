// 验证重构后的 Mithril Shader.cpp：
//  shader_link_program() —— 单 TProgram + mapIO + SPIRV-Reflect binding 重排
// 验证点：
//  1) VS/FS 编译成功（EShClientVulkan + relaxed，gl_VertexID 原生可用）
//  2) attrib locations 正确（glBindAttribLocation 通过 IoResolver 生效）
//  3) descriptor binding 跨 stage 不冲突（VS 资源 0..N，FS 接着 N+1..M）
//  4) 两个 flip 变体 binding 一致
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>

#include <GL/gl.h>
#include <spirv_cross.hpp>

#include "../Mithril-Wrapper-cpp/MG_Impl/Shader.h"

static void dump_resources(const std::vector<uint32_t>& spirv, const char* tag) {
    spirv_cross::Compiler compiler(spirv.data(), spirv.size());
    spirv_cross::ShaderResources res = compiler.get_shader_resources();
    printf("== %s ==\n", tag);
    for (auto& r : res.uniform_buffers) {
        unsigned set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        unsigned b   = compiler.get_decoration(r.id, spv::DecorationBinding);
        printf("  UBO '%s' set=%u binding=%u\n", r.name.c_str(), set, b);
    }
    for (auto& r : res.sampled_images) {
        unsigned set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        unsigned b   = compiler.get_decoration(r.id, spv::DecorationBinding);
        printf("  SAMPLER '%s' set=%u binding=%u\n", r.name.c_str(), set, b);
    }
    for (auto& r : res.stage_inputs) {
        unsigned loc = compiler.get_decoration(r.id, spv::DecorationLocation);
        printf("  INPUT '%s' location=%u\n", r.name.c_str(), loc);
    }
}

int main() {
    const char* vs_src = R"(#version 330 core
in vec3 Position;
in vec2 UV0;
in vec4 Color;
uniform mat4 ModelViewMat;
uniform mat4 ProjMat;
uniform sampler2D Sampler2;
out vec2 texCoord0;
out vec4 vertexColor;
void main() {
    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);
    texCoord0 = UV0;
    vertexColor = Color;
    float unused = float(gl_VertexID);   // gl_VertexID must compile natively
    gl_Position.y += unused * 0.0;
}
)";
    const char* fs_src = R"(#version 330 core
in vec2 texCoord0;
in vec4 vertexColor;
uniform sampler2D Sampler0;
uniform vec4 ColorModulator;
uniform float FogStart;
uniform float FogEnd;
uniform vec4 FogColor;
out vec4 fragColor;
void main() {
    vec4 color = texture(Sampler0, texCoord0) * vertexColor * ColorModulator;
    fragColor = color;
}
)";

    std::unordered_map<std::string, GLuint> attribs;
    attribs["Position"] = 0;
    attribs["UV0"] = 1;
    attribs["Color"] = 2;

    printf("calling shader_link_program...\n");
    fflush(stdout);
    mithril::ShaderLinkOutput out;
    std::string info;
    if (!mithril::shader_link_program(vs_src, fs_src, &attribs, out, info)) {
        printf("FAIL: shader_link_program: %s\n", info.c_str());
        return 1;
    }
    printf("link returned OK\n"); fflush(stdout);
    printf("link OK: VS=%zu VSflip=%zu FS=%zu words\n",
           out.vertexSpirv.size(), out.vertexSpirvFlipped.size(), out.fragmentSpirv.size());

    dump_resources(out.vertexSpirv, "VS (non-flip)");
    dump_resources(out.fragmentSpirv, "FS");
    dump_resources(out.vertexSpirvFlipped, "VS (flip)");

    // 校验：binding 不冲突 + flip 变体一致
    auto bindings_of = [](const std::vector<uint32_t>& spv, std::vector<std::pair<std::string,int>>& out_list) {
        spirv_cross::Compiler c(spv.data(), spv.size());
        spirv_cross::ShaderResources r = c.get_shader_resources();
        for (auto& u : r.uniform_buffers)
            out_list.emplace_back(u.name, c.get_decoration(u.id, spv::DecorationBinding));
        for (auto& s : r.sampled_images)
            out_list.emplace_back(s.name, c.get_decoration(s.id, spv::DecorationBinding));
    };
    std::vector<std::pair<std::string,int>> vb, fb, vbf;
    bindings_of(out.vertexSpirv, vb);
    bindings_of(out.fragmentSpirv, fb);
    bindings_of(out.vertexSpirvFlipped, vbf);

    bool ok = true;
    // VS 与 FS 的资源数：VS 2 个（UBO+Sampler2），FS 2 个（UBO+Sampler0）
    if (vb.size() != 2 || fb.size() != 2) { printf("FAIL: unexpected resource counts\n"); ok = false; }
    // VS 和 FS 的 binding 不重叠
    for (auto& a : vb) for (auto& b : fb) {
        if (a.second == b.second) { printf("FAIL: binding collision %s=%d vs %s=%d\n", a.first.c_str(), a.second, b.first.c_str(), b.second); ok = false; }
    }
    // flip 与非 flip 的 VS binding 一致
    for (size_t i = 0; i < vb.size() && i < vbf.size(); ++i) {
        if (vb[i].second != vbf[i].second) { printf("FAIL: flip binding mismatch %s=%d vs %d\n", vb[i].first.c_str(), vb[i].second, vbf[i].second); ok = false; }
    }
    // attrib location：Position=0, UV0=1, Color=2
    {
        spirv_cross::Compiler c(out.vertexSpirv.data(), out.vertexSpirv.size());
        spirv_cross::ShaderResources r = c.get_shader_resources();
        std::unordered_map<std::string,int> locs;
        for (auto& i : r.stage_inputs) locs[i.name] = c.get_decoration(i.id, spv::DecorationLocation);
        if (locs["Position"] != 0 || locs["UV0"] != 1 || locs["Color"] != 2) {
            printf("FAIL: attrib locations Position=%d UV0=%d Color=%d\n", locs["Position"], locs["UV0"], locs["Color"]);
            ok = false;
        } else printf("attrib locations OK\n");
    }

    // 无 attrib bindings 的情况
    mithril::ShaderLinkOutput out2;
    if (!mithril::shader_link_program(vs_src, fs_src, nullptr, out2, info)) {
        printf("FAIL: link without attribs: %s\n", info.c_str());
        ok = false;
    } else printf("link without attrib bindings OK\n");

    // 编译失败的情况（坏 shader）
    if (mithril::shader_compile_stage(GL_VERTEX_SHADER, "this is not glsl", info)) {
        printf("FAIL: bad shader should not compile\n");
        ok = false;
    } else printf("bad shader correctly rejected\n");

    // fallback shader
    std::vector<uint32_t> fb_spv;
    if (!mithril::get_fallback_spirv(GL_FRAGMENT_SHADER, false, fb_spv)) {
        printf("FAIL: fallback FS\n");
        ok = false;
    } else printf("fallback FS OK (%zu words)\n", fb_spv.size());

    // ---- MC 1.21.1 真实风格：#version 150 blit_screen（验证版本升级 + relaxed）----
    {
        const char* vs150 = R"(#version 150
in vec4 Position;
in vec2 UV0;
uniform sampler2D Sampler0;
uniform mat4 ModelViewMat;
uniform mat4 ProjMat;
out vec2 texCoord0;
void main() {
    gl_Position = ProjMat * ModelViewMat * vec4(Position.xy, 0.0, 1.0);
    texCoord0 = UV0;
}
)";
        const char* fs150 = R"(#version 150
uniform sampler2D Sampler0;
uniform vec4 ColorModulator;
in vec2 texCoord0;
out vec4 fragColor;
void main() {
    vec4 color = texture(Sampler0, texCoord0);
    if (color.a < 0.1) discard;
    fragColor = color * ColorModulator;
}
)";
        mithril::ShaderLinkOutput out150;
        if (!mithril::shader_link_program(vs150, fs150, nullptr, out150, info)) {
            printf("FAIL: MC 150 blit_screen link: %s\n", info.c_str());
            ok = false;
        } else {
            printf("MC #version 150 blit_screen link OK (%zu/%zu/%zu words)\n",
                   out150.vertexSpirv.size(), out150.vertexSpirvFlipped.size(),
                   out150.fragmentSpirv.size());
            // 校验 binding：VS(UBO,Sampler0) + FS(UBO,Sampler0) = 4 个 binding
            spirv_cross::Compiler cvs(out150.vertexSpirv.data(), out150.vertexSpirv.size());
            spirv_cross::Compiler cfs(out150.fragmentSpirv.data(), out150.fragmentSpirv.size());
            int vsb = (int)cvs.get_shader_resources().uniform_buffers.size() +
                      (int)cvs.get_shader_resources().sampled_images.size();
            int fsb = (int)cfs.get_shader_resources().uniform_buffers.size() +
                      (int)cfs.get_shader_resources().sampled_images.size();
            if (vsb != 2 || fsb != 2) {
                printf("FAIL: blit_screen resource counts VS=%d FS=%d\n", vsb, fsb);
                ok = false;
            } else printf("blit_screen bindings OK\n");
        }
    }

    printf(ok ? "ALL CHECKS PASSED\n" : "CHECKS FAILED\n");
    return ok ? 0 : 1;
}
