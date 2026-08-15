#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = ROOT / "ci/minecraft-e2e/native/glfw_mithril_bridge.mm"
s = p.read_text()

def replace_once(old: str, new: str, label: str):
    global s
    if new in s:
        return
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one match, found {n}")
    s = s.replace(old, new, 1)

replace_once(
    '''std::atomic<unsigned long long> g_proc_count{0};\n''',
    '''std::atomic<unsigned long long> g_proc_count{0};\nstd::string g_last_gl_vendor;\nstd::string g_last_gl_renderer;\nstd::string g_last_gl_version;\n''',
    "identity cache globals",
)

replace_once(
    '''    const char* vendor = "";\n    const char* renderer = "";\n    const char* version = "";\n    auto& m = mithril();\n    if (g_current && m.getString) {\n        vendor = reinterpret_cast<const char*>(m.getString(GL_VENDOR));\n        renderer = reinterpret_cast<const char*>(m.getString(GL_RENDERER));\n        version = reinterpret_cast<const char*>(m.getString(GL_VERSION));\n    }\n''',
    '''    const char* vendor = "";\n    const char* renderer = "";\n    const char* version = "";\n    auto& m = mithril();\n    if (g_current && m.getString) {\n        vendor = reinterpret_cast<const char*>(m.getString(GL_VENDOR));\n        renderer = reinterpret_cast<const char*>(m.getString(GL_RENDERER));\n        version = reinterpret_cast<const char*>(m.getString(GL_VERSION));\n        if (vendor && *vendor) g_last_gl_vendor = vendor;\n        if (renderer && *renderer) g_last_gl_renderer = renderer;\n        if (version && *version) g_last_gl_version = version;\n    } else {\n        // The final bridge-state snapshot is normally written after Minecraft\n        // detaches/destroys the context. Preserve the last identity observed\n        // while the production context was current instead of overwriting the\n        // evidence with empty strings during teardown.\n        vendor = g_last_gl_vendor.c_str();\n        renderer = g_last_gl_renderer.c_str();\n        version = g_last_gl_version.c_str();\n    }\n''',
    "persist last GL identity",
)

replace_once(
    '''    const EGLint contextAttribs[] = {\n        EGL_CONTEXT_MAJOR_VERSION, 4,\n        EGL_CONTEXT_MINOR_VERSION, 6,\n        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,\n        EGL_NONE\n    };\n''',
    '''    const EGLint contextAttribs[] = {\n        // Keep the bridge request aligned with the semantic capability\n        // contract. Higher-version compatibility symbols may still exist in\n        // the dylib, but this production E2E must not imply a 4.x context.\n        EGL_CONTEXT_MAJOR_VERSION, 3,\n        EGL_CONTEXT_MINOR_VERSION, 3,\n        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,\n        EGL_NONE\n    };\n''',
    "bridge context version",
)

p.write_text(s)
print("bridge identity transformation complete")
