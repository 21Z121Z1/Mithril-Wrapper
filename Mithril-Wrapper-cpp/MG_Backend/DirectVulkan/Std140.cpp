// Mithril-Wrapper - MG_Backend/DirectVulkan/Std140.cpp
// See Std140.h for the interface contract.
#include "Std140.h"

#include <cstring>

void pack_std140(void* dst, size_t dst_size, const Std140Slot& slot,
                 const float* src, size_t src_count) {
    if (!dst || !src || src_count == 0) return;
    if (slot.offset >= dst_size) return;
    uint8_t* base = static_cast<uint8_t*>(dst) + slot.offset;
    const size_t avail = dst_size - slot.offset;

    const uint32_t rows = slot.rows ? slot.rows : 1;
    const uint32_t cols = slot.columns ? slot.columns : 1;

    // Column-major matrix: each column is a `rows`-component vector padded to
    // `matrixStride` bytes (std140 requires 16-byte column alignment).
    if (cols > 1 && rows > 1) {
        const uint32_t mstride = slot.matrixStride ? slot.matrixStride
                                                   : (rows * static_cast<uint32_t>(sizeof(float)));
        for (uint32_t c = 0; c < cols; ++c) {
            for (uint32_t r = 0; r < rows; ++r) {
                const size_t idx = static_cast<size_t>(c) * rows + r;
                if (idx >= src_count) return;
                const size_t out = static_cast<size_t>(c) * mstride +
                                  static_cast<size_t>(r) * sizeof(float);
                if (out + sizeof(float) > avail) return;
                std::memcpy(base + out, &src[idx], sizeof(float));
            }
        }
        return;
    }

    // Array: each element padded to `arrayStride` bytes.
    if (slot.arraySize > 1) {
        const uint32_t astride = slot.arrayStride ? slot.arrayStride
                                                  : (slot.size ? slot.size : 0);
        if (astride == 0) return;
        const uint32_t elemFloats = astride / static_cast<uint32_t>(sizeof(float));
        if (elemFloats == 0) return;
        for (uint32_t e = 0; e < slot.arraySize; ++e) {
            const size_t srcIdx = static_cast<size_t>(e) * elemFloats;
            if (srcIdx >= src_count) break;
            const size_t out = static_cast<size_t>(e) * astride;
            const size_t n = elemFloats * sizeof(float);
            if (out + n > avail) break;
            std::memcpy(base + out, &src[srcIdx], n);
        }
        return;
    }

    // Scalar / vector: tight copy of the float payload.
    size_t n = src_count * sizeof(float);
    if (n > avail) n = avail;
    std::memcpy(base, src, n);
}
