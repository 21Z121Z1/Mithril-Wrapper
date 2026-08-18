#!/usr/bin/env python3
from pathlib import Path

path = Path("src/backend/types.h")
text = path.read_text()
wrong = '''struct FboSpec {
    std::vector<FboAttach> colors;
    FboAttach depth_stencil;
    bool has_depth_stencil = false;
    GLenum depth_stencil_format = GL_DEPTH24_STENCIL8;
    std::vector<GLenum> draw_bufs;
    GLenum read_buf = GL_COLOR_ATTACHMENT0;
};
'''
correct = '''struct FboSpec {
    std::vector<FboAttach> color;
    std::vector<GLenum> draw_bufs;
    GLenum read_buf = GL_COLOR_ATTACHMENT0;
    bool has_depth = false;
    FboAttach depth;
    uint32_t width = 0, height = 0;
};
'''
if text.count(wrong) != 1:
    raise SystemExit(f"expected one regressed FboSpec, found {text.count(wrong)}")
text = text.replace(wrong, correct, 1)
path.write_text(text)
print("restored integration/directmetal-next FboSpec contract")
