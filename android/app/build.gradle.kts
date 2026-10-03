import com.launchers_plugin.renderer.buildscript.RendererConfig
import com.launchers_plugin.renderer.buildscript.buildEnvs
import com.launchers_plugin.renderer.buildscript.buildJsonValue
import com.launchers_plugin.renderer.buildscript.legacyManifest
import com.launchers_plugin.renderer.buildscript.nativePath
import com.launchers_plugin.renderer.buildscript.renderer

buildscript {
    repositories {
        maven("https://jitpack.io")
    }
    dependencies {
        classpath("com.github.ZalithLauncher.RendererPlugin-v2:dsl:1.0-alpha6")
    }
}

plugins {
    id("com.android.application")
}

apply(plugin = "com.launchers_plugin.renderer.dsl")

fun Project.mithrilAbiFilters(): List<String> =
    (findProperty("mithril.abis") ?: System.getenv("MITHRIL_ABIS") ?: "arm64-v8a")
        .toString().split(',').map { it.trim() }.filter { it.isNotEmpty() }

/**
 * The renderer descriptor the launcher reads. This is what makes the package
 * show up as a renderer option at all: a bare APK carrying only a .so is
 * invisible to it, because discovery goes through the fclPlugin_V2 meta-data
 * below, whose value is this JSON.
 */
val pluginRendererConfig = buildJsonValue {
    renderer(
        displayName = "Mithril-Wrapper",
        // Must start with "opengles". The launcher does not match on our own
        // name: FCL's pojavInitOpenGL() dispatches with
        //   if (!strncmp("opengles", renderer, 8)) { set_gl_bridge_tbl(); ... }
        // and only that branch assigns br_init. A rendererId it does not
        // recognise leaves br_init NULL, and the last line
        //   if (br_init()) br_setup_window();
        // then calls through a NULL function pointer - SIGSEGV at pc=0x0 in
        // pojavInitOpenGL (R21 = br_init+0x0), which is exactly what the
        // launcher log showed.
        //
        // MobileGL uses "opengles3" for the same reason: it is recognised by
        // that prefix check, not because anyone looks for "mobilegl".
        rendererId = "opengles3",
        rendererGLPath = nativePath("libmithril.so"),
        rendererEGLPath = nativePath("libmithril.so"),
        dlopenLibPaths = emptyList(),
        env = buildEnvs {
            // FCL's native GL bridge parses LIBGL_ES unconditionally when it
            // creates the EGL context. Keep this explicit just like MobileGL:
            // without it getenv("LIBGL_ES") can be null before strtol(), and
            // the requested EGL_CONTEXT_CLIENT_VERSION is undefined.
            normal("LIBGL_ES", "3")

            // Escape hatch for the GL level: Mithril advertises 4.6, but a
            // device that hits an unimplemented path can be dropped to 3.3
            // without rebuilding.
            selectable(
                key = "MITHRIL_GL_VERSION",
                title = RendererConfig.MetaString("mithril_gl_version_title"),
                items = RendererConfig.EnvItems("4.6", listOf("3.3")),
            )
            // Orientation: MoltenVK is Apple-only, so this is Android-only in
            // practice. On Android Mithril is the sole flipper, so the override
            // is offered in both directions.
            toggleable("MITHRIL_YFLIP", "1", false, RendererConfig.MetaString("mithril_yflip_title"))
            toggleable("MITHRIL_DEBUG", "1", false, RendererConfig.MetaString("mithril_debug_title"))
            customizable("MITHRIL_VRAM_BUDGET_MB", "1024", RendererConfig.MetaString("mithril_vram_budget_title"))
            // Driver selection.
            //
            // Many Snapdragon devices expose only Vulkan 1.1 through the stock
            // driver, while Turnip (libvulkan_freedreno.so) offers far more.
            // The Android loader cannot be redirected with VK_ICD_FILENAMES or
            // VK_DRIVER_FILES - it discovers drivers through hw_get_module
            // only - so Mithril dlopens the driver itself
            // (VulkanDispatchAndroid.cpp) and this is the switch for it.
            //
            // VK_ICD_FILENAMES / VK_DRIVER_FILES are deliberately NOT offered:
            // the Android loader ignores them entirely (it discovers drivers
            // through hw_get_module), so exposing them only invited invalid
            // values such as "1" that look like they do something.
            toggleable("MITHRIL_TURNIP", "1", false, RendererConfig.MetaString("mithril_turnip_title"))
            customizable("MITHRIL_VULKAN_LIBRARY", "", RendererConfig.MetaString("mithril_vulkan_library_title"))
        },
        minMCVer = null,
        maxMCVer = null,
    )
}

android {
    namespace = "com.mithril.wrapper"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.mithril.wrapper"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        resValue("string", "config", pluginRendererConfig)

        // Legacy (FCL / Pojav-style) discovery. The V2 meta-data above covers
        // modern launchers; these placeholders keep the older path working too.
        manifestPlaceholders.putAll(legacyManifest {
            displayName = "Mithril-Wrapper"
            rendererName = "Mithril-Wrapper"
            rendererLib = "libmithril.so"
            eglLib = "/libmithril.so"
            minMCVer = ""
            maxMCVer = ""
            boatEnv {
                put("POJAV_RENDERER", "opengles3")
                put("LIBGL_ES", "3")
            }
            pojavEnv {
                put("POJAV_RENDERER", "opengles3")
                put("LIBGL_ES", "3")
            }
        })
        manifestPlaceholders["appLabel"] = "Mithril-Wrapper"

        ndk {
            abiFilters += mithrilAbiFilters()
        }
    }

    buildFeatures {
        resValues = true
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
        }
    }

    packaging {
        jniLibs {
            // Must be TRUE. With false (extractNativeLibs=false) the .so stays
            // inside base.apk and is never written to
            // /data/app/<pkg>/lib/<abi>/, which is exactly the path the launcher
            // dlopens. Run log:
            //   DLOPEN: loading /data/app/.../com.mithril.wrapper-.../lib/arm64//libmithril.so
            //     (error = dlopen failed: library "..." not found)
            // dlopen then returns NULL, the launcher resolves GL/EGL entrypoints
            // from it, calls through a NULL pointer and dies with
            // SIGSEGV at pc=0x0 inside libpojavexec.so.
            //
            // MobileGL's plugin sets the same value, for the same reason: the
            // renderer is consumed as an extracted native library, not read out
            // of the APK.
            useLegacyPackaging = true
        }
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}

dependencies {}
