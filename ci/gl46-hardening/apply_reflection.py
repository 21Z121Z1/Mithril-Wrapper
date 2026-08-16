#!/usr/bin/env python3
"""Wire GL 4.x uniform reflection APIs to Mithril's real Program metadata.

Program.cpp already reflects uniform type/size/location/block/offset/stride
information from SPIR-V.  The compatibility layer historically discarded that
information and returned placeholders.  This patch exposes the existing data
without touching draw-time hot paths.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GL46 = ROOT / "Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp"
AUDIT = ROOT / "verify/gl46_semantic_audit.py"

REPLACEMENT = r'''/* 2-5. Uniform and uniform-block reflection.
 * Program.cpp already owns authoritative SPIR-V reflection metadata.  Build a
 * deterministic, cold-path view here instead of returning facade defaults. */
static std::vector<const mithril::Uniform*> gl46_active_uniforms(GLuint program) {
    std::vector<const mithril::Uniform*> out;
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked) return out;
    out.reserve(p->uniforms.size());
    for (const auto& kv : p->uniforms) out.push_back(&kv.second);
    std::sort(out.begin(), out.end(), [](const mithril::Uniform* a,
                                         const mithril::Uniform* b) {
        return a->name < b->name;
    });
    return out;
}

static bool gl46_uniform_name_matches(const mithril::Uniform& u, const char* name) {
    if (!name) return false;
    if (u.name == name) return true;
    std::string n(name);
    if (n.size() > 3 && n.compare(n.size() - 3, 3, "[0]") == 0 &&
        u.name == n.substr(0, n.size() - 3)) return true;
    if (u.size > 1 && u.name.size() > 3 &&
        u.name.compare(u.name.size() - 3, 3, "[0]") == 0 &&
        u.name.substr(0, u.name.size() - 3) == n) return true;
    return false;
}

void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount,
                           const GLuint* uniformIndices, GLenum pname,
                           GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (uniformCount < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    if (uniformCount == 0) return;
    if (!uniformIndices || !params) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    const auto uniforms = gl46_active_uniforms(program);
    for (GLsizei i = 0; i < uniformCount; ++i) {
        if (uniformIndices[i] >= uniforms.size()) {
            params[i] = 0;
            mithril::state_set_error(GL_INVALID_VALUE);
            continue;
        }
        const mithril::Uniform& u = *uniforms[uniformIndices[i]];
        switch (pname) {
            case GL_UNIFORM_TYPE:          params[i] = (GLint)u.type; break;
            case GL_UNIFORM_SIZE:          params[i] = u.size; break;
            case GL_UNIFORM_NAME_LENGTH:   params[i] = (GLint)u.name.size() + 1; break;
            case GL_UNIFORM_BLOCK_INDEX:   params[i] = u.blockIndex; break;
            case GL_UNIFORM_OFFSET:        params[i] = u.offset; break;
            case GL_UNIFORM_ARRAY_STRIDE:  params[i] = u.arrayStride; break;
            case GL_UNIFORM_MATRIX_STRIDE: params[i] = u.matrixStride; break;
            case GL_UNIFORM_IS_ROW_MAJOR:  params[i] = u.rowMajor ? GL_TRUE : GL_FALSE; break;
            default:
                params[i] = 0;
                mithril::state_set_error(GL_INVALID_ENUM);
                break;
        }
    }
}

void glGetUniformIndices(GLuint program, GLsizei uniformCount,
                         const GLchar* const* uniformNames,
                         GLuint* uniformIndices) {
    MITHRIL_ENSURE_INIT();
    if (uniformCount < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    if (uniformCount == 0) return;
    if (!uniformNames || !uniformIndices) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    const auto uniforms = gl46_active_uniforms(program);
    for (GLsizei i = 0; i < uniformCount; ++i) {
        uniformIndices[i] = GL_INVALID_INDEX;
        for (GLuint j = 0; j < (GLuint)uniforms.size(); ++j) {
            if (gl46_uniform_name_matches(*uniforms[j], uniformNames[i])) {
                uniformIndices[i] = j;
                break;
            }
        }
    }
}

void glGetActiveUniformName(GLuint program, GLuint uniformIndex,
                            GLsizei bufSize, GLsizei* length,
                            GLchar* uniformName) {
    MITHRIL_ENSURE_INIT();
    if (bufSize < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    const auto uniforms = gl46_active_uniforms(program);
    if (uniformIndex >= uniforms.size()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        if (length) *length = 0;
        if (uniformName && bufSize > 0) uniformName[0] = '\0';
        return;
    }
    const std::string& name = uniforms[uniformIndex]->name;
    GLsizei copied = 0;
    if (uniformName && bufSize > 0) {
        copied = (GLsizei)std::min<size_t>(name.size(), (size_t)bufSize - 1);
        memcpy(uniformName, name.data(), (size_t)copied);
        uniformName[copied] = '\0';
    }
    if (length) *length = copied;
}

void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex,
                                 GLsizei bufSize, GLsizei* length,
                                 GLchar* uniformName) {
    MITHRIL_ENSURE_INIT();
    if (bufSize < 0) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    mithril::Program* p = mithril::state_get_program(program);
    if (!p || !p->linked) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    const std::string* found = nullptr;
    for (const auto& kv : p->uniformBlocks) {
        if (kv.second == uniformBlockIndex) { found = &kv.first; break; }
    }
    if (!found) {
        mithril::state_set_error(GL_INVALID_VALUE);
        if (length) *length = 0;
        if (uniformName && bufSize > 0) uniformName[0] = '\0';
        return;
    }
    GLsizei copied = 0;
    if (uniformName && bufSize > 0) {
        copied = (GLsizei)std::min<size_t>(found->size(), (size_t)bufSize - 1);
        memcpy(uniformName, found->data(), (size_t)copied);
        uniformName[copied] = '\0';
    }
    if (length) *length = copied;
}

'''

PATTERN = re.compile(
    r'/\* 2\. glGetActiveUniformsiv.*?(?=/\* 6\. glClearTexImage)',
    re.S,
)

AUDIT_APPEND = r'''
require("gl46_active_uniforms" in gl and "u.blockIndex" in gl and "u.matrixStride" in gl,
        "uniform reflection APIs must expose Program/SPIR-V metadata")
require("Set all indices to GL_INVALID_INDEX" not in gl,
        "glGetUniformIndices must not be a facade default")
'''


def apply() -> None:
    text = GL46.read_text()
    if '#include <algorithm>' not in text:
        text = text.replace('#include <cstring>\n', '#include <cstring>\n#include <algorithm>\n', 1)
    if 'static std::vector<const mithril::Uniform*> gl46_active_uniforms' not in text:
        text, n = PATTERN.subn(REPLACEMENT, text, count=1)
        if n != 1:
            raise SystemExit(f'uniform reflection region: expected one match, found {n}')
    GL46.write_text(text)
    audit = AUDIT.read_text()
    if 'uniform reflection APIs must expose Program/SPIR-V metadata' not in audit:
        audit += AUDIT_APPEND
        AUDIT.write_text(audit)
    verify()


def verify() -> None:
    text = GL46.read_text()
    required = (
        '#include <algorithm>',
        'gl46_active_uniforms',
        'gl46_uniform_name_matches',
        'case GL_UNIFORM_TYPE:',
        'case GL_UNIFORM_BLOCK_INDEX:',
        'case GL_UNIFORM_OFFSET:',
        'case GL_UNIFORM_MATRIX_STRIDE:',
        'p->uniformBlocks',
    )
    for needle in required:
        if needle not in text:
            raise SystemExit(f'missing reflection invariant: {needle}')
    if 'Set all indices to GL_INVALID_INDEX' in text:
        raise SystemExit('legacy uniform reflection facade remains')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL program reflection semantics: PASS')


def main() -> None:
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply', action='store_true')
    g.add_argument('--verify', action='store_true')
    a = p.parse_args()
    apply() if a.apply else verify()


if __name__ == '__main__':
    main()
