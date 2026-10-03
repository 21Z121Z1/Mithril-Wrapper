package com.mithril.wrapper;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

/**
 * Launcher-visible shell for the plugin. MobileGL ships an 800-line benchmark
 * POST UI here; this one only identifies the installed renderer and its ABI,
 * which is all this package needs to show.
 */
public final class PostActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView tv = new TextView(this);
        tv.setText("Mithril-Wrapper\n" + abiLine());
        tv.setPadding(48, 48, 48, 48);
        setContentView(tv);
    }

    private static String abiLine() {
        String[] abis = android.os.Build.SUPPORTED_ABIS;
        return abis == null || abis.length == 0 ? "abi: unknown" : "abi: " + abis[0];
    }
}
