#!/usr/bin/env python3
"""Remove the extra DirectMetal default-framebuffer Y reversal.

Mithril's default render target is already produced in GL logical row order by
the default-FBO render path. Applying another CPU row reversal in glReadPixels
returns top-to-bottom data, violating the OpenGL bottom-left readback contract.
The cross-platform Minecraft oracle detects this as a vertical inversion.
"""
from pathlib import Path
import argparse

ROOT=Path(__file__).resolve().parents[2]
BACKEND=ROOT/'Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalBackend.mm'
AUDIT=ROOT/'verify/gl46_semantic_audit.py'

OLD='''    // User FBO textures are already stored in GL bottom-left row order; only
    // the drawable/default framebuffer uses the presentation-oriented Metal
    // row order that needs conversion on readback.
    return dmt::dmt_internal_read_pixels(src, x, y, w, h, format, type,
                                         out_pixels, readDefaultFramebuffer ? 1 : 0);
'''
NEW='''    // Both user FBOs and the DirectMetal default render target are already
    // stored in GL logical bottom-left row order by their render paths.  A
    // second CPU Y reversal here would make glReadPixels return top-to-bottom
    // rows.  Keep the internal flip facility for explicit non-GL callers, but
    // OpenGL readback must copy rows as stored.
    return dmt::dmt_internal_read_pixels(src, x, y, w, h, format, type,
                                         out_pixels, 0);
'''
AUDIT_APPEND='''\n_metal_backend=(root/"Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalBackend.mm").read_text()\nrequire("out_pixels, readDefaultFramebuffer ? 1 : 0" not in _metal_backend,\n        "DirectMetal glReadPixels must not vertically reverse the already GL-oriented default framebuffer")\nrequire("out_pixels, 0);" in _metal_backend,\n        "DirectMetal glReadPixels must preserve GL bottom-left row order")\n'''

def apply():
    text=BACKEND.read_text()
    if OLD in text:
        BACKEND.write_text(text.replace(OLD,NEW,1))
    elif NEW not in text:
        raise SystemExit('DirectMetal readback anchor not found')
    audit=AUDIT.read_text()
    if 'DirectMetal glReadPixels must preserve GL bottom-left row order' not in audit:
        AUDIT.write_text(audit+AUDIT_APPEND)
    verify()

def verify():
    text=BACKEND.read_text()
    if NEW not in text: raise SystemExit('correct default-FBO readback orientation missing')
    if 'out_pixels, readDefaultFramebuffer ? 1 : 0' in text:
        raise SystemExit('legacy double Y reversal remains')
    compile(AUDIT.read_text(),str(AUDIT),'exec')
    print('DirectMetal glReadPixels orientation source invariant: PASS')

def main():
    p=argparse.ArgumentParser(); g=p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply',action='store_true'); g.add_argument('--verify',action='store_true')
    a=p.parse_args(); apply() if a.apply else verify()
if __name__=='__main__': main()
