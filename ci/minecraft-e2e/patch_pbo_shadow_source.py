#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Texture.cpp')
s = p.read_text()
old = '''    void* base = backend_get_buffer_mapped_pointer(pboName);\n    if (!base) {\n        if (pbo->data.size() < bufferSize) {\n            mithril::state_set_error(GL_INVALID_OPERATION);\n            return nullptr;\n        }\n        base = pbo->data.data();\n    }\n    return static_cast<const uint8_t*>(base) + offset;\n'''
new = '''    // The GL map entry points currently return Buffer::data for every mapped\n    // buffer, including GL_MAP_PERSISTENT_BIT storage.  That makes this CPU\n    // shadow the bytes the application actually writes.  DirectVulkan also\n    // keeps a persistently-mapped VkBuffer allocation, but that is a different\n    // address; preferring backend_get_buffer_mapped_pointer() here reads stale\n    // zero/old bytes for pixel-unpack buffers.  DirectMetal already falls back\n    // to Buffer::data because its persistentHost alias is null.  Until the map\n    // entry points themselves return a backend alias, the GL shadow is the\n    // authoritative PBO upload source.\n    if (pbo->data.size() < bufferSize) {\n        mithril::state_set_error(GL_INVALID_OPERATION);\n        return nullptr;\n    }\n    const void* base = pbo->data.data();\n    return static_cast<const uint8_t*>(base) + offset;\n'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
