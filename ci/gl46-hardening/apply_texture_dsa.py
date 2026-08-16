#!/usr/bin/env python3
"""Make GL 4.5 texture parameter DSA calls target-correct.

The previous compatibility path hard-coded GL_TEXTURE_2D and the integer DSA
variants were placeholders.  Reuse the existing non-DSA texture implementation
with the object's tracked immutable target and save/bind/call/restore the exact
binding slot.  This keeps backend sampler-cache invalidation in one place and
stays off the draw hot path.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GL46 = ROOT / 'Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp'
AUDIT = ROOT / 'verify/gl46_semantic_audit.py'

PARAM_PATTERN = re.compile(
    r'/\* 45\. glTextureParameterf.*?(?=/\* 49\. glGenerateTextureMipmap)',
    re.S,
)
PARAM_REPLACEMENT = r'''/* 45-48. Texture DSA parameter setters.
 * Use the target established on the texture object; DSA must not reinterpret
 * every object as GL_TEXTURE_2D.  The legacy setter owns backend invalidation. */
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

INT_PATTERN = re.compile(
    r'void glTextureParameterIiv\(GLuint texture, GLenum pname, const GLint\* params\).*?'
    r'void glGetTextureParameterIuiv\(GLuint texture, GLenum pname, GLuint\* params\) \{.*?\n\}',
    re.S,
)
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
}'''

AUDIT_APPEND = r'''
require("gl46_dsa_texture_target" in gl and "textureTargetFromGL" in gl,
        "texture DSA parameter calls must use the object's tracked target")
require("sampler_default_params(pname" not in gl,
        "texture DSA integer getters must not return sampler facade defaults")
'''


def normalize(text: str) -> str:
    text, n = PARAM_PATTERN.subn(lambda _: PARAM_REPLACEMENT, text, count=1)
    if n != 1:
        raise SystemExit(f'texture DSA scalar/vector region: expected one match, found {n}')
    text, n = INT_PATTERN.subn(lambda _: INT_REPLACEMENT, text, count=1)
    if n != 1:
        raise SystemExit(f'texture DSA integer region: expected one match, found {n}')
    return text


def apply() -> None:
    GL46.write_text(normalize(GL46.read_text()))
    audit = AUDIT.read_text()
    if "texture DSA integer getters must not return sampler facade defaults" not in audit:
        AUDIT.write_text(audit + AUDIT_APPEND)
    verify()


def verify() -> None:
    text = GL46.read_text()
    for needle in ('gl46_dsa_texture_target', 'textureTargetFromGL',
                   'glTexParameterIiv(target', 'glGetTexParameterIuiv(target'):
        if needle not in text:
            raise SystemExit(f'missing texture DSA invariant: {needle}')
    if 'sampler_default_params(pname' in text:
        raise SystemExit('legacy sampler-default texture DSA getter remains')
    if normalize(text) != text:
        raise SystemExit('texture DSA parameter regions are not canonical/idempotent')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL texture DSA parameter semantics: PASS; target-aware and idempotent')


def main() -> None:
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply', action='store_true')
    g.add_argument('--verify', action='store_true')
    a = p.parse_args()
    apply() if a.apply else verify()

if __name__ == '__main__':
    main()
