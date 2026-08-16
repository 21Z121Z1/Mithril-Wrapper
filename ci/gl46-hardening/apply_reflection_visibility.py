#!/usr/bin/env python3
"""Keep backend-synthetic UBOs out of the public OpenGL reflection model.

Mithril lowers default-block/loose uniforms to synthetic descriptor-backed UBOs
internally.  OpenGL still requires GL_UNIFORM_BLOCK_INDEX == -1 for those
uniforms.  Program::blockIndexForDescriptor contains only application-declared
uniform blocks, so it is the authoritative visibility discriminator without
changing backend routing.
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[2]
GL46 = ROOT / 'Mithril-Wrapper-cpp/MG_Impl/GL46_Compat.cpp'
AUDIT = ROOT / 'verify/gl46_semantic_audit.py'

OLD = 'case GL_UNIFORM_BLOCK_INDEX:   params[i] = u.blockIndex; break;'
NEW = '''case GL_UNIFORM_BLOCK_INDEX:
                params[i] = (u.blockIndex >= 0 &&
                             u.blockBinding >= 0 &&
                             p->blockIndexForDescriptor.count((GLuint)u.blockBinding) != 0)
                                ? u.blockIndex : -1;
                break;'''

AUDIT_APPEND = r'''
require("p->blockIndexForDescriptor.count((GLuint)u.blockBinding)" in gl,
        "GL uniform reflection must hide backend-synthetic UBOs from GL_UNIFORM_BLOCK_INDEX")
'''

def apply():
    text = GL46.read_text()
    if OLD in text:
        text = text.replace(OLD, NEW, 1)
        GL46.write_text(text)
    elif NEW not in text:
        raise SystemExit('uniform block visibility anchor not found')
    audit = AUDIT.read_text()
    if 'GL uniform reflection must hide backend-synthetic UBOs' not in audit:
        AUDIT.write_text(audit + AUDIT_APPEND)
    verify()

def verify():
    text = GL46.read_text()
    if text.count(NEW) != 1:
        raise SystemExit('canonical synthetic-block visibility mapping missing or duplicated')
    if OLD in text:
        raise SystemExit('raw internal blockIndex is still exposed')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL uniform block visibility semantics: PASS')

def main():
    p=argparse.ArgumentParser(); g=p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply', action='store_true'); g.add_argument('--verify', action='store_true')
    a=p.parse_args(); apply() if a.apply else verify()

if __name__=='__main__': main()
