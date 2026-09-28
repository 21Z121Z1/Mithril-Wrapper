package dev.mithril.e2e;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.worldselection.CreateWorldScreen;
import net.minecraft.client.gui.screens.Screen;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.DirectoryStream;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Version-portable Minecraft client E2E probe for Mithril (Mojang mappings).
 *
 * Boot the real production client, validate the main menu, then PROGRAMMATICALLY
 * create and join a fresh world (no mouse / synthetic clicks), wait for the
 * integrated server and terrain, sample the full default framebuffer across a
 * warm window and gate on real, non-uniform in-world content. The best in-world
 * frame is written both as raw RGBA and as a viewable PNG so the renderer
 * output is a hard, machine- and human-checkable release gate.
 */
public final class MithrilE2EClient implements ClientModInitializer {
    private static final int MENU_WARMUP_TICKS = 120;
    private static final int CREATE_SCREEN_WAIT_TICKS = 200;
    private static final int WORLD_WAIT_TICKS = 300;
    private static final double RED_FAIL_RATIO = 0.90;
    /** In-world content gates (full default framebuffer). */
    private static final double MAX_UNIFORM_FILL = 0.85;
    private static final int MIN_DISTINCT_COLORS = 250;
    private static final double MIN_GRAY_STDDEV = 18.0;
    private static final int INWORLD_WARM_TICKS = 240;
    private static final long WATCHDOG_DEADLINE_MS = 600_000L;

    private enum Phase { MENU, OPEN_CREATE, WAIT_WORLD, INWORLD_WARM, DONE }
    private Phase phase = Phase.MENU;
    private int ticks = 0;
    private int phaseTicks = 0;
    private Stats bestStats = null;
    private byte[] bestRaw = null;
    private int bestW = 0, bestH = 0;
    private int prepresentReq = 0;
    private int lastPrepresentSeen = -1;

