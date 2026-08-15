#!/usr/bin/env python3
from pathlib import Path

p = Path('.github/ci/minecraft-e2e/src/main/java/dev/mithril/e2e/MithrilClientGameTest.java')
text = p.read_text(encoding='utf-8')

old = 'import org.lwjgl.opengl.GL11;\nimport org.lwjgl.opengl.GL30;'
new = 'import org.lwjgl.opengl.GL11;\nimport org.lwjgl.opengl.GL12;\nimport org.lwjgl.opengl.GL15;\nimport org.lwjgl.opengl.GL21;\nimport org.lwjgl.opengl.GL30;'
if text.count(old) != 1:
    raise SystemExit('OpenGL import site mismatch')
text = text.replace(old, new)

old = '''            ControlResult textureControl = context.computeOnClient(client -> textureRoundTripControl());
            require(root, textureControl.exact, "RENDER_READBACK_CONTROL_FAILED", "render_control",
                    "Texture/FBO/readPixels round-trip control failed: " + textureControl);
            writeControl(root, textureControl);'''
new = '''            ControlResult textureControl = context.computeOnClient(client -> textureRoundTripControl(root));
            // Persist the observer result before asserting so a failing control is
            // still self-diagnosing in the compact artifact.
            writeControl(root, textureControl);
            require(root, textureControl.exact, "RENDER_READBACK_CONTROL_FAILED", "render_control",
                    "Texture/FBO/readPixels round-trip control failed: " + textureControl);'''
if text.count(old) != 1:
    raise SystemExit('control call site mismatch')
text = text.replace(old, new)

