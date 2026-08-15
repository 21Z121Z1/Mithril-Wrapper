package dev.mithril.e2e;

import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL21;
import org.lwjgl.opengl.GL30;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.List;
import java.util.Locale;
import java.util.Set;

@SuppressWarnings("UnstableApiUsage")
public final class MithrilClientGameTest implements FabricClientGameTest {
    private static final int SAMPLE_COUNT = 8;

    @Override
    public void runTest(ClientGameTestContext context) {
        Path root = Path.of(System.getProperty("mithril.e2e.root", "build/evidence"))
                .toAbsolutePath().normalize();
        try {
            Files.createDirectories(root.resolve("render"));
        } catch (IOException e) {
            throw new IllegalStateException("Could not create E2E evidence root " + root, e);
        }

        event(root, "client_gametest_started", "game_state", "Fabric Client GameTest entered production Minecraft");

        try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
            int chunkRenderTicks = singleplayer.getClientLevel().waitForChunksRender();
            context.waitTicks(40);

            GameIdentity identity = context.computeOnClient(client -> {
                String vendor = safe(GL11.glGetString(GL11.GL_VENDOR));
                String renderer = safe(GL11.glGetString(GL11.GL_RENDERER));
                String version = safe(GL11.glGetString(GL11.GL_VERSION));
                boolean level = client.level != null;
                boolean player = client.player != null;
                int width = client.getWindow().getWidth();
                int height = client.getWindow().getHeight();
                return new GameIdentity(vendor, renderer, version, level, player, width, height);
            });

            require(root, identity.levelLoaded, "GAME_WORLD_CREATE_FAILED", "game_state",
                    "Minecraft Client GameTest created no client level");
            require(root, identity.playerLoaded, "GAME_PLAYER_MISSING", "game_state",
                    "Minecraft Client GameTest created no player");
            require(root, identity.width > 1 && identity.height > 1, "GAME_INVALID_FRAMEBUFFER_SIZE", "game_state",
                    "Invalid Minecraft framebuffer size " + identity.width + "x" + identity.height);
            require(root,
                    identity.version.contains("Mithril-Wrapper") || identity.renderer.contains("Mithril-Wrapper"),
                    "WRAPPER_NOT_ACTIVE", "runtime_identity",
                    "Active OpenGL implementation is not Mithril: version=" + identity.version + ", renderer=" + identity.renderer);

            writeGameState(root, identity, chunkRenderTicks);
            writeOracle(root, "l1_process", "pass");
            writeOracle(root, "l2_runtime_identity", "pass");
            writeOracle(root, "l3_game_state", "pass");

            ControlResult textureControl = context.computeOnClient(client -> textureRoundTripControl(root));
            // Persist the observer result before asserting so a failing control is
            // still self-diagnosing in the compact artifact.
            writeControl(root, textureControl);
            require(root, textureControl.exact, "RENDER_READBACK_CONTROL_FAILED", "render_control",
                    "Texture/FBO/readPixels round-trip control failed: " + textureControl);

            List<FrameSample> samples = new ArrayList<>();
            for (int i = 0; i < SAMPLE_COUNT; i++) {
                if (i == SAMPLE_COUNT / 2) {
                    context.computeOnClient(client -> {
                        if (client.player != null) client.player.setYRot(client.player.getYRot() + 90.0f);
                        return true;
                    });
                    context.waitTicks(12);
                }
                context.waitTicks(4);
                final int frame = i + 1;
                FrameSample sample = context.computeOnClient(client ->
                        captureDefaultFramebuffer(root, frame,
                                client.getWindow().getWidth(), client.getWindow().getHeight()));
                samples.add(sample);
            }

            FrameSample best = samples.stream()
                    .max((a, b) -> Double.compare(a.score(), b.score()))
                    .orElseThrow();
            require(root, best.nonBlackPixels > 0, "RENDER_BLACK_FRAME", "render_probe",
                    "All sampled Minecraft framebuffers are black: " + best);
            require(root, best.distinctRgb > 16 && best.lumaStddev > 1.0,
                    "RENDER_CONSTANT_FRAME", "render_probe",
                    "Minecraft framebuffer lacks image variation: " + best);
            long distinctHashes = samples.stream().map(FrameSample::sha256).distinct().count();
            require(root, distinctHashes >= 2, "RENDER_FROZEN", "render_probe",
                    "Eight samples across a 90-degree camera turn produced one framebuffer hash");

            writeSamples(root, samples, best.frameId, distinctHashes);
            try {
                Files.copy(best.png, root.resolve("render/minecraft-framebuffer.png"),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } catch (IOException e) {
                throw fail(root, "ARTIFACT_STAGE_FAILED", "render_probe", "Could not stage canonical framebuffer", e);
            }
            Path screenshot = context.takeScreenshot("mithril-hosted-world");
            require(root, Files.isRegularFile(screenshot), "PRESENT_SCREENSHOT_MISSING", "presentation_probe",
                    "Client GameTest screenshot was not created");
            writePresentation(root, screenshot);
            writeOracle(root, "l4_gpu_render", "pass");
            writeOracle(root, "l5_presentation", "diagnostic");
            event(root, "client_gametest_completed", "render_probe", "L1-L4 passed; L5 retained as diagnostic");
        }
    }

