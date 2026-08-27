#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Impl/Texture.cpp')
s = p.read_text()

anchor = '''static bool checked_mul_size(size_t a, size_t b, size_t* result) {\n    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) return false;\n    *result = a * b;\n    return true;\n}\n'''
helper = r'''

static uint64_t atlas_trace_hash(const void* ptr, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(ptr);
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void atlas_trace_upload(const char* kind, const mithril::Texture* t,
                               GLint level, GLint x, GLint y,
                               GLsizei width, GLsizei height,
                               GLenum format, GLenum type, const void* pixels) {
    if (!t || t->width < 1024 || t->height < 1024) return;
    const int bpp = backend_host_texel_bytes(format, type);
    size_t logical = 0;
    if (bpp > 0 && width > 0 && height > 0 &&
        checked_mul_size((size_t)width, (size_t)height, &logical) &&
        checked_mul_size(logical, (size_t)bpp, &logical)) {
        const size_t sample = std::min<size_t>(logical, 65536u);
        const uint64_t hash = pixels && sample ? atlas_trace_hash(pixels, sample) : 0;
        const uint8_t* q = static_cast<const uint8_t*>(pixels);
        uint32_t first = 0;
        if (q && sample >= 4) std::memcpy(&first, q, 4);
        const GLuint pbo = g_state->bufferBindings[(int)mithril::BufferTarget::PixelUnpack].name;
        MITHRIL_LOG_WARN("vk",
            "ATLAS_UPLOAD kind=%s tex=%u base=%dx%d internal=0x%x level=%d rect=%d,%d %dx%d format=0x%x type=0x%x bpp=%d pbo=%u unpack=%d/%d/%d/%d/%d/%d sampleBytes=%zu hash=%016llx first4=%08x",
            kind, (unsigned)t->id, (int)t->width, (int)t->height,
            (unsigned)t->internalFormat, (int)level, (int)x, (int)y,
            (int)width, (int)height, (unsigned)format, (unsigned)type, bpp,
            (unsigned)pbo,
            g_state->pixelStore.unpackAlignment,
            g_state->pixelStore.unpackRowLength,
            g_state->pixelStore.unpackSkipPixels,
            g_state->pixelStore.unpackSkipRows,
            g_state->pixelStore.unpackImageHeight,
            g_state->pixelStore.unpackSkipImages,
            sample, (unsigned long long)hash, first);
    }
}
'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, anchor + helper, 1)

anchor = '''    backend_get_or_create_texture(t->id, width, height, 1, levels,\n                                  internalFormat, target, 1);\n    // Transition UNDEFINED -> SHADER_READ_ONLY_OPTIMAL so the texture is in a\n'''
insert = r'''    if (width >= 1024 && height >= 1024) {
        MITHRIL_LOG_WARN("vk",
            "ATLAS_STORAGE tex=%u %dx%d levels=%d internal=0x%x target=0x%x",
            (unsigned)t->id, (int)width, (int)height, (int)levels,
            (unsigned)internalFormat, (unsigned)target);
    }
'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, '''    backend_get_or_create_texture(t->id, width, height, 1, levels,\n                                  internalFormat, target, 1);\n''' + insert + '''    // Transition UNDEFINED -> SHADER_READ_ONLY_OPTIMAL so the texture is in a\n''', 1)

anchor = '''    const void* uploadPixels = resolve_unpack_pixels(pixels, width, height, 1,\n                                                     format, type);\n    if (!uploadPixels) return;\n    MGUnpackParams unpack{\n'''
replace = '''    const void* uploadPixels = resolve_unpack_pixels(pixels, width, height, 1,\n                                                     format, type);\n    if (!uploadPixels) return;\n    atlas_trace_upload("sub2d", t, level, xoffset, yoffset, width, height,\n                       format, type, uploadPixels);\n    MGUnpackParams unpack{\n'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, replace, 1)

# Trace large glTexImage2D uploads as well. The matching occurrence is the one
# before MGUnpackParams in glTexImage2D; glTexSubImage2D was replaced above.
anchor = '''    const void* uploadPixels = resolve_unpack_pixels(pixels, width, height, 1,\n                                                     format, type);\n    if (uploadPixels) {\n        MGUnpackParams unpack{\n'''
replace = '''    const void* uploadPixels = resolve_unpack_pixels(pixels, width, height, 1,\n                                                     format, type);\n    if (uploadPixels) {\n        atlas_trace_upload("image2d", t, level, 0, 0, width, height,\n                           format, type, uploadPixels);\n        MGUnpackParams unpack{\n'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, replace, 1)

anchor = '''    t->generateMipmaps = true;\n    backend_generate_mipmaps(t->id);\n'''
replace = '''    t->generateMipmaps = true;\n    if (t->width >= 1024 && t->height >= 1024) {\n        MITHRIL_LOG_WARN("vk", "ATLAS_MIPMAP tex=%u %dx%d levels=%d internal=0x%x",\n                         (unsigned)t->id, (int)t->width, (int)t->height,\n                         (int)t->levels, (unsigned)t->internalFormat);\n    }\n    backend_generate_mipmaps(t->id);\n'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, replace, 1)

p.write_text(s)