start = text.index('    private static ControlResult textureRoundTripControl() {')
end = text.index('\n    private static FrameSample captureDefaultFramebuffer', start)
new_method = r'''    private static ControlResult textureRoundTripControl(Path root) {
        final int w = 32, h = 32;
        int previousRead = GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING);
        int previousDraw = GL11.glGetInteger(GL30.GL_DRAW_FRAMEBUFFER_BINDING);
        int previousTexture2D = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
        int previousPackBuffer = GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int previousUnpackBuffer = GL11.glGetInteger(GL21.GL_PIXEL_UNPACK_BUFFER_BINDING);
        int previousPackAlignment = GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT);
        int previousPackRowLength = GL11.glGetInteger(GL12.GL_PACK_ROW_LENGTH);
        int previousPackSkipRows = GL11.glGetInteger(GL12.GL_PACK_SKIP_ROWS);
        int previousPackSkipPixels = GL11.glGetInteger(GL12.GL_PACK_SKIP_PIXELS);
        int previousPackImageHeight = GL11.glGetInteger(GL12.GL_PACK_IMAGE_HEIGHT);
        int previousPackSkipImages = GL11.glGetInteger(GL12.GL_PACK_SKIP_IMAGES);
        int previousUnpackAlignment = GL11.glGetInteger(GL11.GL_UNPACK_ALIGNMENT);
        int previousUnpackRowLength = GL11.glGetInteger(GL12.GL_UNPACK_ROW_LENGTH);
        int previousUnpackSkipRows = GL11.glGetInteger(GL12.GL_UNPACK_SKIP_ROWS);
        int previousUnpackSkipPixels = GL11.glGetInteger(GL12.GL_UNPACK_SKIP_PIXELS);
        int previousUnpackImageHeight = GL11.glGetInteger(GL12.GL_UNPACK_IMAGE_HEIGHT);
        int previousUnpackSkipImages = GL11.glGetInteger(GL12.GL_UNPACK_SKIP_IMAGES);

        int texture = GL11.glGenTextures();
        int fbo = GL30.glGenFramebuffers();
        ByteBuffer pattern = BufferUtils.createByteBuffer(w * h * 4);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                pattern.put((byte) ((x * 7 + y * 3) & 0xff));
                pattern.put((byte) ((x * 5 + y * 11) & 0xff));
                pattern.put((byte) ((x * 13 + y * 2) & 0xff));
                pattern.put((byte) 0xff);
            }
        }
        pattern.flip();
        try {
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, 0);
            GL15.glBindBuffer(GL21.GL_PIXEL_UNPACK_BUFFER, 0);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 4);
            GL11.glPixelStorei(GL12.GL_PACK_ROW_LENGTH, 0);
            GL11.glPixelStorei(GL12.GL_PACK_SKIP_ROWS, 0);
            GL11.glPixelStorei(GL12.GL_PACK_SKIP_PIXELS, 0);
            GL11.glPixelStorei(GL12.GL_PACK_IMAGE_HEIGHT, 0);
            GL11.glPixelStorei(GL12.GL_PACK_SKIP_IMAGES, 0);
            GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 4);
            GL11.glPixelStorei(GL12.GL_UNPACK_ROW_LENGTH, 0);
            GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_ROWS, 0);
            GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_PIXELS, 0);
            GL11.glPixelStorei(GL12.GL_UNPACK_IMAGE_HEIGHT, 0);
            GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_IMAGES, 0);

            GL11.glBindTexture(GL11.GL_TEXTURE_2D, texture);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_NEAREST);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_NEAREST);
            GL11.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA8, w, h, 0,
                    GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, pattern);
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, fbo);
            GL30.glFramebufferTexture2D(GL30.GL_FRAMEBUFFER, GL30.GL_COLOR_ATTACHMENT0,
                    GL11.GL_TEXTURE_2D, texture, 0);
            int status = GL30.glCheckFramebufferStatus(GL30.GL_FRAMEBUFFER);
            if (status != GL30.GL_FRAMEBUFFER_COMPLETE) {
                writeControlBytes(root, pattern, null);
                return new ControlResult(false, w * h * 4,
                        "framebuffer-incomplete-0x%04x".formatted(status));
            }
            ByteBuffer read = BufferUtils.createByteBuffer(w * h * 4);
            GL11.glFinish();
            GL11.glReadPixels(0, 0, w, h, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, read);
            GL11.glFinish();
            int mismatches = 0;
            for (int i = 0; i < w * h * 4; i++) {
                if (read.get(i) != pattern.get(i)) mismatches++;
            }
            writeControlBytes(root, pattern, read);
            return new ControlResult(mismatches == 0, mismatches, "texture-fbo-readpixels");
        } finally {
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, previousRead);
            GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER, previousDraw);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, previousTexture2D);
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, previousPackBuffer);
            GL15.glBindBuffer(GL21.GL_PIXEL_UNPACK_BUFFER, previousUnpackBuffer);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, previousPackAlignment);
            GL11.glPixelStorei(GL12.GL_PACK_ROW_LENGTH, previousPackRowLength);
            GL11.glPixelStorei(GL12.GL_PACK_SKIP_ROWS, previousPackSkipRows);
            GL11.glPixelStorei(GL12.GL_PACK_SKIP_PIXELS, previousPackSkipPixels);
            GL11.glPixelStorei(GL12.GL_PACK_IMAGE_HEIGHT, previousPackImageHeight);
            GL11.glPixelStorei(GL12.GL_PACK_SKIP_IMAGES, previousPackSkipImages);
            GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
            GL11.glPixelStorei(GL12.GL_UNPACK_ROW_LENGTH, previousUnpackRowLength);
            GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_ROWS, previousUnpackSkipRows);
            GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_PIXELS, previousUnpackSkipPixels);
            GL11.glPixelStorei(GL12.GL_UNPACK_IMAGE_HEIGHT, previousUnpackImageHeight);
            GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_IMAGES, previousUnpackSkipImages);
            GL30.glDeleteFramebuffers(fbo);
            GL11.glDeleteTextures(texture);
        }
    }

    private static void writeControlBytes(Path root, ByteBuffer expected, ByteBuffer actual) {
        Path dir = root.resolve("render-control");
        try {
            Files.createDirectories(dir);
            byte[] exp = new byte[expected.capacity()];
            for (int i = 0; i < exp.length; i++) exp[i] = expected.get(i);
            Files.write(dir.resolve("expected.rgba"), exp);
            if (actual != null) {
                byte[] got = new byte[actual.capacity()];
                for (int i = 0; i < got.length; i++) got[i] = actual.get(i);
                Files.write(dir.resolve("actual.rgba"), got);
            }
        } catch (IOException e) {
            event(root, "diagnostic_write_failed", "render_control",
                    "Could not persist readback-control bytes: " + e);
        }
    }
'''
text = text[:start] + new_method + text[end:]
p.write_text(text, encoding='utf-8')
