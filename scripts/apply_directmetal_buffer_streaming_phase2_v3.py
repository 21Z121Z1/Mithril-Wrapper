#!/usr/bin/env python3
"""Patch BufferData structurally, then run the fail-closed phase-2 patcher."""
from pathlib import Path

internal = Path("src/gl/internal.h")
text = internal.read_text()
old = '''struct BufferData {
    std::vector<uint8_t> data;
    uint64_t lifetime_id = 0;
    uint64_t content_version = 0;
    bool defined = false;
    bool mapped = false;
    bool map_writable = false;
    size_t map_offset = 0;
};
'''
new = '''struct BufferData {
    std::vector<uint8_t> data;
    uint64_t lifetime_id = 0;
    uint64_t content_version = 0;
    uint64_t previous_content_version = 0;
    size_t update_offset = 0;
    size_t update_size = 0;
    bool update_is_partial = false;
    bool defined = false;
    bool mapped = false;
    bool map_writable = false;
    size_t map_offset = 0;

    void RecordUpdate(size_t offset, size_t size, bool partial) {
        previous_content_version = content_version;
        ++content_version;
        update_offset = offset;
        update_size = size;
        update_is_partial = partial;
    }
};
'''
if text.count(old) != 1:
    raise SystemExit(f"src/gl/internal.h: expected exactly one BufferData source shape, found {text.count(old)}")
internal.write_text(text.replace(old, new, 1))

base = Path("scripts/apply_directmetal_buffer_streaming_phase2.py")
script = base.read_text()
start = script.index('exact(\n    "src/gl/internal.h",')
end = script.index('\n\nin_function(', start)
script = script[:start] + '# BufferData was patched structurally by phase2_v3.\n' + script[end:]
exec(compile(script, str(base), "exec"), {"__name__": "__main__", "__file__": str(base)})
