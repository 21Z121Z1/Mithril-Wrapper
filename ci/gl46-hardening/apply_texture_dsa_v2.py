#!/usr/bin/env python3
"""Canonicalize GL 4.5 texture-parameter DSA semantics by function boundaries.

This deliberately does not depend on numbered comments: earlier migrations may
rewrite those comments.  The replacement is idempotent and remains entirely on
cold API paths.
"""
from __future__ import annotations
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GL46 = ROOT / 'Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp'
AUDIT = ROOT / 'verify/gl46_semantic_audit.py'

PARAM_REPLACEMENT = r'''/* 45-48. Texture DSA parameter setters.
 * Use the target established on the texture object; DSA must not reinterpret
 * every object as GL_TEXTURE_2D. The legacy setter owns backend invalidation. */
static GLenum gl46_dsa_texture_target(GLuint texture) {
    mithril::Texture* t = mithril::state_get_texture(texture);
    if (!t) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return 0;
    }
    return t->target;
}

static GLuint gl46_dsa_bind_texture(GLuint texture, GLenum* targetOut) {
    GLenum target = gl46_dsa_texture_target(texture);
    if (!target) { if (targetOut) *targetOut = 0; return 0; }
    mithril::TextureTarget slot = mithril::textureTargetFromGL(target);
    if (slot == mithril::TextureTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        if (targetOut) *targetOut = 0;
        return 0;
    }
    GLuint unit = g_state->activeTextureUnit;
    GLuint previous = unit < mithril::kMaxTextureUnits
        ? g_state->textureBindings[unit][(int)slot].name : 0;
    glBindTexture(target, texture);
    if (targetOut) *targetOut = target;
    return previous;
}

static void gl46_dsa_restore_texture(GLenum target, GLuint previous) {
    if (target) glBindTexture(target, previous);
}

void glTextureParameterf(GLuint texture, GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glTexParameterf(target, pname, param);
    gl46_dsa_restore_texture(target, previous);
}

void glTextureParameteri(GLuint texture, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glTexParameteri(target, pname, param);
    gl46_dsa_restore_texture(target, previous);
}

void glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glTexParameterfv(target, pname, params);
    gl46_dsa_restore_texture(target, previous);
}

void glTextureParameteriv(GLuint texture, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glTexParameteriv(target, pname, params);
    gl46_dsa_restore_texture(target, previous);
}

'''

INT_REPLACEMENT = r'''void glTextureParameterIiv(GLuint texture, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glTexParameterIiv(target, pname, params);
    gl46_dsa_restore_texture(target, previous);
}

void glTextureParameterIuiv(GLuint texture, GLenum pname, const GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glTexParameterIuiv(target, pname, params);
    gl46_dsa_restore_texture(target, previous);
}

void glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glGetTexParameterIiv(target, pname, params);
    gl46_dsa_restore_texture(target, previous);
}

void glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    GLenum target = 0; GLuint previous = gl46_dsa_bind_texture(texture, &target);
    if (!target) return;
    glGetTexParameterIuiv(target, pname, params);
    gl46_dsa_restore_texture(target, previous);
}
'''

AUDIT_APPEND = r'''
require("gl46_dsa_texture_target" in gl and "textureTargetFromGL" in gl,
        "texture DSA parameter calls must use the object's tracked target")
require("sampler_default_params(pname" not in gl,
        "texture DSA integer getters must not return sampler facade defaults")
'''

def function_end(text: str, start: int) -> int:
    brace = text.find('{', start)
    if brace < 0: raise SystemExit('function opening brace not found')
    depth = 0
    i = brace
    while i < len(text):
        ch = text[i]
        if ch == '{': depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0: return i + 1
        i += 1
    raise SystemExit('unterminated function')

def normalize(text: str) -> str:
    first = text.find('void glTextureParameterf(')
    if first < 0: raise SystemExit('glTextureParameterf not found')
    comment = text.rfind('/*', max(0, first - 700), first)
    start = comment if comment >= 0 and '45' in text[comment:first] else first
    next_fn = text.find('void glGenerateTextureMipmap(', first)
    if next_fn < 0: raise SystemExit('glGenerateTextureMipmap boundary not found')
    text = text[:start] + PARAM_REPLACEMENT + text[next_fn:]

    starts = []
    cursor = 0
    for name in ('glTextureParameterIiv', 'glTextureParameterIuiv',
                 'glGetTextureParameterIiv', 'glGetTextureParameterIuiv'):
        pos = text.find('void ' + name + '(', cursor)
        if pos < 0: raise SystemExit(name + ' not found')
        starts.append(pos)
        cursor = pos + 1
    int_start = starts[0]
    int_end = function_end(text, starts[-1])
    text = text[:int_start] + INT_REPLACEMENT + text[int_end:]
    return text

def apply() -> None:
    GL46.write_text(normalize(GL46.read_text()))
    audit = AUDIT.read_text()
    if 'texture DSA integer getters must not return sampler facade defaults' not in audit:
        AUDIT.write_text(audit + AUDIT_APPEND)
    verify()

def verify() -> None:
    text = GL46.read_text()
    for needle in ('gl46_dsa_texture_target', 'textureTargetFromGL',
                   'glTexParameterIiv(target', 'glGetTexParameterIuiv(target'):
        if needle not in text: raise SystemExit('missing invariant: ' + needle)
    if 'sampler_default_params(pname' in text:
        raise SystemExit('legacy texture DSA facade remains')
    if normalize(text) != text:
        raise SystemExit('texture DSA normalization is not idempotent')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL texture DSA semantics: PASS; function-boundary normalization idempotent')

def main() -> None:
    p=argparse.ArgumentParser(); g=p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply',action='store_true'); g.add_argument('--verify',action='store_true')
    a=p.parse_args(); apply() if a.apply else verify()

if __name__ == '__main__': main()
