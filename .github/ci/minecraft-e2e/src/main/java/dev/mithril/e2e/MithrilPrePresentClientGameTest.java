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
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
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
public final class MithrilPrePresentClientGameTest implements FabricClientGameTest {
    private static final int SAMPLE_COUNT = 8;
    private static final int CAPTURE_TIMEOUT_TICKS = 80;

    @Override
    public void runTest(ClientGameTestContext context) {
        Path root = Path.of(System.getProperty("mithril.e2e.root", "build/evidence"))
                .toAbsolutePath().normalize();
        String requestedBackend = System.getenv().getOrDefault("MITHRIL_BACKEND", "metal");
        String expectedBackend = "vulkan".equals(requestedBackend)
                ? "Vulkan 1.2 (MoltenVK)" : "DirectMetal";
        try {
            Files.createDirectories(root.resolve("render"));
        } catch (IOException e) {
            throw new IllegalStateException("Could not create E2E evidence root " + root, e);
        }

        event(root, "client_gametest_started", "game_state",
                "Fabric production Client GameTest entered real Minecraft");

        try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
            int chunkRenderTicks = singleplayer.getClientLevel().waitForChunksRender();
            context.waitTicks(40);

            GameIdentity identity = context.computeOnClient(client -> new GameIdentity(
                    safe(GL11.glGetString(GL11.GL_VENDOR)),
                    safe(GL11.glGetString(GL11.GL_RENDERER)),
                    safe(GL11.glGetString(GL11.GL_VERSION)),
                    client.level != null,
                    client.player != null,
                    client.getWindow().getWidth(),
                    client.getWindow().getHeight()));

            require(root, identity.levelLoaded, "GAME_WORLD_CREATE_FAILED", "game_state",
                    "Minecraft Client GameTest has no client level");
            require(root, identity.playerLoaded, "GAME_PLAYER_MISSING", "game_state",
                    "Minecraft Client GameTest has no player");
            require(root, identity.width > 1 && identity.height > 1,
                    "GAME_INVALID_FRAMEBUFFER_SIZE", "game_state",
                    "Invalid Minecraft framebuffer size " + identity.width + "x" + identity.height);
            require(root,
                    identity.version.contains("Mithril-Wrapper") && identity.version.contains(expectedBackend)
                            && identity.renderer.contains("Mithril-Wrapper") && identity.renderer.contains(expectedBackend),
                    "WRAPPER_NOT_ACTIVE", "runtime_identity",
                    "Active OpenGL implementation is not Mithril " + expectedBackend + ": version="
                            + identity.version + ", renderer=" + identity.renderer);

            writeGameState(root, identity, chunkRenderTicks);
            writeOracle(root, "l1_process", "pass");
            writeOracle(root, "l2_runtime_identity", "pass");
            writeOracle(root, "l3_game_state", "pass");

            ControlResult control = context.computeOnClient(client -> textureRoundTripControl(root));
            writeControl(root, control);
            require(root, control.exact, "RENDER_READBACK_CONTROL_FAILED", "render_control",
                    "Texture/FBO/readPixels round-trip control failed: " + control);

            List<FrameSample> samples = new ArrayList<>();
            for (int i = 0; i < SAMPLE_COUNT; i++) {
                if (i == SAMPLE_COUNT / 2) {
                    context.computeOnClient(client -> {
                        if (client.player != null) client.player.setYRot(client.player.getYRot() + 90.0f);
                        return true;
                    });
                    context.waitTicks(12);
                }
                int frame = i + 1;
                samples.add(requestPrePresentCapture(context, root, frame));
                context.waitTicks(3);
            }

            writeSamples(root, samples);
            FrameSample best = samples.stream()
                    .max((a, b) -> Double.compare(a.score(), b.score()))
                    .orElseThrow();
            long distinctHashes = samples.stream().map(FrameSample::sha256).distinct().count();

            require(root, best.nonBlackPixels > 0, "RENDER_BLACK_FRAME", "render_probe",
                    "All sampled pre-present Minecraft framebuffers are black: " + best);
            require(root, best.distinctRgb > 16 && best.lumaStddev > 1.0,
                    "RENDER_CONSTANT_FRAME", "render_probe",
                    "Pre-present Minecraft framebuffer lacks image variation: " + best);
            require(root, distinctHashes >= 2, "RENDER_FROZEN", "render_probe",
                    "Eight pre-present captures across a 90-degree camera turn produced one framebuffer hash");

            try {
                Files.copy(best.png, root.resolve("render/minecraft-framebuffer.png"),
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (IOException e) {
                throw fail(root, "ARTIFACT_STAGE_FAILED", "render_probe",
                        "Could not stage canonical pre-present framebuffer", e);
            }

            // L5 remains intentionally non-authoritative on hosted Apple VMs.
            // A WindowServer screenshot is useful diagnostics, but its failure
            // must never turn a valid L1-L4 run red.
            try {
                Path screenshot = context.takeScreenshot("mithril-hosted-world");
                writePresentation(root, screenshot, Files.isRegularFile(screenshot) ? "captured" : "missing");
            } catch (Throwable t) {
                writePresentation(root, null, "unavailable: " + t.getClass().getSimpleName());
                event(root, "presentation_diagnostic_unavailable", "presentation_probe", t.toString());
            }

            writeOracle(root, "l4_gpu_render", "pass");
            writeOracle(root, "l5_presentation", "diagnostic");
            event(root, "client_gametest_completed", "render_probe",
                    "L1-L4 passed using requested pre-present " + expectedBackend + " readback; L5 is diagnostic");
        }
    }

    private static FrameSample requestPrePresentCapture(ClientGameTestContext context, Path root, int frame) {
        Path render = root.resolve("render");
        Path request = render.resolve("prepresent-request.txt");
        Path requestTmp = render.resolve("prepresent-request.txt.tmp");
        Path raw = render.resolve("prepresent-frame-%04d.rgba".formatted(frame));
        Path meta = render.resolve("prepresent-frame-%04d.meta".formatted(frame));
        Path png = render.resolve("prepresent-frame-%04d.png".formatted(frame));
        try {
            Files.deleteIfExists(request);
            Files.deleteIfExists(requestTmp);
            Files.deleteIfExists(raw);
            Files.deleteIfExists(meta);
            Files.deleteIfExists(png);
            Files.writeString(requestTmp, Integer.toString(frame) + "\n", StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
            try {
                Files.move(requestTmp, request, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(requestTmp, request, StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException e) {
            throw fail(root, "RENDER_CAPTURE_REQUEST_FAILED", "render_probe",
                    "Could not create pre-present capture request " + frame, e);
        }

        for (int tick = 0; tick < CAPTURE_TIMEOUT_TICKS; tick++) {
            if (Files.isRegularFile(meta) && Files.isRegularFile(raw)) break;
            context.waitTicks(1);
        }
        require(root, Files.isRegularFile(meta) && Files.isRegularFile(raw),
                "RENDER_PREPRESENT_CAPTURE_TIMEOUT", "render_probe",
                "Bridge did not answer pre-present capture request " + frame
                        + " within " + CAPTURE_TIMEOUT_TICKS + " ticks");

        try {
            String[] parts = Files.readString(meta, StandardCharsets.UTF_8).trim().split("\\s+");
            if (parts.length != 3) throw new IOException("invalid metadata: " + String.join(" ", parts));
            int width = Integer.parseInt(parts[0]);
            int height = Integer.parseInt(parts[1]);
            long declaredBytes = Long.parseLong(parts[2]);
            byte[] bytes = Files.readAllBytes(raw);
            require(root, width > 1 && height > 1 && declaredBytes == (long) width * height * 4
                            && bytes.length == declaredBytes,
                    "RENDER_PREPRESENT_CAPTURE_INVALID", "render_probe",
                    "Invalid pre-present capture dimensions/size for frame " + frame + ": "
                            + width + "x" + height + " declared=" + declaredBytes + " actual=" + bytes.length);
            return analyzeFrame(root, frame, width, height, bytes, png);
        } catch (IOException | NumberFormatException e) {
            throw fail(root, "RENDER_PREPRESENT_CAPTURE_INVALID", "render_probe",
                    "Could not parse pre-present capture " + frame, e);
        }
    }

    private static FrameSample analyzeFrame(Path root, int frame, int width, int height,
                                            byte[] raw, Path png) {
        long nonBlack = 0;
        Set<Integer> colors = new HashSet<>();
        double sum = 0.0;
        double sum2 = 0.0;
        BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int i = (y * width + x) * 4;
                int r = raw[i] & 0xff;
                int g = raw[i + 1] & 0xff;
                int b = raw[i + 2] & 0xff;
                int a = raw[i + 3] & 0xff;
                if (r > 4 || g > 4 || b > 4) nonBlack++;
                colors.add((r << 16) | (g << 8) | b);
                double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                sum += luma;
                sum2 += luma * luma;
                image.setRGB(x, height - 1 - y, (a << 24) | (r << 16) | (g << 8) | b);
            }
        }
        long pixels = (long) width * height;
        double mean = sum / pixels;
        double variance = Math.max(0.0, sum2 / pixels - mean * mean);
        double stddev = Math.sqrt(variance);
        try {
            ImageIO.write(image, "png", png.toFile());
        } catch (IOException e) {
            throw fail(root, "ARTIFACT_PNG_FAILED", "render_probe",
                    "Could not write PNG for pre-present frame " + frame, e);
        }
        return new FrameSample(frame, width, height, nonBlack, colors.size(), mean, stddev,
                sha256(raw), png);
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
            resetPixelStore();
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, texture);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_NEAREST);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_NEAREST);
            GL11.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA8, w, h, 0,
                    GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, pattern);
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, fbo);
            GL30.glFramebufferTexture2D(GL30.GL_FRAMEBUFFER, GL30.GL_COLOR_ATTACHMENT0,
                    GL11.GL_TEXTURE_2D, texture, 0);
            if (GL30.glCheckFramebufferStatus(GL30.GL_FRAMEBUFFER) != GL30.GL_FRAMEBUFFER_COMPLETE) {
                return new ControlResult(false, w * h * 4, "framebuffer-incomplete");
            }
            ByteBuffer read = BufferUtils.createByteBuffer(w * h * 4);
            GL11.glReadPixels(0, 0, w, h, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, read);
            int mismatches = 0;
            for (int i = 0; i < w * h * 4; i++) if (read.get(i) != pattern.get(i)) mismatches++;
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

    private static void resetPixelStore() {
        GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 1);
        GL11.glPixelStorei(GL12.GL_PACK_ROW_LENGTH, 0);
        GL11.glPixelStorei(GL12.GL_PACK_SKIP_ROWS, 0);
        GL11.glPixelStorei(GL12.GL_PACK_SKIP_PIXELS, 0);
        GL11.glPixelStorei(GL12.GL_PACK_IMAGE_HEIGHT, 0);
        GL11.glPixelStorei(GL12.GL_PACK_SKIP_IMAGES, 0);
        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 1);
        GL11.glPixelStorei(GL12.GL_UNPACK_ROW_LENGTH, 0);
        GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_ROWS, 0);
        GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_PIXELS, 0);
        GL11.glPixelStorei(GL12.GL_UNPACK_IMAGE_HEIGHT, 0);
        GL11.glPixelStorei(GL12.GL_UNPACK_SKIP_IMAGES, 0);
    }

    private static void writeGameState(Path root, GameIdentity id, int chunkTicks) {
        write(root.resolve("game-state.json"), """
                {
                  "schema_version":"1.0",
                  "level_loaded":%s,
                  "player_loaded":%s,
                  "chunks_rendered":true,
                  "chunk_render_ticks":%d,
                  "framebuffer_width":%d,
                  "framebuffer_height":%d,
                  "gl_vendor":"%s",
                  "gl_renderer":"%s",
                  "gl_version":"%s"
                }
                """.formatted(id.levelLoaded, id.playerLoaded, chunkTicks, id.width, id.height,
                json(id.vendor), json(id.renderer), json(id.version)));
    }

    private static void writeControl(Path root, ControlResult result) {
        write(root.resolve("readback-control.json"), """
                {"schema_version":"1.0","exact":%s,"mismatch_bytes":%d,"kind":"%s"}
                """.formatted(result.exact, result.mismatchBytes, json(result.kind)));
    }

    private static void writeSamples(Path root, List<FrameSample> samples) {
        StringBuilder out = new StringBuilder("{\n  \"schema_version\":\"1.0\",\n  \"source\":\"pre-present-default-framebuffer\",\n  \"samples\":[\n");
        for (int i = 0; i < samples.size(); i++) {
            FrameSample s = samples.get(i);
            out.append("    {\"frame_id\":").append(s.frameId)
                    .append(",\"width\":").append(s.width)
                    .append(",\"height\":").append(s.height)
                    .append(",\"non_black_pixels\":").append(s.nonBlackPixels)
                    .append(",\"distinct_rgb\":").append(s.distinctRgb)
                    .append(",\"luma_mean\":").append(String.format(Locale.ROOT, "%.6f", s.lumaMean))
                    .append(",\"luma_stddev\":").append(String.format(Locale.ROOT, "%.6f", s.lumaStddev))
                    .append(",\"sha256\":\"").append(s.sha256).append("\"}");
            if (i + 1 < samples.size()) out.append(',');
            out.append('\n');
        }
        out.append("  ]\n}\n");
        write(root.resolve("render/samples.json"), out.toString());
    }

    private static void writePresentation(Path root, Path screenshot, String status) {
        write(root.resolve("presentation.json"),
                "{\"schema_version\":\"1.0\",\"status\":\"" + json(status)
                        + "\",\"screenshot\":\"" + json(screenshot == null ? "" : screenshot.toString()) + "\"}\n");
    }

    private static void writeOracle(Path root, String name, String status) {
        Path path = root.resolve("oracle-results.json");
        String prior = "";
        try { if (Files.exists(path)) prior = Files.readString(path); } catch (IOException ignored) {}
        write(path, prior + "{\"oracle\":\"" + json(name) + "\",\"status\":\"" + json(status) + "\"}\n");
    }

    private static void event(Path root, String event, String phase, String message) {
        String line = "{\"schema_version\":\"1.0\",\"timestamp\":\"" + Instant.now()
                + "\",\"producer\":\"mithril-client-gametest\",\"phase\":\"" + json(phase)
                + "\",\"event\":\"" + json(event) + "\",\"message\":\"" + json(message) + "\"}\n";
        try {
            Files.writeString(root.resolve("events.jsonl"), line, StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (IOException ignored) {}
    }

    private static void require(Path root, boolean condition, String failureId, String phase, String message) {
        if (!condition) throw fail(root, failureId, phase, message, null);
    }

    private static IllegalStateException fail(Path root, String failureId, String phase,
                                              String message, Throwable cause) {
        Path failure = root.resolve("failure.json");
        if (!Files.exists(failure)) {
            write(failure, "{\"schema_version\":\"1.0\",\"failure_id\":\"" + json(failureId)
                    + "\",\"phase\":\"" + json(phase) + "\",\"retryable\":false,\"message\":\""
                    + json(message) + "\"}\n");
        }
        event(root, "oracle_failed", phase, failureId + ": " + message);
        return cause == null ? new IllegalStateException(failureId + ": " + message)
                : new IllegalStateException(failureId + ": " + message, cause);
    }

    private static void write(Path path, String content) {
        try {
            if (path.getParent() != null) Files.createDirectories(path.getParent());
            Files.writeString(path, content, StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
        } catch (IOException e) {
            throw new IllegalStateException("Could not write E2E evidence " + path, e);
        }
    }

    private static String safe(String value) { return value == null ? "" : value; }

    private static String json(String value) {
        return safe(value).replace("\\", "\\\\").replace("\"", "\\\"")
                .replace("\n", "\\n").replace("\r", "\\r");
    }

    private static String sha256(byte[] bytes) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    private record GameIdentity(String vendor, String renderer, String version,
                                boolean levelLoaded, boolean playerLoaded, int width, int height) {}
    private record ControlResult(boolean exact, int mismatchBytes, String kind) {}
    private record FrameSample(int frameId, int width, int height, long nonBlackPixels,
                               int distinctRgb, double lumaMean, double lumaStddev,
                               String sha256, Path png) {
        double score() { return distinctRgb + lumaStddev + nonBlackPixels / 10000.0; }
    }
}
