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
        rendererId = "mithril",
        rendererGLPath = nativePath("libmithril.so"),
        rendererEGLPath = nativePath("libmithril.so"),
        dlopenLibPaths = emptyList(),
        env = buildEnvs {
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
            // Custom driver selection. Many Snapdragon devices expose only
            // Vulkan 1.1 through the system libvulkan.so; Turnip ships as a
            // separate ICD. Pointing either of these at its json makes the
            // platform loader pick it up instead of the system driver.
            // Mithril steps its instance down to 1.1 when 1.2 is refused and
            // gates every 1.2-only feature on the negotiated version, so a 1.1
            // driver is usable - it just runs with fewer features enabled.
            customizable("VK_ICD_FILENAMES", "", RendererConfig.MetaString("mithril_vk_icd_title"))
            customizable("VK_DRIVER_FILES", "", RendererConfig.MetaString("mithril_vk_driver_files_title"))
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
                put("POJAV_RENDERER", "mithril")
            }
            pojavEnv {
                put("POJAV_RENDERER", "mithril")
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
