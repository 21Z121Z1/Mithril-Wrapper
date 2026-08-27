#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp')
s = p.read_text()
start_marker = '            if (plan.appBlock) {\n'
end_marker = '            } else {\n                /* ---- Layer-synthesised block: pack, hash, maybe upload ----\n'
start = s.index(start_marker)
end = s.index(end_marker, start)
replacement = r'''            if (plan.appBlock) {
                /* A/B: stage every application UBO through the aligned per-frame
                 * arena from the GL CPU shadow. This deliberately bypasses the
                 * direct VkBuffer descriptor path so one experiment covers four
                 * failure classes at once: persistent-map propagation, range
                 * offset/alignment, in-flight backing-buffer lifetime, and stale
                 * backend bytes. The source of truth is the GL Buffer::data
                 * shadow written by glBufferData/SubData/MapBufferRange. */
                const mithril::UniformBlockInfo& info = prog->blockInfos[plan.glBlockIndex];
                GLuint point = info.bindingPoint;
                auto pit = prog->uniformBlockBindings.find(plan.glBlockIndex);
                if (pit != prog->uniformBlockBindings.end()) point = pit->second;

                const uint32_t blockSize = info.dataSize > 0
                    ? (uint32_t)info.dataSize
                    : (db.bufferSize ? db.bufferSize : 16u);
                bool staged = false;
                if (point < (GLuint)mithril::kMaxIndexedBindings) {
                    const auto& sl = mithril::g_state->indexedBufferBindings
                                         [(int)mithril::IndexedBufferTarget::Uniform][point];
                    if (sl.name) {
                        mithril::Buffer* glbuf = mithril::state_get_buffer(sl.name);
                        if (glbuf) {
                            const size_t srcOff = sl.hasExplicitRange
                                ? (size_t)std::max<GLintptr>(0, sl.offset) : 0u;
                            const size_t declaredBytes = sl.hasExplicitRange && sl.size > 0
                                ? (size_t)sl.size : (size_t)blockSize;
                            std::vector<uint8_t> bytes(blockSize, 0);
                            size_t take = 0;
                            if (srcOff < glbuf->data.size()) {
                                take = std::min<size_t>(blockSize,
                                       std::min<size_t>(declaredBytes,
                                           glbuf->data.size() - srcOff));
                                if (take) std::memcpy(bytes.data(), glbuf->data.data() + srcOff, take);
                            }
                            UboSlice slc;
                            if (ubo_arena_upload(slot, bytes.data(), (VkDeviceSize)bytes.size(), slc)) {
                                ubuf = slc.buffer;
                                uoff = slc.offset;
                                urange = (VkDeviceSize)bytes.size();
                                staged = true;
                                static int stageLog = 0;
                                if (stageLog < 80) {
                                    uint32_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;
                                    if (bytes.size() >= 4)  std::memcpy(&w0, bytes.data(), 4);
                                    if (bytes.size() >= 8)  std::memcpy(&w1, bytes.data() + 4, 4);
                                    if (bytes.size() >= 12) std::memcpy(&w2, bytes.data() + 8, 4);
                                    if (bytes.size() >= 16) std::memcpy(&w3, bytes.data() + 12, 4);
                                    MITHRIL_LOG_WARN("vk", "APP_UBO_STAGE prog=%u db=%u block=%u point=%u "
                                                      "buf=%u explicit=%d srcOff=%zu declared=%zu blockSize=%u "
                                                      "shadow=%zu copied=%zu words=%08x,%08x,%08x,%08x arenaOff=%llu",
                                                     program, db.binding, plan.glBlockIndex, point,
                                                     sl.name, sl.hasExplicitRange ? 1 : 0, srcOff,
                                                     declaredBytes, blockSize, glbuf->data.size(), take,
                                                     w0, w1, w2, w3,
                                                     (unsigned long long)slc.offset);
                                    ++stageLog;
                                }
                            } else {
                                MITHRIL_LOG_WARN("vk", "APP_UBO_STAGE arena upload failed prog=%u binding=%u",
                                                 program, db.binding);
                                return;
                            }
                        }
                    }
                }
                if (!staged) {
                    std::vector<uint8_t> zeros(blockSize, 0);
                    UboSlice slc;
                    if (!ubo_arena_upload(slot, zeros.data(), (VkDeviceSize)zeros.size(), slc)) return;
                    ubuf = slc.buffer;
                    uoff = slc.offset;
                    urange = (VkDeviceSize)zeros.size();
                    static int missingLog = 0;
                    if (missingLog++ < 10) {
                        MITHRIL_LOG_WARN("vk", "APP_UBO_STAGE missing source prog=%u binding=%u point=%u — zeros",
                                         program, db.binding, point);
                    }
                }
'''
s = s[:start] + replacement + s[end:]
p.write_text(s)
