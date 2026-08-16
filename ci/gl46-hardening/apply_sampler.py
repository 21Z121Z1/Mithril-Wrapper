#!/usr/bin/env python3
"""Normalize GL 3.3 sampler object semantics onto tracked Sampler state."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GL46 = ROOT / 'Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp'
AUDIT = ROOT / 'verify/gl46_semantic_audit.py'

PATTERN = re.compile(
    r'/\* ---- Sampler object getters(?:/setters)? \(GL 3\.3\).*?(?=void glBindFragDataLocationIndexed)',
    re.S,
)

REPLACEMENT = r'''/* ---- Sampler object getters/setters (GL 3.3).
 * Sampler state is tracked in GLState and participates in backend descriptor
 * state. Keep these cold-path API calls self-contained rather than calling
 * another translation unit through an undeclared GL entry point. */
static mithril::Sampler* gl46_sampler(GLuint sampler) {
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s) mithril::state_set_error(GL_INVALID_OPERATION);
    return s;
}

static bool gl46_get_sampler_scalar(const mithril::Sampler& s, GLenum pname,
                                    GLfloat* f, GLint* i) {
    auto setf = [&](GLfloat v) { if (f) *f = v; if (i) *i = (GLint)v; };
    auto seti = [&](GLint v)   { if (i) *i = v; if (f) *f = (GLfloat)v; };
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:   seti(s.minFilter); return true;
        case GL_TEXTURE_MAG_FILTER:   seti(s.magFilter); return true;
        case GL_TEXTURE_WRAP_S:       seti(s.wrapS); return true;
        case GL_TEXTURE_WRAP_T:       seti(s.wrapT); return true;
        case GL_TEXTURE_WRAP_R:       seti(s.wrapR); return true;
        case GL_TEXTURE_MIN_LOD:      setf(s.minLod); return true;
        case GL_TEXTURE_MAX_LOD:      setf(s.maxLod); return true;
        case GL_TEXTURE_LOD_BIAS:     setf(s.lodBias); return true;
        case GL_TEXTURE_COMPARE_MODE: seti((GLint)s.compareMode); return true;
        case GL_TEXTURE_COMPARE_FUNC: seti((GLint)s.compareFunc); return true;
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
        case GL_TEXTURE_MAX_ANISOTROPY_EXT: setf(s.maxAnisotropy); return true;
#endif
        default: return false;
    }
}

static bool gl46_set_sampler_scalar(mithril::Sampler& s, GLenum pname,
                                    GLfloat value) {
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:   s.minFilter = (GLint)value; break;
        case GL_TEXTURE_MAG_FILTER:   s.magFilter = (GLint)value; break;
        case GL_TEXTURE_WRAP_S:       s.wrapS = (GLint)value; break;
        case GL_TEXTURE_WRAP_T:       s.wrapT = (GLint)value; break;
        case GL_TEXTURE_WRAP_R:       s.wrapR = (GLint)value; break;
        case GL_TEXTURE_MIN_LOD:      s.minLod = value; break;
        case GL_TEXTURE_MAX_LOD:      s.maxLod = value; break;
        case GL_TEXTURE_LOD_BIAS:     s.lodBias = value; break;
        case GL_TEXTURE_COMPARE_MODE: s.compareMode = (GLenum)(GLint)value; break;
        case GL_TEXTURE_COMPARE_FUNC: s.compareFunc = (GLenum)(GLint)value; break;
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
        case GL_TEXTURE_MAX_ANISOTROPY_EXT: s.maxAnisotropy = value; break;
#endif
        default: return false;
    }
    ++s.version;
    return true;
}

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Sampler* s = gl46_sampler(sampler);
    if (!s) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        for (int k = 0; k < 4; ++k) params[k] = (GLint)s->borderColor[k];
        return;
    }
    if (!gl46_get_sampler_scalar(*s, pname, nullptr, params))
        mithril::state_set_error(GL_INVALID_ENUM);
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Sampler* s = gl46_sampler(sampler);
    if (!s) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        memcpy(params, s->borderColor, sizeof(s->borderColor));
        return;
    }
    if (!gl46_get_sampler_scalar(*s, pname, params, nullptr))
        mithril::state_set_error(GL_INVALID_ENUM);
}

void glGetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Sampler* s = gl46_sampler(sampler);
    if (!s) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        memcpy(params, s->borderColorI, sizeof(s->borderColorI));
        return;
    }
    if (!gl46_get_sampler_scalar(*s, pname, nullptr, params))
        mithril::state_set_error(GL_INVALID_ENUM);
}

void glGetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Sampler* s = gl46_sampler(sampler);
    if (!s) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        for (int k = 0; k < 4; ++k) params[k] = (GLuint)s->borderColorUI[k];
        return;
    }
    GLint v = 0;
    if (!gl46_get_sampler_scalar(*s, pname, nullptr, &v)) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    *params = (GLuint)v;
}

void glSamplerParameterIiv(GLuint sampler, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Sampler* s = gl46_sampler(sampler);
    if (!s) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        memcpy(s->borderColorI, params, sizeof(s->borderColorI));
        for (int k = 0; k < 4; ++k) s->borderColor[k] = (GLfloat)params[k];
        ++s->version;
        return;
    }
    if (!gl46_set_sampler_scalar(*s, pname, (GLfloat)params[0]))
        mithril::state_set_error(GL_INVALID_ENUM);
}

void glSamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Sampler* s = gl46_sampler(sampler);
    if (!s) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        for (int k = 0; k < 4; ++k) {
            s->borderColorUI[k] = (GLint)params[k];
            s->borderColor[k] = (GLfloat)params[k];
        }
        ++s->version;
        return;
    }
    if (!gl46_set_sampler_scalar(*s, pname, (GLfloat)params[0]))
        mithril::state_set_error(GL_INVALID_ENUM);
}

'''

AUDIT_APPEND = r'''
require("gl46_get_sampler_scalar" in gl and "gl46_set_sampler_scalar" in gl,
        "sampler getters/setters must expose tracked sampler state")
require("Sampler object getters (GL 3.3): return the GL defaults" not in gl,
        "sampler queries must not return facade defaults")
require("glSamplerParameteri(sampler, pname" not in gl,
        "GL46 sampler integer setters must not depend on undeclared cross-TU entry points")
'''


def apply() -> None:
    text = GL46.read_text()
    text, n = PATTERN.subn(lambda _: REPLACEMENT, text, count=1)
    if n != 1:
        raise SystemExit(f'sampler region: expected one match, found {n}')
    GL46.write_text(text)
    audit = AUDIT.read_text()
    if 'GL46 sampler integer setters must not depend on undeclared cross-TU entry points' not in audit:
        audit += AUDIT_APPEND
        AUDIT.write_text(audit)
    verify()


def verify() -> None:
    text = GL46.read_text()
    for needle in ('gl46_get_sampler_scalar', 'gl46_set_sampler_scalar',
                   's->borderColorI', 's->borderColorUI'):
        if needle not in text:
            raise SystemExit(f'missing sampler invariant: {needle}')
    if 'glSamplerParameteri(sampler, pname' in text:
        raise SystemExit('cross-TU glSamplerParameteri dependency remains')
    canonical, n = PATTERN.subn(lambda _: REPLACEMENT, text, count=1)
    if n != 1 or canonical != text:
        raise SystemExit('sampler region is not canonical/idempotent')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL sampler semantics: PASS; migration is idempotent')


def main() -> None:
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply', action='store_true')
    g.add_argument('--verify', action='store_true')
    a = p.parse_args()
    apply() if a.apply else verify()

if __name__ == '__main__':
    main()
