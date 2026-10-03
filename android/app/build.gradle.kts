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
            // Keep the libraries uncompressed and page-aligned so they can be
            // dlopened straight from the installed path.
            useLegacyPackaging = false
        }
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}

dependencies {}
