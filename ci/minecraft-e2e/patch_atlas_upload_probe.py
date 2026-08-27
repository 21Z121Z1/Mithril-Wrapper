#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Resources.cpp')
s = p.read_text()

anchor = '''    // Copy pixel data into staging buffer at stagingOffset\n'''
insert = r'''    // EXPERIMENT: identify Minecraft's large atlases and prove what bytes
    // reach the Vulkan staging copy. This runs only for >=1024x1024 textures
    // and is removed once the atlas path is diagnosed.
    GLuint atlasProbeName = 0;
    if (tex.width >= 1024 && tex.height >= 1024) {
        for (auto& kv : texture_table()) {
            if (&kv.second == &tex) { atlasProbeName = kv.first; break; }
        }
    }
'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, insert + anchor, 1)

anchor2 = '''    if (!usedArena && stagingMapped) {\n'''
insert2 = r'''    if (atlasProbeName && stagingMapped && pixels) {
        static std::unordered_map<GLuint, int> atlasProbeCounts;
        int& n = atlasProbeCounts[atlasProbeName];
        if (n < 160) {
            auto hash_bytes = [](const uint8_t* p, size_t bytes) -> uint64_t {
                uint64_t h = 1469598103934665603ull;
                for (size_t i = 0; i < bytes; ++i) {
                    h ^= (uint64_t)p[i];
                    h *= 1099511628211ull;
                }
                return h;
            };
            const size_t sourceBytes = src_stride * (size_t)h * (size_t)d;
            const uint8_t* staged = (const uint8_t*)stagingMapped + stagingOffset;
            uint64_t srcHash = hash_bytes((const uint8_t*)pixels, sourceBytes);
            uint64_t dstHash = hash_bytes(staged, staging);
            size_t sampledNonZero = 0, sampledCount = 0;
            size_t step = staging > 65536 ? staging / 65536 : 1;
            for (size_t i = 0; i < staging; i += step) {
                sampledNonZero += staged[i] != 0;
                ++sampledCount;
            }
            MITHRIL_LOG_WARN("vk",
                "ATLAS_UPLOAD n=%d tex=%u image=%p %dx%d levels=%d level=%d dst=(%d,%d,%d) size=%dx%dx%d fmt=%d glfmt=0x%x type=0x%x full=%d arena=%d srcStride=%zu staging=%zu srcHash=%016llx dstHash=%016llx sampledNZ=%zu/%zu",
                n, (unsigned)atlasProbeName, (void*)tex.image, tex.width, tex.height,
                tex.levels, level, x, y, z, w, h, d, (int)tex.format,
                (unsigned)format, (unsigned)type, is_full_upload ? 1 : 0,
                usedArena ? 1 : 0, src_stride, staging,
                (unsigned long long)srcHash, (unsigned long long)dstHash,
                sampledNonZero, sampledCount);
            ++n;
        }
    }

'''
assert s.count(anchor2) == 1, s.count(anchor2)
s = s.replace(anchor2, insert2 + anchor2, 1)
p.write_text(s)
