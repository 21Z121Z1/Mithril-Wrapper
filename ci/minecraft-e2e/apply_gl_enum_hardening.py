#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/include/GL/gl.h')
text = p.read_text(encoding='utf-8')
old = '''#ifndef GL_PACK_SKIP_IMAGES
#define GL_PACK_SKIP_IMAGES          0x806B
#endif
'''
new = '''#ifndef GL_PACK_SKIP_IMAGES
#define GL_PACK_SKIP_IMAGES          0x806B
#endif
#ifndef GL_PIXEL_PACK_BUFFER_BINDING
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_PACK_SWAP_BYTES
#define GL_PACK_SWAP_BYTES           0x0D00
#endif
#ifndef GL_PACK_LSB_FIRST
#define GL_PACK_LSB_FIRST            0x0D01
#endif
#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES         0x0CF0
#endif
#ifndef GL_UNPACK_LSB_FIRST
#define GL_UNPACK_LSB_FIRST          0x0CF1
#endif
'''
if text.count(old) != 1:
    raise SystemExit('pixel-store enum insertion site mismatch')
p.write_text(text.replace(old, new), encoding='utf-8')
