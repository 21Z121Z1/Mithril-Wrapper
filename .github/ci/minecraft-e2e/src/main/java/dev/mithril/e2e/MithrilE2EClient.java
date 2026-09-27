package dev.mithril.e2e;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.world.Difficulty;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.presets.WorldPresets;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Minecraft 1.21.1 gameplay E2E probe.
 *
 * This is deliberately stronger than a title-screen smoke test: after the
 * production client has reached a stable title state it creates a deterministic
 * single-player world through the same WorldOpenFlows path used by vanilla,
 * waits until both the client level and local player exist, lets gameplay render
 * for a settling interval, and only then publishes world-ready.json.
 */
public final class MithrilE2EClient implements ClientModInitializer {
    private static final int TITLE_SETTLE_TICKS = 120;
    private static final int WORLD_SETTLE_TICKS = 120;
    private static final String WORLD_ID = "mithril-e2e-world";
    private static final long WATCHDOG_DEADLINE_MS = 420_000L;

    private int ticks;
    private int worldTicks;
    private boolean worldRequested;
    private boolean readyWritten;

    @Override
    public void onInitializeClient() {
        final Path root = Path.of(System.getProperty("mithril.e2e.root",
                System.getenv().getOrDefault("MITHRIL_E2E_ROOT", "build/evidence")))
                .toAbsolutePath().normalize();

        Thread watchdog = new Thread(() -> {
            try {
                Thread.sleep(WATCHDOG_DEADLINE_MS);
            } catch (InterruptedException ignored) {
                return;
            }
            if (readyWritten) return;
            try {
                Files.createDirectories(root);
                Map<String, Object> failure = new LinkedHashMap<>();
                failure.put("schema_version", "1.0");
                failure.put("reason", "world-ready deadline exceeded");
                failure.put("ticks_observed", ticks);
                failure.put("world_requested", worldRequested);
                writeJson(root.resolve("world-entry-failure.json"), failure);
            } catch (Throwable t) {
                t.printStackTrace();
            }
        }, "mithril-e2e-world-watchdog");
        watchdog.setDaemon(true);
        watchdog.start();

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (readyWritten) return;
            ticks++;

            if (!worldRequested && ticks >= TITLE_SETTLE_TICKS
                    && client.level == null && client.player == null) {
                worldRequested = true;
                System.out.println("[mithril-e2e] requesting deterministic single-player world");
                try {
                    LevelSettings settings = new LevelSettings(
                            "Mithril E2E",
                            GameType.CREATIVE,
                            false,
                            Difficulty.PEACEFUL,
                            true,
                            new GameRules(),
                            WorldDataConfiguration.DEFAULT);
                    WorldOptions options = new WorldOptions(0x4D49544852494CL, false, false);
                    client.createWorldOpenFlows().createFreshLevel(
                            WORLD_ID,
                            settings,
                            options,
                            WorldPresets::createNormalWorldDimensions,
                            client.screen);
                } catch (Throwable t) {
                    t.printStackTrace();
                    try {
                        Files.createDirectories(root);
                        Map<String, Object> failure = new LinkedHashMap<>();
                        failure.put("schema_version", "1.0");
                        failure.put("reason", "createFreshLevel threw");
                        failure.put("exception", t.toString());
                        writeJson(root.resolve("world-entry-failure.json"), failure);
                    } catch (Throwable nested) {
                        nested.printStackTrace();
                    }
                }
                return;
            }

            if (worldRequested && client.level != null && client.player != null
                    && client.screen == null) {
                worldTicks++;
                if (worldTicks < WORLD_SETTLE_TICKS) return;

                try {
                    Files.createDirectories(root);
                    Map<String, Object> ready = new LinkedHashMap<>();
                    ready.put("schema_version", "1.0");
                    ready.put("world_id", WORLD_ID);
                    ready.put("client_tick", ticks);
                    ready.put("world_settle_ticks", worldTicks);
                    ready.put("dimension", client.level.dimension().location().toString());
                    ready.put("player_x", client.player.getX());
                    ready.put("player_y", client.player.getY());
                    ready.put("player_z", client.player.getZ());
                    ready.put("screen", "none");
                    writeJson(root.resolve("world-ready.json"), ready);
                    readyWritten = true;
                    System.out.println("[mithril-e2e] world-ready: level+player live for "
                            + worldTicks + " ticks");
                } catch (Throwable t) {
                    t.printStackTrace();
                }
            } else if (worldRequested) {
                worldTicks = 0;
            }
        });
    }

    private static void writeJson(Path path, Map<String, Object> values) throws Exception {
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
                b.append('\"').append(escape(String.valueOf(v))).append('\"');
            }
            if (++i < values.size()) b.append(',');
            b.append('\n');
        }
        b.append("}\n");
        Files.writeString(path, b.toString(), StandardCharsets.UTF_8);
    }

    private static String escape(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"");
    }
}