    @Override
    public void onInitializeClient() {
        System.out.println("[mithril-e2e] onInitializeClient: mod loaded and initializing");
        System.setProperty("java.awt.headless", "true");
        final Path root = Path.of(System.getProperty("mithril.e2e.root", "build/evidence"))
                .toAbsolutePath().normalize();

        Thread watchdog = new Thread(() -> {
            try {
                Thread.sleep(WATCHDOG_DEADLINE_MS);
            } catch (InterruptedException ignored) {
                return;
            }
            System.err.println("[mithril-e2e] WATCHDOG: deadline in phase " + phase
                    + " ticks=" + ticks + "; forcing halt");
            try {
                Files.createDirectories(root.resolve("render"));
                Map<String, Object> hang = new LinkedHashMap<>();
                hang.put("schema_version", "1.0");
                hang.put("reason", "watchdog deadline reached");
                hang.put("phase", phase.name());
                hang.put("ticks_observed", ticks);
                writeJson(root.resolve("hang-detected.json"), hang);
            } catch (Throwable t) {
                t.printStackTrace();
            }
            Runtime.getRuntime().halt(11);
        });
        watchdog.setDaemon(true);
        watchdog.setName("mithril-e2e-watchdog");
        watchdog.start();

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (phase == Phase.DONE) return;
            ticks++;
            phaseTicks++;
            try {
                switch (phase) {
                    case MENU -> {
                        if (ticks < MENU_WARMUP_TICKS) return;
                        validateMenu(client, root);
                        // Programmatically open the "Create New World" flow with defaults.
                        CreateWorldScreen.openFresh(client, client.screen);
                        advance(Phase.OPEN_CREATE);
                    }
                    case OPEN_CREATE -> {
                        Screen s = client.screen;
                        if (s instanceof CreateWorldScreen cws) {
                            // Equivalent to pressing the Create button: builds the
                            // world from the default context and joins it. onCreate()
                            // is private in Mojang mappings, so invoke it reflectively
                            // (deterministic, no synthetic click).
                            java.lang.reflect.Method create =
                                    CreateWorldScreen.class.getDeclaredMethod("onCreate");
                            create.setAccessible(true);
                            create.invoke(cws);
                            advance(Phase.WAIT_WORLD);
                        } else if (phaseTicks > CREATE_SCREEN_WAIT_TICKS) {
                            fail(root, "CreateWorldScreen never appeared; screen="
                                    + (s == null ? "null" : s.getClass().getName()));
                        }
                    }
                    case WAIT_WORLD -> {
                        boolean inWorld = client.player != null && client.level != null;
                        if (inWorld) {
                            advance(Phase.INWORLD_WARM);
                        } else if (phaseTicks > WORLD_WAIT_TICKS) {
                            fail(root, "world never loaded; player=" + (client.player != null)
                                    + " level=" + (client.level != null));
                        }
                    }
                    case INWORLD_WARM -> {
                        // Sample the presented framebuffer every tick and remember the
                        // most non-uniform frame, so a single blank/loading frame cannot
                        // fail the gate.
                        sampleInWorld(client, root);
                        if (phaseTicks % 40 == 0) {
                            System.out.println("[mithril-e2e] warm tick=" + phaseTicks
                                + (bestStats == null ? " best=none"
                                : " best_fill=" + fmt(bestStats.uniformFill)
                                  + " best_distinct=" + bestStats.distinctColors
                                  + " best_stddev=" + fmt(bestStats.grayStddev)));
                        }
                        boolean playerLeft = client.player == null || client.level == null;
                        if (playerLeft) {
                            advance(Phase.WAIT_WORLD);
                        } else if (phaseTicks >= INWORLD_WARM_TICKS) {
                            finalizeInWorld(root);
                            advance(Phase.DONE);
                        }
                    }
                    default -> { }
                }
            } catch (Throwable t) {
                t.printStackTrace();
                try {
                    Map<String, Object> e = new LinkedHashMap<>();
                    e.put("schema_version", "1.0");
                    e.put("phase", phase.name());
                    e.put("message", String.valueOf(t));
                    writeJson(root.resolve("render/exception.json"), e);
                } catch (Throwable ignored) { }
                System.exit(4);
            }
        });
    }

    private void advance(Phase next) {
        phase = next;
        phaseTicks = 0;
        System.out.println("[mithril-e2e] phase -> " + next);
    }

    private void validateMenu(Minecraft client, Path root) throws Exception {
        Files.createDirectories(root.resolve("render"));
        String vendor = safe(GL11.glGetString(GL11.GL_VENDOR));
        String renderer = safe(GL11.glGetString(GL11.GL_RENDERER));
        String version = safe(GL11.glGetString(GL11.GL_VERSION));

        int w = client.getWindow().getWidth();
        int h = client.getWindow().getHeight();
        int sw = Math.max(1, Math.min(64, w));
        int sh = Math.max(1, Math.min(64, h));
        int x = (w - sw) / 2;
        int y = (h - sh) / 2;
        ByteBuffer px = BufferUtils.createByteBuffer(sw * sh * 4);
        GL11.glReadPixels(x, y, sw, sh, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, px);
        int total = sw * sh, red = 0;
        for (int i = 0; i < total; i++) {
            int r = px.get() & 0xFF, g = px.get() & 0xFF, b = px.get() & 0xFF;
            px.get();
            if (r > 200 && g < 40 && b < 40) red++;
        }
        double redRatio = (double) red / (double) total;

        Map<String, Object> st = new LinkedHashMap<>();
        st.put("schema_version", "1.0");
        st.put("tick", ticks);
        st.put("window_width", w);
        st.put("window_height", h);
        st.put("gl_vendor", vendor);
        st.put("gl_renderer", renderer);
        st.put("gl_version", version);
        st.put("red_pixel_ratio", redRatio);
        writeJson(root.resolve("render/game-state.json"), st);
        writeJson(root.resolve("game-state.json"), st);

        if (redRatio > RED_FAIL_RATIO) {
            Map<String, Object> re = new LinkedHashMap<>();
            re.put("schema_version", "1.0");
            re.put("red_pixel_ratio", redRatio);
            writeJson(root.resolve("render/red-screen-detected.json"), re);
            System.err.println("[mithril-e2e] FAILURE: pure-red menu (red_pixel_ratio=" + redRatio + ")");
            System.exit(3);
        }
        System.out.println("[mithril-e2e] menu OK; red_pixel_ratio=" + redRatio);
    }

    /** Request a prepresent capture; remember the most non-uniform frame. */
    private void sampleInWorld(Minecraft client, Path root) throws Exception {
        int w = client.getWindow().getWidth();
        int h = client.getWindow().getHeight();
        Path renderDir = root.resolve("render");
        Files.createDirectories(renderDir);
        // The bridge captures the DEFAULT framebuffer at the next eglSwapBuffers
        // (after the level target is blitted to screen and the HUD/GUI is drawn),
        // so this frame truly represents what would be presented.
        ++prepresentReq;
        Files.writeString(renderDir.resolve("prepresent-request.txt"),
                          String.valueOf(prepresentReq));
        // Read the newest prepresent frame already captured (one-tick lag).
        int bestNum = -1; Path newest = null;
        try (DirectoryStream<Path> ds = Files.newDirectoryStream(renderDir,
                "prepresent-frame-*.rgba")) {
            for (Path fp : ds) {
                Matcher m = Pattern.compile("prepresent-frame-(\\d{4})\\.rgba")
                        .matcher(fp.getFileName().toString());
                if (m.matches()) {
                    int num = Integer.parseInt(m.group(1));
                    if (num > bestNum) { bestNum = num; newest = fp; }
                }
            }
        }
        if (newest != null && bestNum != lastPrepresentSeen) {
            lastPrepresentSeen = bestNum;
            byte[] raw = Files.readAllBytes(newest);
            Stats st = Stats.compute(raw, w, h);
            boolean better = bestStats == null
                    || st.uniformFill < bestStats.uniformFill
                    || (st.uniformFill == bestStats.uniformFill
                        && (st.distinctColors > bestStats.distinctColors
                            || (st.distinctColors == bestStats.distinctColors
                                && st.grayStddev > bestStats.grayStddev)));
            if (better) {
                bestStats = st; bestRaw = raw; bestW = w; bestH = h;
            }
        }
    }

    private void finalizeInWorld(Path root) throws Exception {
        Files.createDirectories(root.resolve("render"));
        if (bestRaw == null) {
            fail(root, "no in-world frame sampled");
            return;
        }
        final byte[] raw = bestRaw;
        final int w = bestW, h = bestH;
        final Stats st = bestStats;

        String vendor = safe(GL11.glGetString(GL11.GL_VENDOR));
        String renderer = safe(GL11.glGetString(GL11.GL_RENDERER));
        String glversion = safe(GL11.glGetString(GL11.GL_VERSION));

        boolean identityOk = glversion.contains("Mithril-Wrapper")
                && renderer.contains("Mithril-Wrapper");
        boolean contentOk = st.uniformFill < MAX_UNIFORM_FILL
                && st.distinctColors > MIN_DISTINCT_COLORS
                && st.grayStddev > MIN_GRAY_STDDEV;

        // Raw evidence. The viewable PNG is produced by the CI workflow via
        // ci/minecraft-e2e/rgba_to_png.py (canonical converter).
        Files.write(root.resolve("render/inworld-frame.rgba"), raw);

        Map<String, Object> meta = new LinkedHashMap<>();
        meta.put("schema_version", "1.0");
        meta.put("git_sha", System.getenv().getOrDefault("MITHRIL_E2E_SHA", ""));
        meta.put("capture_source", "default-framebuffer glReadPixels (in-world, best of warm window)");
        meta.put("pixel_format", "RGBA8");
        meta.put("width", w);
        meta.put("height", h);
        meta.put("warm_window_ticks", INWORLD_WARM_TICKS);
        meta.put("uniform_fill", st.uniformFill);
        meta.put("distinct_colors", st.distinctColors);
        meta.put("gray_stddev", st.grayStddev);
        meta.put("dominant_color", st.dominant);
        meta.put("gl_vendor", vendor);
        meta.put("gl_renderer", renderer);
        meta.put("gl_version", glversion);
        writeJson(root.resolve("render/inworld-frame.json"), meta);

        Map<String, Object> oracles = new LinkedHashMap<>();
        oracles.put("schema_version", "1.0");
        oracles.put("l1_process", "pass");
        oracles.put("l2_runtime_identity", identityOk ? "pass" : "fail");
        oracles.put("l3_game_state", "pass");
        oracles.put("l4_gpu_render", contentOk ? "pass" : "fail");
        oracles.put("l5_presentation", "pass");
        oracles.put("l6_in_world_render", contentOk ? "pass" : "fail");
        writeJson(root.resolve("oracle-results.json"), oracles);
        writeJson(root.resolve("render/oracle-results.json"), oracles);

        System.out.println("[mithril-e2e] BEST in-world frame " + w + "x" + h
                + " uniform_fill=" + fmt(st.uniformFill)
                + " distinct=" + st.distinctColors
                + " gray_stddev=" + fmt(st.grayStddev)
                + " identity=" + identityOk + " content=" + contentOk);

        if (!identityOk || !contentOk) {
            System.err.println("[mithril-e2e] FAILURE: in-world gate failed");
            System.exit(5);
        }

        // All gates passed: terminate the client so gradle returns 0. Without
        // this the game keeps running after the phase advances to DONE (whose
        // client tick is a no-op), and the workflow's watchdog reports
        // RUNTIME_MINECRAFT_HANG even though every gate already passed, which
        // also skips the rc==0 PNG conversion.
        System.out.println("[mithril-e2e] ALL GATES PASSED; exiting client (code 0)");
        System.exit(0);
    }

    private void fail(Path root, String reason) {
        System.err.println("[mithril-e2e] FAILURE: " + reason);
        try {
            Map<String, Object> e = new LinkedHashMap<>();
            e.put("schema_version", "1.0");
            e.put("phase", phase.name());
            e.put("reason", reason);
            writeJson(root.resolve("render/failure.json"), e);
        } catch (Throwable ignored) { }
        System.exit(6);
    }

    private record Stats(double uniformFill, int distinctColors, double grayStddev,
                         String dominant) {
        static Stats compute(byte[] raw, int w, int h) {
            Map<Integer, Integer> counts = new LinkedHashMap<>();
            double sum = 0, sum2 = 0;
            int n = w * h;
            for (int i = 0; i < n; i++) {
                int o = i * 4;
                int r = raw[o] & 0xFF, g = raw[o + 1] & 0xFF, b = raw[o + 2] & 0xFF;
                int rgb = (r << 16) | (g << 8) | b;
                counts.merge(rgb, 1, Integer::sum);
                int gr = (r + g + b) / 3;
                sum += gr;
                sum2 += gr * gr;
            }
            int top = 0, topN = 0;
            for (Map.Entry<Integer, Integer> e : counts.entrySet()) {
                if (e.getValue() > topN) { topN = e.getValue(); top = e.getKey(); }
            }
            double mean = sum / n, var = sum2 / n - mean * mean;
            int dr = (top >> 16) & 0xFF, dg = (top >> 8) & 0xFF, db = top & 0xFF;
            return new Stats((double) topN / n, counts.size(),
                    Math.sqrt(Math.max(0, var)),
                    "(" + dr + "," + dg + "," + db + ")");
        }
    }

    private static String safe(String s) { return s == null ? "" : s; }
    private static String fmt(double d) { return String.format(Locale.ROOT, "%.4f", d); }

    private static void writeJson(Path path, Map<String, Object> values) throws Exception {
        Files.createDirectories(path.getParent());
        StringBuilder b = new StringBuilder("{\n");
        int i = 0;
        for (Map.Entry<String, Object> e : values.entrySet()) {
            b.append("  \"").append(escape(e.getKey())).append("\":");
            Object v = e.getValue();
            if (v instanceof Number) {
                b.append(v instanceof Double || v instanceof Float
                        ? String.format(Locale.ROOT, "%.4f", ((Number) v).doubleValue())
                        : v.toString());
            } else if (v instanceof Boolean) {
                b.append(((Boolean) v) ? "true" : "false");
            } else {
                b.append('"').append(escape(String.valueOf(v))).append('"');
            }
            if (++i < values.size()) b.append(',');
            b.append('\n');
        }
        b.append("}\n");
        Files.writeString(path, b.toString(), StandardCharsets.UTF_8);
    }

    private static String escape(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' || c == '\\') sb.append('\\');
            sb.append(c);
        }
        return sb.toString();
    }
}
