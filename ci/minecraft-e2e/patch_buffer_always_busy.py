#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Resources.cpp')
s = p.read_text()
old = '''static inline bool buffer_maybe_inflight(const BufferEntry& e) {
    return e.lastWriteSerial > dvk_last_completed_serial();
}
'''
new = '''static inline bool buffer_maybe_inflight(const BufferEntry& e) {
    (void)e;
    // A/B probe: conservatively treat every buffer as busy. Full respecs
    // orphan and partial uploads stage, so no CPU write can race GPU reads.
    return true;
}
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
