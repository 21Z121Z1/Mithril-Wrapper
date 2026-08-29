#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp')
s = p.read_text()
old = '''    const bool want_y_flip =\n        (backend_active_kind() == MITHRIL_BACKEND_KIND_VULKAN)\n            ? ((!is_default_fbo) ^ clip_upper_left)\n            : (is_default_fbo ^ clip_upper_left);\n'''
new = '''    const bool want_y_flip =\n        (backend_active_kind() == MITHRIL_BACKEND_KIND_VULKAN)\n            ? (!clip_upper_left)\n            : (is_default_fbo ^ clip_upper_left);\n'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
