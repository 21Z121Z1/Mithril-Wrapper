#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(path, old, new, label):
    p = root / path
    s = p.read_text()
    if new in s:
        return
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 match, got {n}")
    p.write_text(s.replace(old, new, 1))

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/Program.cpp",
    '''void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {\n    MITHRIL_ENSURE_INIT();\n    mithril::Program* p = mithril::state_get_program(program);\n    if (!p || !params) return;\n    auto it = p->uniformByLocation.find(location);\n    if (it == p->uniformByLocation.end()) { *params = 0; return; }\n    auto& u = p->uniforms[it->second];\n    if (!u.value.empty()) *params = u.value[0];\n    else *params = 0;\n}\n\nvoid glGetUniformiv(GLuint program, GLint location, GLint* params) {\n    MITHRIL_ENSURE_INIT();\n    mithril::Program* p = mithril::state_get_program(program);\n    if (!p || !params) return;\n    auto it = p->uniformByLocation.find(location);\n    if (it == p->uniformByLocation.end()) { *params = 0; return; }\n    auto& u = p->uniforms[it->second];\n    *params = u.value.empty() ? 0 : (GLint)u.value[0];\n}\n''',
    '''void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {\n    MITHRIL_ENSURE_INIT();\n    mithril::Program* p = mithril::state_get_program(program);\n    if (!p || !params) return;\n    auto it = p->uniformByLocation.find(location);\n    if (it == p->uniformByLocation.end()) { *params = 0; return; }\n    const auto& u = p->uniforms[it->second];\n    if (u.value.empty()) { *params = 0; return; }\n    // Core GL requires glGetUniform* to return every component of the\n    // uniform value (and every element for arrays/matrices), not only x.\n    std::copy(u.value.begin(), u.value.end(), params);\n}\n\nvoid glGetUniformiv(GLuint program, GLint location, GLint* params) {\n    MITHRIL_ENSURE_INIT();\n    mithril::Program* p = mithril::state_get_program(program);\n    if (!p || !params) return;\n    auto it = p->uniformByLocation.find(location);\n    if (it == p->uniformByLocation.end()) { *params = 0; return; }\n    const auto& u = p->uniforms[it->second];\n    if (u.value.empty()) { *params = 0; return; }\n    for (size_t i = 0; i < u.value.size(); ++i) params[i] = (GLint)u.value[i];\n}\n''',
    "glGetUniform float/int vector semantics",
)

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp",
    '''void glGetUniformuiv(GLuint program, GLint location, GLuint* params) {\n    MITHRIL_ENSURE_INIT();\n    GLint v = 0;\n    glGetUniformiv(program, location, &v);\n    if (params) *params = (GLuint)v;\n}\n''',
    '''void glGetUniformuiv(GLuint program, GLint location, GLuint* params) {\n    MITHRIL_ENSURE_INIT();\n    if (!params) return;\n    mithril::Program* p = mithril::state_get_program(program);\n    if (!p) return;\n    auto it = p->uniformByLocation.find(location);\n    if (it == p->uniformByLocation.end()) { *params = 0; return; }\n    const auto& u = p->uniforms[it->second];\n    if (u.value.empty()) { *params = 0; return; }\n    for (size_t i = 0; i < u.value.size(); ++i) params[i] = (GLuint)u.value[i];\n}\n''',
    "glGetUniform unsigned vector semantics",
)

print("uniform query semantic transformation complete")
