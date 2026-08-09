// Probe: does glslang EShClientOpenGL + EShMsgVulkanRules emit
// OpTypeSampledImage (combined) or separate OpTypeImage + OpTypeSampler
// for a plain `uniform sampler2D Sampler0;`?
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    glslang::InitializeProcess();
    const char* src =
        "#version 330 core\n"
        "uniform sampler2D Sampler0;\n"
        "uniform vec4 ColorModulator;\n"
        "in vec2 v_uv;\n"
        "out vec4 frag;\n"
        "void main(){ frag = texture(Sampler0, v_uv) * ColorModulator; }\n";
    EShLanguage stage = EShLangFragment;
    glslang::TShader shader(stage);
    const char* s = src;
    shader.setStrings(&s, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 330);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    shader.setAutoMapLocations(true);
    shader.setAutoMapBindings(true);
    shader.setShiftBinding(glslang::EResUbo, 64);
    shader.setShiftBinding(glslang::EResTexture, 64);
    shader.setShiftBinding(glslang::EResSampler, 64);
    EShMessages messages = static_cast<EShMessages>(
        EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 330, true, messages)) {
        printf("PARSE FAIL: %s\n", shader.getInfoLog());
        return 1;
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(messages)) {
        printf("LINK FAIL: %s\n", prog.getInfoLog());
        return 1;
    }
    glslang::TIntermediate* inter = prog.getIntermediate(stage);
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*inter, spirv, nullptr);
    printf("SPIR-V words: %zu\n", spirv.size());

    printf("=== SPIR-V type opcodes ===\n");
    for (size_t i = 5; i < spirv.size();) {
        uint32_t op = spirv[i] & 0xFFFF;
        uint32_t wc  = spirv[i] >> 16;
        if (wc == 0) break;
        const char* name = nullptr;
        switch (op) {
            case 25: name = "OpTypeImage"; break;
            case 26: name = "OpTypeSampler"; break;
            case 27: name = "OpTypeSampledImage"; break;
            case 71: name = "OpVariable"; break;
        }
        if (name) {
            printf("  [%zu] %s (wc=%u)", i, name, wc);
            for (uint32_t k = 1; k < wc && i + k < spirv.size(); ++k)
                printf(" %u", spirv[i + k]);
            printf("\n");
        }
        i += wc;
    }

    spirv_cross::Compiler c(spirv.data(), spirv.size());
    auto res = c.get_shader_resources();
    printf("=== SPIRV-Cross reflection ===\n");
    printf("uniform_buffers:   %zu\n", res.uniform_buffers.size());
    for (auto& r : res.uniform_buffers)
        printf("  ubo '%s' set=%u binding=%u\n", r.name.c_str(),
               c.get_decoration(r.id, 0x1B), c.get_decoration(r.id, 0x1A));
    printf("sampled_images:    %zu\n", res.sampled_images.size());
    for (auto& r : res.sampled_images)
        printf("  sampled '%s' set=%u binding=%u\n", r.name.c_str(),
               c.get_decoration(r.id, 0x1B), c.get_decoration(r.id, 0x1A));
    printf("separate_images:   %zu\n", res.separate_images.size());
    for (auto& r : res.separate_images)
        printf("  sepimg '%s' set=%u binding=%u\n", r.name.c_str(),
               c.get_decoration(r.id, 0x1B), c.get_decoration(r.id, 0x1A));
    printf("separate_samplers: %zu\n", res.separate_samplers.size());
    for (auto& r : res.separate_samplers)
        printf("  sepsmp '%s' set=%u binding=%u\n", r.name.c_str(),
               c.get_decoration(r.id, 0x1B), c.get_decoration(r.id, 0x1A));
    return 0;
}
