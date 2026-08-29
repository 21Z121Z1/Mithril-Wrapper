#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Texture.cpp')
s = p.read_text()
old = '''    void* base = backend_get_buffer_mapped_pointer(pboName);\n    if (!base) {\n        if (pbo->data.size() < bufferSize) {\n            mithril::state_set_error(GL_INVALID_OPERATION);\n            return nullptr;\n        }\n        base = pbo->data.data();\n    }\n    return static_cast<const uint8_t*>(base) + offset;\n'''
new = '''    // glMapBuffer/glMapBufferRange currently expose Buffer::data to the GL\n    // application. For persistent/coherent mappings, application writes can\n    // therefore live only in this CPU shadow until an explicit flush/unmap.\n    // A pixel-unpack operation must read the bytes the GL client actually\n    // wrote, not a separate persistently mapped VkBuffer allocation that may\n    // still contain old data. Keep this source consistent with the mapping\n    // API until those APIs are redesigned to expose one authoritative alias.\n    if (pbo->data.size() < bufferSize) {\n        mithril::state_set_error(GL_INVALID_OPERATION);\n        return nullptr;\n    }\n    const void* base = pbo->data.data();\n    return static_cast<const uint8_t*>(base) + offset;\n'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
