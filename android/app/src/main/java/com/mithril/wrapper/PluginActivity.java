package com.mithril.wrapper;

import android.app.Activity;
import android.os.Bundle;

/**
 * No-display entry point the launcher uses to query the plugin. There is
 * nothing to show, so it finishes immediately - same as MobileGL's
 * PluginActivity, which exists purely so the launcher has something to bind.
 */
public final class PluginActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        finish();
    }
}
