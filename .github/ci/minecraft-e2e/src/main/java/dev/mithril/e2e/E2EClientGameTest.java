package dev.mithril.e2e;

import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;

@SuppressWarnings("UnstableApiUsage")
public final class E2EClientGameTest implements FabricClientGameTest {
    @Override
    public void runTest(ClientGameTestContext context) {
        if (Boolean.getBoolean("mithril.e2e.differential")) {
            new DifferentialClientGameTest().runTest(context);
        } else {
            new MithrilPrePresentClientGameTest().runTest(context);
        }
    }
}
