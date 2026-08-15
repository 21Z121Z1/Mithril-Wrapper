#!/usr/bin/env python3
from pathlib import Path

patcher = Path(__file__).with_name("apply_vertex_binding_fix.py")
source = patcher.read_text()
old = '''# Binding API validation + true relative-offset state.\nreplace_once(\n    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",\n    """    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings) {\n        mithril::state_set_error(GL_INVALID_VALUE);\n        return;\n    }""",\n    """    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings ||\n        offset < 0 || stride < 0) {\n        mithril::state_set_error(GL_INVALID_VALUE);\n        return;\n    }""")\n'''
new = '''# Binding API validation + true relative-offset state.\nregex_once(\n    "Mithril-Wrapper-cpp/MG_Impl/VertexArray.cpp",\n    r"(void glBindVertexBuffer\\(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride\\) \\{\\n    MITHRIL_ENSURE_INIT\\(\\);\\n)    if \\(bindingindex >= \\(GLuint\\)mithril::kMaxVertexBindings\\) \\{\\n        mithril::state_set_error\\(GL_INVALID_VALUE\\);\\n        return;\\n    \\}",\n    r"\\1    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings ||\\n        offset < 0 || stride < 0) {\\n        mithril::state_set_error(GL_INVALID_VALUE);\\n        return;\\n    }")\n'''
if source.count(old) != 1:
    raise SystemExit("unexpected glBindVertexBuffer materializer block")
source = source.replace(old, new, 1)
# The base patcher uses callback replacements, so the raw \\1 above would be
# emitted literally. Convert this one generated regex_once call to an explicit
# lambda that preserves the captured function prefix.
needle = '''    r"\\1    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings ||\\n        offset < 0 || stride < 0) {\\n        mithril::state_set_error(GL_INVALID_VALUE);\\n        return;\\n    }")'''
replacement = '''    lambda m: m.group(1) + "    if (bindingindex >= (GLuint)mithril::kMaxVertexBindings ||\\n        offset < 0 || stride < 0) {\\n        mithril::state_set_error(GL_INVALID_VALUE);\\n        return;\\n    }")'''
# Adapt regex_once helper temporarily so it accepts a callable replacement.
source = source.replace('out, n = re.subn(pattern, lambda _m: repl, s, count=1, flags=re.S)',
                        'out, n = re.subn(pattern, repl if callable(repl) else (lambda _m: repl), s, count=1, flags=re.S)', 1)
source = source.replace(needle, replacement, 1)
exec(compile(source, str(patcher), "exec"), {"__name__": "__main__", "__file__": str(patcher)})
