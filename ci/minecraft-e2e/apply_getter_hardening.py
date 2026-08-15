#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Impl/Getter.cpp')
text = p.read_text(encoding='utf-8')
old = '''        case GL_UNPACK_ALIGNMENT:             *params = g_state->pixelStore.unpackAlignment; break;
        case GL_PACK_ALIGNMENT:               *params = g_state->pixelStore.packAlignment; break;
        case GL_UNPACK_ROW_LENGTH:            *params = g_state->pixelStore.unpackRowLength; break;
        case GL_UNPACK_IMAGE_HEIGHT:          *params = g_state->pixelStore.unpackImageHeight; break;
        case GL_TEXTURE_BINDING_2D:           *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2D].name; break;'''
new = '''        // Pixel transfer state is observable GL state.  Minecraft and our E2E
        // control both need to save/restore it exactly; returning zero for an
        // unsupported getter silently corrupts subsequent uploads/readbacks.
        case GL_PIXEL_PACK_BUFFER_BINDING:
            *params = (GLint)g_state->bufferBindings[(int)mithril::BufferTarget::PixelPack].name; break;
        case GL_PIXEL_UNPACK_BUFFER_BINDING:
            *params = (GLint)g_state->bufferBindings[(int)mithril::BufferTarget::PixelUnpack].name; break;
        case GL_UNPACK_ALIGNMENT:             *params = g_state->pixelStore.unpackAlignment; break;
        case GL_UNPACK_ROW_LENGTH:            *params = g_state->pixelStore.unpackRowLength; break;
        case GL_UNPACK_IMAGE_HEIGHT:          *params = g_state->pixelStore.unpackImageHeight; break;
        case GL_UNPACK_SKIP_ROWS:             *params = g_state->pixelStore.unpackSkipRows; break;
        case GL_UNPACK_SKIP_PIXELS:           *params = g_state->pixelStore.unpackSkipPixels; break;
        case GL_UNPACK_SKIP_IMAGES:           *params = g_state->pixelStore.unpackSkipImages; break;
        case GL_UNPACK_SWAP_BYTES:            *params = g_state->pixelStore.unpackSwapBytes ? GL_TRUE : GL_FALSE; break;
        case GL_UNPACK_LSB_FIRST:             *params = g_state->pixelStore.unpackLSBFirst ? GL_TRUE : GL_FALSE; break;
        case GL_PACK_ALIGNMENT:               *params = g_state->pixelStore.packAlignment; break;
        case GL_PACK_ROW_LENGTH:              *params = g_state->pixelStore.packRowLength; break;
        case GL_PACK_IMAGE_HEIGHT:            *params = g_state->pixelStore.packImageHeight; break;
        case GL_PACK_SKIP_ROWS:               *params = g_state->pixelStore.packSkipRows; break;
        case GL_PACK_SKIP_PIXELS:             *params = g_state->pixelStore.packSkipPixels; break;
        case GL_PACK_SKIP_IMAGES:             *params = g_state->pixelStore.packSkipImages; break;
        case GL_PACK_SWAP_BYTES:              *params = g_state->pixelStore.packSwapBytes ? GL_TRUE : GL_FALSE; break;
        case GL_PACK_LSB_FIRST:               *params = g_state->pixelStore.packLSBFirst ? GL_TRUE : GL_FALSE; break;
        case GL_TEXTURE_BINDING_2D:           *params = (GLint)g_state->textureBindings[g_state->activeTextureUnit][(int)mithril::TextureTarget::_2D].name; break;'''
if text.count(old) != 1:
    raise SystemExit(f'pixel-store getter block mismatch: {text.count(old)}')
p.write_text(text.replace(old, new), encoding='utf-8')
