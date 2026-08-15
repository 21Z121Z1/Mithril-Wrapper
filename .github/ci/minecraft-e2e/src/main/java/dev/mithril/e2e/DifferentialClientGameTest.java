package dev.mithril.e2e;

import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;
import net.fabricmc.loader.api.FabricLoader;
import org.lwjgl.opengl.GL11;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Locale;

@SuppressWarnings("UnstableApiUsage")
final class DifferentialClientGameTest implements FabricClientGameTest {
    private static final int SAMPLE_COUNT = 8;
    private static final int CAPTURE_TIMEOUT_TICKS = 80;

    @Override
    public void runTest(ClientGameTestContext context) {
        String backend = System.getProperty("mithril.e2e.backend", "mithril");
        boolean expectSodium = Boolean.parseBoolean(System.getProperty("mithril.e2e.expectSodium", "false"));
        Path root = Path.of(System.getProperty("mithril.e2e.root", "build/differential"))
                .toAbsolutePath().normalize();
        try {
            Files.createDirectories(root.resolve("render"));
        } catch (IOException e) {
            throw new IllegalStateException("Could not create differential evidence root " + root, e);
        }

        event(root, "differential_gametest_started",
                "backend=" + backend + " sodium=" + expectSodium);

        try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
            int chunkRenderTicks = singleplayer.getClientLevel().waitForChunksRender();
            context.waitTicks(40);

            // Fix camera orientation explicitly so native and wrapper lanes do not
            // inherit any spawn-facing or previous-run state. The Fabric test world
            // itself is deterministic (flat preset, seed 1, fixed time/weather).
            context.computeOnClient(client -> {
                if (client.player != null) {
                    client.player.setYRot(0.0f);
                    client.player.setXRot(0.0f);
                }
                return true;
            });
            context.waitTicks(12);

            Identity identity = context.computeOnClient(client -> new Identity(
                    safe(GL11.glGetString(GL11.GL_VENDOR)),
                    safe(GL11.glGetString(GL11.GL_RENDERER)),
                    safe(GL11.glGetString(GL11.GL_VERSION)),
                    client.level != null,
                    client.player != null,
                    client.getWindow().getWidth(),
                    client.getWindow().getHeight()));

            boolean sodiumLoaded = FabricLoader.getInstance().isModLoaded("sodium");
            String sodiumVersion = FabricLoader.getInstance().getModContainer("sodium")
                    .map(c -> c.getMetadata().getVersion().getFriendlyString()).orElse("");

            require(identity.levelLoaded && identity.playerLoaded,
                    "Minecraft differential lane did not enter a real singleplayer world");
            require(identity.width > 1 && identity.height > 1,
                    "Invalid framebuffer size " + identity.width + "x" + identity.height);
            require(sodiumLoaded == expectSodium,
                    "Sodium load mismatch: expected=" + expectSodium + " loaded=" + sodiumLoaded
                            + " version=" + sodiumVersion);
            if (backend.equals("mithril")) {
                require(identity.version.contains("Mithril-Wrapper") && identity.version.contains("DirectMetal")
                                && identity.renderer.contains("Mithril-Wrapper") && identity.renderer.contains("DirectMetal"),
                        "Mithril lane is not running DirectMetal: " + identity);
            } else if (backend.equals("native")) {
                require(!identity.version.contains("Mithril-Wrapper") && !identity.renderer.contains("Mithril-Wrapper")
                                && !identity.version.isBlank() && !identity.renderer.isBlank(),
                        "Native lane unexpectedly resolved Mithril or has no native GL identity: " + identity);
            } else {
                throw new IllegalStateException("Unknown differential backend " + backend);
            }

            writeState(root, backend, expectSodium, sodiumLoaded, sodiumVersion, identity, chunkRenderTicks);

            List<FrameSample> samples = new ArrayList<>();
            for (int i = 0; i < SAMPLE_COUNT; i++) {
                if (i == SAMPLE_COUNT / 2) {
                    context.computeOnClient(client -> {
                        if (client.player != null) client.player.setYRot(90.0f);
                        return true;
                    });
                    context.waitTicks(12);
                }
                samples.add(requestCapture(context, root, i + 1));
                context.waitTicks(3);
            }
            writeSamples(root, samples);
            Files.copy(samples.getFirst().png, root.resolve("render/minecraft-framebuffer.png"),
                    StandardCopyOption.REPLACE_EXISTING);
            require(samples.stream().map(FrameSample::sha256).distinct().count() >= 2,
                    "Differential capture did not change across camera turn");
            event(root, "differential_gametest_completed",
                    "captured=" + samples.size() + " backend=" + backend + " sodium=" + sodiumLoaded);
        } catch (IOException e) {
            throw new IllegalStateException("Could not finalize differential evidence", e);
        }
    }

    private static FrameSample requestCapture(ClientGameTestContext context, Path root, int frame) {
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
            Files.writeString(requestTmp, frame + "\n", StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
            try {
                Files.move(requestTmp, request, StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(requestTmp, request, StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException e) {
            throw new IllegalStateException("Could not request differential capture " + frame, e);
        }

        for (int tick = 0; tick < CAPTURE_TIMEOUT_TICKS; tick++) {
            if (Files.isRegularFile(meta) && Files.isRegularFile(raw)) break;
            context.waitTicks(1);
        }
        require(Files.isRegularFile(meta) && Files.isRegularFile(raw),
                "Capture request " + frame + " timed out");

        try {
            String[] parts = Files.readString(meta, StandardCharsets.UTF_8).trim().split("\\s+");
            require(parts.length == 3, "Invalid capture metadata for frame " + frame);
            int width = Integer.parseInt(parts[0]);
            int height = Integer.parseInt(parts[1]);
            long declaredBytes = Long.parseLong(parts[2]);
            byte[] bytes = Files.readAllBytes(raw);
            require(width > 1 && height > 1 && declaredBytes == (long) width * height * 4
                            && bytes.length == declaredBytes,
                    "Invalid differential framebuffer payload for frame " + frame);
            return analyze(frame, width, height, bytes, png);
        } catch (IOException | NumberFormatException e) {
            throw new IllegalStateException("Could not parse differential capture " + frame, e);
        }
    }

    private static FrameSample analyze(int frame, int width, int height, byte[] raw, Path png) {
        BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
        long nonBlack = 0;
        double sum = 0.0;
        double sum2 = 0.0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int i = (y * width + x) * 4;
                int r = raw[i] & 0xff;
                int g = raw[i + 1] & 0xff;
                int b = raw[i + 2] & 0xff;
                int a = raw[i + 3] & 0xff;
                if (r > 4 || g > 4 || b > 4) nonBlack++;
                double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                sum += luma;
                sum2 += luma * luma;
                // GL readback row 0 is the lower-left row. PNG/ImageIO row 0 is
                // top-left, so exactly one Y inversion is required here.
                image.setRGB(x, height - 1 - y, (a << 24) | (r << 16) | (g << 8) | b);
            }
        }
        require(nonBlack > 0, "Captured differential frame is black: " + frame);
        try {
            ImageIO.write(image, "png", png.toFile());
            BufferedImage persisted = ImageIO.read(png.toFile());
            require(persisted != null, "ImageIO could not reopen differential PNG " + frame);
            require((persisted.getRGB(0, height - 1) & 0xffffffffL) == rgbaAt(raw, 0),
                    "raw bottom-left did not map to PNG bottom-left for frame " + frame);
            require((persisted.getRGB(0, 0) & 0xffffffffL) == rgbaAt(raw, (height - 1) * width * 4),
                    "raw top-left did not map to PNG top-left for frame " + frame);
        } catch (IOException e) {
            throw new IllegalStateException("Could not write/reopen differential PNG " + frame, e);
        }
        long pixels = (long) width * height;
        double mean = sum / pixels;
        double variance = Math.max(0.0, sum2 / pixels - mean * mean);
        return new FrameSample(frame, width, height, nonBlack, mean, Math.sqrt(variance), sha256(raw), png);
    }

    private static long rgbaAt(byte[] raw, int i) {
        long r = raw[i] & 0xffL;
        long g = raw[i + 1] & 0xffL;
        long b = raw[i + 2] & 0xffL;
        long a = raw[i + 3] & 0xffL;
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    private static void writeState(Path root, String backend, boolean expectSodium,
                                   boolean sodiumLoaded, String sodiumVersion,
                                   Identity identity, int chunkRenderTicks) {
        String json = "{\n"
                + "  \"schema_version\": \"1.0\",\n"
                + "  \"backend\": \"" + json(backend) + "\",\n"
                + "  \"expect_sodium\": " + expectSodium + ",\n"
                + "  \"sodium_loaded\": " + sodiumLoaded + ",\n"
                + "  \"sodium_version\": \"" + json(sodiumVersion) + "\",\n"
                + "  \"gl_vendor\": \"" + json(identity.vendor) + "\",\n"
                + "  \"gl_renderer\": \"" + json(identity.renderer) + "\",\n"
                + "  \"gl_version\": \"" + json(identity.version) + "\",\n"
                + "  \"width\": " + identity.width + ",\n"
                + "  \"height\": " + identity.height + ",\n"
                + "  \"chunk_render_ticks\": " + chunkRenderTicks + "\n"
                + "}\n";
        write(root.resolve("differential-state.json"), json);
    }

    private static void writeSamples(Path root, List<FrameSample> samples) {
        StringBuilder b = new StringBuilder("[\n");
        for (int i = 0; i < samples.size(); i++) {
            FrameSample s = samples.get(i);
            if (i > 0) b.append(",\n");
            b.append("  {\"frame\":").append(s.frame)
                    .append(",\"width\":").append(s.width)
                    .append(",\"height\":").append(s.height)
                    .append(",\"non_black\":").append(s.nonBlack)
                    .append(",\"luma_mean\":").append(String.format(Locale.ROOT, "%.6f", s.lumaMean))
                    .append(",\"luma_stddev\":").append(String.format(Locale.ROOT, "%.6f", s.lumaStddev))
                    .append(",\"sha256\":\"").append(s.sha256).append("\"}");
        }
        b.append("\n]\n");
        write(root.resolve("render/samples.json"), b.toString());
    }

    private static void event(Path root, String event, String message) {
        String line = "{\"schema_version\":\"1.0\",\"ts\":\"" + Instant.now()
                + "\",\"event\":\"" + json(event) + "\",\"message\":\""
                + json(message) + "\"}\n";
        try {
            Files.writeString(root.resolve("events.jsonl"), line, StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (IOException e) {
            throw new IllegalStateException("Could not write differential event", e);
        }
    }

    private static void write(Path path, String text) {
        try {
            Files.createDirectories(path.getParent());
            Files.writeString(path, text, StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
        } catch (IOException e) {
            throw new IllegalStateException("Could not write " + path, e);
        }
    }

    private static String sha256(byte[] bytes) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    private static String safe(String s) { return s == null ? "" : s; }
    private static String json(String s) {
        return safe(s).replace("\\", "\\\\").replace("\"", "\\\"")
                .replace("\n", "\\n").replace("\r", "\\r");
    }
    private static void require(boolean condition, String message) {
        if (!condition) throw new IllegalStateException(message);
    }

    private record Identity(String vendor, String renderer, String version,
                            boolean levelLoaded, boolean playerLoaded, int width, int height) {}
    private record FrameSample(int frame, int width, int height, long nonBlack,
                               double lumaMean, double lumaStddev, String sha256, Path png) {}
}
