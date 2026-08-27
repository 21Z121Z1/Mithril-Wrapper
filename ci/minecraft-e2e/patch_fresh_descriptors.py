#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp')
s = p.read_text()
old = '''    VkDescriptorSet set = VK_NULL_HANDLE;
    for (int i = 0; i < kDescriptorMemoSize; ++i) {
        const DescriptorMemoEntry& e = pr.descMemo[slot][i];
        if (e.valid && e.set != VK_NULL_HANDLE && e.signature == sig) {
            set = e.set;
            break;
        }
    }
'''
new = '''    VkDescriptorSet set = VK_NULL_HANDLE;
    // A/B probe: bypass the descriptor-content memo. Every draw takes the
    // next set in the slot and rewrites all descriptors. This isolates stale
    // memo/set reuse from shader, VAO, UBO packing and resource contents.
    if (false) {
        for (int i = 0; i < kDescriptorMemoSize; ++i) {
            const DescriptorMemoEntry& e = pr.descMemo[slot][i];
            if (e.valid && e.set != VK_NULL_HANDLE && e.signature == sig) {
                set = e.set;
                break;
            }
        }
    }
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
