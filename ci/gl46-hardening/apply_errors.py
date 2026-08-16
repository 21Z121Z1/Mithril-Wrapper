#!/usr/bin/env python3
"""Restore spec-observable OpenGL error and binding-query semantics.

The migration deliberately normalizes the whole texture-binding query region,
so re-running this helper is idempotent and can never duplicate switch cases.
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[2]
GETTER = ROOT / 'Mithril-Wrapper-cpp/MG_Impl/Getter.cpp'
AUDIT = ROOT / 'verify/gl46_semantic_audit.py'

OLD = '''GLenum glGetError(void) {
    MITHRIL_ENSURE_INIT();
    // Mirror MobileGlues: always return GL_NO_ERROR to prevent Minecraft from
    // spamming the log with GL errors that are harmless in the translation layer.
    mithril::state_take_error();
    return GL_NO_ERROR;
}
'''
NEW = '''GLenum glGetError(void) {
    MITHRIL_ENSURE_INIT();
    /* OpenGL exposes the oldest pending error and removes exactly that error
     * from the context queue.  The state machine already maintains the queue;
     * never discard it merely to silence a workload's diagnostics. */
    return mithril::state_take_error();
}
'''

DEFINE_ANCHOR = '''#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
'''
DEFINE_BLOCK = DEFINE_ANCHOR + '''#ifndef GL_TEXTURE_BINDING_1D
#define GL_TEXTURE_BINDING_1D 0x8068
#endif
#ifndef GL_TEXTURE_BINDING_RECTANGLE
#define GL_TEXTURE_BINDING_RECTANGLE 0x84F6
#endif
#ifndef GL_TEXTURE_BINDING_BUFFER
#define GL_TEXTURE_BINDING_BUFFER 0x8C2C
#endif
#ifndef GL_TEXTURE_BINDING_1D_ARRAY
#define GL_TEXTURE_BINDING_1D_ARRAY 0x8C1C
#endif
#ifndef GL_TEXTURE_BINDING_CUBE_MAP_ARRAY
#define GL_TEXTURE_BINDING_CUBE_MAP_ARRAY 0x900A
#endif
#ifndef GL_TEXTURE_BINDING_2D_MULTISAMPLE
#define GL_TEXTURE_BINDING_2D_MULTISAMPLE 0x9104
#endif
#ifndef GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY
#define GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY 0x9105
#endif
'''

REGION_START = '''        case GL_PACK_LSB_FIRST:               *params = g_state->pixelStore.packLSBFirst ? GL_TRUE : GL_FALSE; break;
'''
REGION_END = '''        case GL_BLEND_SRC_RGB:                *params = (GLint)g_state->blends[0].srcRGB; break;
'''
BINDING_NEW = '''        case GL_TEXTURE_BINDING_1D:           *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_1D].name; break;
        case GL_TEXTURE_BINDING_2D:           *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2D].name; break;
        case GL_TEXTURE_BINDING_3D:           *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_3D].name; break;
        case GL_TEXTURE_BINDING_CUBE_MAP:     *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::CubeMap].name; break;
        case GL_TEXTURE_BINDING_RECTANGLE:    *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::Rectangle].name; break;
        case GL_TEXTURE_BINDING_2D_MULTISAMPLE:*params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2DMultisample].name; break;
        case GL_TEXTURE_BINDING_BUFFER:       *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::Buffer].name; break;
        case GL_TEXTURE_BINDING_1D_ARRAY:     *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_1DArray].name; break;
        case GL_TEXTURE_BINDING_2D_ARRAY:     *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2DArray].name; break;
        case GL_TEXTURE_BINDING_CUBE_MAP_ARRAY:*params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::CubeMapArray].name; break;
        case GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY:*params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2DMultisampleArray].name; break;
'''

PNAMES = (
    'GL_TEXTURE_BINDING_1D', 'GL_TEXTURE_BINDING_2D', 'GL_TEXTURE_BINDING_3D',
    'GL_TEXTURE_BINDING_CUBE_MAP', 'GL_TEXTURE_BINDING_RECTANGLE',
    'GL_TEXTURE_BINDING_2D_MULTISAMPLE', 'GL_TEXTURE_BINDING_BUFFER',
    'GL_TEXTURE_BINDING_1D_ARRAY', 'GL_TEXTURE_BINDING_2D_ARRAY',
    'GL_TEXTURE_BINDING_CUBE_MAP_ARRAY', 'GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY',
)

AUDIT_CHECK = '''\nrequire("return mithril::state_take_error();" in (root / "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp").read_text(),\n        "glGetError must expose the context error queue instead of swallowing it")\n'''
AUDIT_BINDING_CHECK = '''\n_getter = (root / "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp").read_text()\nfor _pname in ("GL_TEXTURE_BINDING_1D", "GL_TEXTURE_BINDING_2D", "GL_TEXTURE_BINDING_3D",\n               "GL_TEXTURE_BINDING_CUBE_MAP", "GL_TEXTURE_BINDING_RECTANGLE",\n               "GL_TEXTURE_BINDING_2D_MULTISAMPLE", "GL_TEXTURE_BINDING_BUFFER",\n               "GL_TEXTURE_BINDING_1D_ARRAY", "GL_TEXTURE_BINDING_2D_ARRAY",\n               "GL_TEXTURE_BINDING_CUBE_MAP_ARRAY", "GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY"):\n    require(_getter.count("case " + _pname + ":") == 1,\n            "texture binding getter must appear exactly once: " + _pname)\n'''


def normalize_binding_region(text: str) -> str:
    start = text.find(REGION_START)
    if start < 0:
        raise SystemExit('texture binding region start anchor not found')
    start += len(REGION_START)
    end = text.find(REGION_END, start)
    if end < 0:
        raise SystemExit('texture binding region end anchor not found')
    return text[:start] + BINDING_NEW + text[end:]


def apply():
    text = GETTER.read_text()
    if DEFINE_BLOCK not in text:
        if DEFINE_ANCHOR not in text:
            raise SystemExit('getter define anchor not found')
        text = text.replace(DEFINE_ANCHOR, DEFINE_BLOCK, 1)
    if OLD in text:
        text = text.replace(OLD, NEW, 1)
    elif NEW not in text:
        raise SystemExit('glGetError anchor not found')
    text = normalize_binding_region(text)
    GETTER.write_text(text)

    audit = AUDIT.read_text()
    if 'glGetError must expose the context error queue' not in audit:
        audit += AUDIT_CHECK
    # Upgrade an older presence-only guard to an exact-count guard.
    old_guard_marker = 'missing texture binding getter:'
    if old_guard_marker in audit:
        prefix = audit[:audit.rfind('\n_getter =', 0, audit.find(old_guard_marker))]
        if prefix != audit:
            audit = prefix.rstrip() + '\n'
    if 'texture binding getter must appear exactly once:' not in audit:
        audit += AUDIT_BINDING_CHECK
    AUDIT.write_text(audit)
    verify()


def verify():
    text = GETTER.read_text()
    if NEW not in text:
        raise SystemExit('glGetError is not spec-observable')
    if DEFINE_BLOCK not in text:
        raise SystemExit('missing fallback texture binding enum definitions')
    if 'mithril::state_take_error();\n    return GL_NO_ERROR;' in text:
        raise SystemExit('legacy error swallowing remains')
    for pname in PNAMES:
        count = text.count(f'case {pname}:')
        if count != 1:
            raise SystemExit(f'{pname}: expected one switch case, found {count}')
    if normalize_binding_region(text) != text:
        raise SystemExit('binding getter region is not canonical/idempotent')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL error/binding getter semantics: PASS; migration is idempotent')


def main():
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply', action='store_true')
    g.add_argument('--verify', action='store_true')
    a = p.parse_args()
    apply() if a.apply else verify()

if __name__ == '__main__':
    main()