    private static ControlResult textureRoundTripControl(Path root) {
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

    private static FrameSample captureDefaultFramebuffer(Path root, int frameId, int width, int height) {
        int previousRead = GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING);
        ByteBuffer bytes = BufferUtils.createByteBuffer(width * height * 4);
        try {
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, 0);
            GL11.glFinish();
            GL11.glReadPixels(0, 0, width, height, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, bytes);
        } finally {
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, previousRead);
        }
        byte[] raw = new byte[bytes.capacity()];
        for (int i = 0; i < raw.length; i++) raw[i] = bytes.get(i);
        long nonBlack = 0;
        Set<Integer> colors = new HashSet<>();
        double sum = 0.0, sum2 = 0.0;
        for (int i = 0; i < raw.length; i += 4) {
            int r = raw[i] & 0xff, g = raw[i + 1] & 0xff, b = raw[i + 2] & 0xff;
            if (r > 4 || g > 4 || b > 4) nonBlack++;
            if (colors.size() < 8192) colors.add((r << 16) | (g << 8) | b);
            double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            sum += l;
            sum2 += l * l;
        }
        int pixels = width * height;
        double mean = sum / pixels;
        double stddev = Math.sqrt(Math.max(0.0, sum2 / pixels - mean * mean));
        String sha = sha256(raw);
        Path rawPath = root.resolve("render/frame-%04d.rgba".formatted(frameId));
        Path pngPath = root.resolve("render/frame-%04d.png".formatted(frameId));
        try {
            Files.write(rawPath, raw);
            BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
            for (int y = 0; y < height; y++) {
                int srcY = height - 1 - y;
                for (int x = 0; x < width; x++) {
                    int o = (srcY * width + x) * 4;
                    int argb = ((raw[o + 3] & 0xff) << 24) | ((raw[o] & 0xff) << 16)
                            | ((raw[o + 1] & 0xff) << 8) | (raw[o + 2] & 0xff);
                    image.setRGB(x, y, argb);
                }
            }
            ImageIO.write(image, "PNG", pngPath.toFile());
        } catch (IOException e) {
            throw new IllegalStateException("Could not write framebuffer evidence", e);
        }
        return new FrameSample(frameId, width, height, raw.length, nonBlack, colors.size(), mean, stddev, sha, pngPath);
    }

    private static void writeGameState(Path root, GameIdentity id, int chunkRenderTicks) {
        write(root.resolve("game-state.json"), """
                {
                  "schema_version": "1.0",
                  "minecraft": "26.2",
                  "world_loaded": %s,
                  "player_loaded": %s,
                  "chunks_rendered": true,
                  "chunk_render_ticks": %d,
                  "framebuffer_width": %d,
                  "framebuffer_height": %d,
                  "gl_vendor": "%s",
                  "gl_renderer": "%s",
                  "gl_version": "%s"
                }
                """.formatted(id.levelLoaded, id.playerLoaded, chunkRenderTicks, id.width, id.height,
                escape(id.vendor), escape(id.renderer), escape(id.version)));
    }

    private static void writeControl(Path root, ControlResult c) {
        write(root.resolve("readback-control.json"), """
                {"schema_version":"1.0","name":"%s","exact":%s,"mismatch_bytes":%d}
                """.formatted(escape(c.name), c.exact, c.mismatches));
    }

    private static void writePresentation(Path root, Path screenshot) {
        write(root.resolve("presentation.json"), """
                {"schema_version":"1.0","status":"diagnostic","client_screenshot":"%s"}
                """.formatted(escape(screenshot.toAbsolutePath().normalize().toString())));
    }

    private static void writeSamples(Path root, List<FrameSample> samples, int selected, long distinctHashes) {
        StringBuilder b = new StringBuilder();
        b.append("{\n  \"schema_version\": \"1.0\",\n  \"sample_count\": ").append(samples.size())
                .append(",\n  \"selected_frame_id\": ").append(selected)
                .append(",\n  \"distinct_hashes\": ").append(distinctHashes).append(",\n  \"samples\": [\n");
        for (int i = 0; i < samples.size(); i++) {
            FrameSample s = samples.get(i);
            b.append("    {\"frame_id\":").append(s.frameId)
                    .append(",\"width\":").append(s.width).append(",\"height\":").append(s.height)
                    .append(",\"byte_count\":").append(s.byteCount)
                    .append(",\"non_black_pixels\":").append(s.nonBlackPixels)
                    .append(",\"distinct_rgb\":").append(s.distinctRgb)
                    .append(",\"mean_luma\":").append(String.format(Locale.ROOT, "%.6f", s.meanLuma))
                    .append(",\"luma_stddev\":").append(String.format(Locale.ROOT, "%.6f", s.lumaStddev))
                    .append(",\"sha256\":\"").append(s.sha256).append("\"}");
            if (i + 1 < samples.size()) b.append(',');
            b.append('\n');
        }
        b.append("  ]\n}\n");
        write(root.resolve("render/samples.json"), b.toString());
    }

    private static synchronized void writeOracle(Path root, String name, String status) {
        Path path = root.resolve("oracle-results.json");
        java.util.LinkedHashMap<String, String> values = new java.util.LinkedHashMap<>();
        values.put("schema_version", "1.0");
        if (Files.isRegularFile(path)) {
            try {
                String text = Files.readString(path);
                for (String key : new String[]{"l1_process","l2_runtime_identity","l3_game_state","l4_gpu_render","l5_presentation"}) {
                    String needle = "\"" + key + "\":\"";
                    int p = text.indexOf(needle);
                    if (p >= 0) {
                        int start = p + needle.length();
                        int end = text.indexOf('"', start);
                        if (end > start) values.put(key, text.substring(start, end));
                    }
                }
            } catch (IOException ignored) {}
        }
        values.put(name, status);
        StringBuilder b = new StringBuilder("{\n");
        int i = 0;
        for (var e : values.entrySet()) {
            b.append("  \"").append(e.getKey()).append("\":\"").append(escape(e.getValue())).append("\"");
            if (++i < values.size()) b.append(',');
            b.append('\n');
        }
        b.append("}\n");
        write(path, b.toString());
    }

    private static void event(Path root, String event, String phase, String message) {
        String line = "{\"schema_version\":\"1.0\",\"timestamp\":\"" + Instant.now()
                + "\",\"producer\":\"fabric-client-gametest\",\"phase\":\"" + escape(phase)
                + "\",\"event\":\"" + escape(event) + "\",\"message\":\"" + escape(message) + "\"}\n";
        try {
            Files.writeString(root.resolve("events.jsonl"), line, StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (IOException ignored) {}
    }

    private static void require(Path root, boolean condition, String id, String phase, String message) {
        if (!condition) throw fail(root, id, phase, message, null);
    }

    private static IllegalStateException fail(Path root, String id, String phase, String message, Throwable cause) {
        Path path = root.resolve("failure.json");
        try {
            String current = Files.isRegularFile(path) ? Files.readString(path) : "";
            if (!current.contains("\"status\": \"failed\"") && !current.contains("\"status\":\"failed\"")) {
                write(path, """
                        {
                          "schema_version": "1.0",
                          "status": "failed",
                          "failure_id": "%s",
                          "phase": "%s",
                          "retryable": false,
                          "message": "%s",
                          "recorded_at": "%s"
                        }
                        """.formatted(escape(id), escape(phase), escape(message), Instant.now()));
            }
        } catch (Exception ignored) {}
        event(root, "oracle_failed", phase, id + ": " + message);
        return cause == null ? new IllegalStateException(message) : new IllegalStateException(message, cause);
    }

    private static void write(Path path, String text) {
        try {
            Files.createDirectories(path.getParent());
            Files.writeString(path, text, StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new IllegalStateException("Could not write " + path, e);
        }
    }

    private static String safe(String s) { return s == null ? "" : s; }
    private static String escape(String s) { return safe(s).replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n"); }
    private static String sha256(byte[] bytes) {
        try { return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)); }
        catch (Exception e) { throw new IllegalStateException(e); }
    }

    private record GameIdentity(String vendor, String renderer, String version,
                                boolean levelLoaded, boolean playerLoaded, int width, int height) {}
    private record ControlResult(boolean exact, int mismatches, String name) {}
    private record FrameSample(int frameId, int width, int height, int byteCount,
                               long nonBlackPixels, int distinctRgb, double meanLuma,
                               double lumaStddev, String sha256, Path png) {
        double score() { return lumaStddev * 1_000_000.0 + distinctRgb * 1_000.0 + nonBlackPixels; }
    }
}
