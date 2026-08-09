// Mithril-Wrapper - MG_Backend/DirectVulkan/Std140.h
// std140 UBO member placement + a tight-to-std140 packer used by the
// layer-synthesised uniform block path in DescriptorSet.cpp.
//
// The slot fields are populated from the program's UBO member reflection
// (offset / size / columns / rows / arraySize / arrayStride / matrixStride);
// pack_std140() writes the member's float payload into the std140-padded
// location inside the block. Declared in the global namespace so it resolves
// from either global or nested (mithril::vk) call sites.
#ifndef MITHRIL_DIRECTVULKAN_STD140_H
#define MITHRIL_DIRECTVULKAN_STD140_H

#include <cstddef>
#include <cstdint>

struct Std140Slot {
    uint32_t offset       = 0;  // byte offset of the member inside the UBO block
    uint32_t size         = 0;  // total byte size of the member (incl. std140 padding)
    uint32_t columns      = 0;  // matrix columns (1 for non-matrices)
    uint32_t rows         = 0;  // matrix rows    (1 for non-matrices)
    uint32_t arraySize    = 0;  // array length   (1 for non-arrays)
    uint32_t arrayStride  = 0;  // bytes between array elements (std140-padded)
    uint32_t matrixStride = 0;  // bytes between matrix columns (std140-padded)
};

// Pack `src_count` floats (the uniform's float payload) into `dst` at the
// std140-padded location described by `slot`. `dst_size` bounds the write so a
// malformed slot cannot scribble past the end of the block.
void pack_std140(void* dst, size_t dst_size, const Std140Slot& slot,
                 const float* src, size_t src_count);

#endif  // MITHRIL_DIRECTVULKAN_STD140_H
