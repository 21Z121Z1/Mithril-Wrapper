// Mithril-Wrapper - MG_Test/ReflectTest.cpp
// Unit tests for mithril::vk::reflect_stage / merge_bindings
// (MG_Backend/DirectVulkan/Reflect.cpp), which walk a SPIR-V module via
// SPIRV-Cross and produce DescriptorBinding records (set/binding/type/stageMask
// + UBO member layout).
//
// The test binary links Reflect.cpp (pure SPIRV-Cross reflection, no VkDevice)
// and Shader.cpp (mithril::shader_translate, which produces test SPIR-V via
// glslang). reflect_stage is stateless, so plain TEST() cases (no fixture) are
// sufficient.
#include <gtest/gtest.h>

#include "../MG_Backend/DirectVulkan/Reflect.h"
#include "../MG_Impl/Shader.h"

#include <GL/gl.h>

#include <cstdint>
#include <string>
#include <vector>

// A bare `uniform sampler2D tex;` in a fragment shader reflects to a single
// COMBINED_IMAGE_SAMPLER binding named "tex" tagged with the fragment stage.
TEST(Reflect, ReflectsSamplerBinding) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src =
        "#version 330 core\n"
        "uniform sampler2D tex;\n"
        "void main(){}\n";
    ASSERT_TRUE(mithril::shader_translate(GL_FRAGMENT_SHADER, src, spirv, info))
        << "info log: " << info;
    ASSERT_FALSE(spirv.empty());

    auto bindings = mithril::vk::reflect_stage(
        spirv.data(), static_cast<int>(spirv.size()), VK_SHADER_STAGE_FRAGMENT_BIT);
    ASSERT_EQ(bindings.size(), 1u);
    const auto& b = bindings[0];
    EXPECT_EQ(b.type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(b.descriptorCount, 1u);
    EXPECT_EQ(b.stageMask, VK_SHADER_STAGE_FRAGMENT_BIT);
    EXPECT_EQ(b.name, "tex");
}

// A `layout(set=0,binding=1) uniform Block { mat4 mvp; vec4 color; };` reflects
// to a single UBO binding whose two members are named "mvp" and "color" with
// std140 offsets 0 and 64 respectively. (set/binding may be auto-mapped to 0 by
// glslang, so those are not over-constrained here.) NOTE:
//   - `binding=` layout qualifier requires #version 420 (GL_ARB_shading_language_420pack);
//     330 rejects it with "'binding' : not supported for this version".
//   - SPIRV-Cross's get_active_buffer_ranges() only reports members the shader
//     actually references, so main() must read mvp/color — a void main(){} would
//     yield zero members even though the UBO is declared.
TEST(Reflect, ReflectsUboMembers) {
    std::vector<uint32_t> spirv;
    std::string info;
    std::string src =
        "#version 420 core\n"
        "layout(set=0,binding=1) uniform Block{mat4 mvp;vec4 color;};\n"
        "layout(location=0) out vec4 fragColor;\n"
        "void main(){fragColor=mvp*color;}\n";
    ASSERT_TRUE(mithril::shader_translate(GL_FRAGMENT_SHADER, src, spirv, info))
        << "info log: " << info;
    ASSERT_FALSE(spirv.empty());

    auto bindings = mithril::vk::reflect_stage(
        spirv.data(), static_cast<int>(spirv.size()), VK_SHADER_STAGE_FRAGMENT_BIT);
    ASSERT_EQ(bindings.size(), 1u);
    const auto& b = bindings[0];
    EXPECT_EQ(b.type, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_GT(b.bufferSize, 0u);
    ASSERT_EQ(b.members.size(), 2u);
    EXPECT_EQ(b.members[0].name, "mvp");
    EXPECT_EQ(b.members[1].name, "color");
    EXPECT_EQ(b.members[0].offset, 0u);
    // mat4 is 4*16 = 64 bytes in std140, so color starts at offset 64.
    EXPECT_EQ(b.members[1].offset, 64u);
}

// merge_bindings ORs stageMask for matching (set,binding,type) triples and
// appends bindings with distinct (set,binding,type) triples.
TEST(Reflect, MergeBindingsOrsStageMask) {
    // Same (set=0,binding=0,type=UBO) from VS and FS: stageMasks must OR.
    std::vector<mithril::vk::DescriptorBinding> vs;
    mithril::vk::DescriptorBinding vs_b{};
    vs_b.set = 0;
    vs_b.binding = 0;
    vs_b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    vs_b.stageMask = VK_SHADER_STAGE_VERTEX_BIT;
    vs.push_back(vs_b);

    std::vector<mithril::vk::DescriptorBinding> fs;
    mithril::vk::DescriptorBinding fs_b{};
    fs_b.set = 0;
    fs_b.binding = 0;
    fs_b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fs_b.stageMask = VK_SHADER_STAGE_FRAGMENT_BIT;
    fs.push_back(fs_b);

    auto merged = vs;
    mithril::vk::merge_bindings(merged, fs);
    ASSERT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].stageMask,
              static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT |
                                              VK_SHADER_STAGE_FRAGMENT_BIT));

    // Different (set,binding): VS {0,0} + FS {0,1} -> 2 bindings.
    std::vector<mithril::vk::DescriptorBinding> vs2;
    mithril::vk::DescriptorBinding vs2_b{};
    vs2_b.set = 0;
    vs2_b.binding = 0;
    vs2_b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    vs2_b.stageMask = VK_SHADER_STAGE_VERTEX_BIT;
    vs2.push_back(vs2_b);

    std::vector<mithril::vk::DescriptorBinding> fs2;
    mithril::vk::DescriptorBinding fs2_b{};
    fs2_b.set = 0;
    fs2_b.binding = 1;
    fs2_b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fs2_b.stageMask = VK_SHADER_STAGE_FRAGMENT_BIT;
    fs2.push_back(fs2_b);

    auto merged2 = vs2;
    mithril::vk::merge_bindings(merged2, fs2);
    EXPECT_EQ(merged2.size(), 2u);
}

// reflect_stage on null/zero-length SPIR-V or on garbage SPIR-V returns an
// empty vector without throwing (SPIRV-Cross exceptions are caught internally).
TEST(Reflect, BadSpirvReturnsEmptyNoThrow) {
    auto empty_null = mithril::vk::reflect_stage(nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT);
    EXPECT_TRUE(empty_null.empty());

    // A 4-word garbage blob that has neither a valid SPIR-V magic number nor a
    // sane header. SPIRV-Cross throws inside reflect_stage; the catch block
    // swallows the exception and returns an empty vector.
    const uint32_t garbage[4] = {0xDEADBEEFu, 0u, 0u, 0u};
    auto empty_garbage =
        mithril::vk::reflect_stage(garbage, 4, VK_SHADER_STAGE_VERTEX_BIT);
    EXPECT_TRUE(empty_garbage.empty());
}
