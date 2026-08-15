#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str, label: str):
    p = ROOT / path
    s = p.read_text()
    if new in s:
        return
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one match, found {n}")
    p.write_text(s.replace(old, new, 1))

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/includes.h",
    '''using mithril::g_state;\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n''',
    '''using mithril::g_state;\n\nnamespace mithril {\n// Records the first call to each GL entry point that originated outside the\n// Mithril dylib. Calls made by one Mithril GL entry point delegating to another\n// are filtered using the caller image, so the production trace reflects the\n// application/LWJGL surface rather than wrapper implementation details.\nvoid semantic_trace_external_api_call(const char* api, const void* caller);\n}\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n''',
    "trace declaration",
)

replace_once(
    "Mithril-Wrapper-cpp/MG_Impl/includes.h",
    '''#define MITHRIL_ENSURE_INIT() \\\n    do { \\\n        if (!::mithril::g_state) { \\\n            if (!::mithril::g_eglInitialized) ::proc_init(); \\\n        } else if (!::mithril::g_eglInitialized && !backend_available()) { \\\n            ::proc_init(); \\\n        } \\\n    } while (0)\n''',
    '''#if defined(__clang__) || defined(__GNUC__)\n#define MITHRIL_CALLER_ADDRESS() __builtin_return_address(0)\n#else\n#define MITHRIL_CALLER_ADDRESS() nullptr\n#endif\n\n#define MITHRIL_ENSURE_INIT() \\\n    do { \\\n        if (!::mithril::g_state) { \\\n            if (!::mithril::g_eglInitialized) ::proc_init(); \\\n        } else if (!::mithril::g_eglInitialized && !backend_available()) { \\\n            ::proc_init(); \\\n        } \\\n        ::mithril::semantic_trace_external_api_call(__func__, MITHRIL_CALLER_ADDRESS()); \\\n    } while (0)\n''',
    "trace macro",
)

replace_once(
    "Mithril-Wrapper-cpp/MG_State/State.cpp",
    '''#include <cstdarg>\n#include <cstdio>\n#include <cstdlib>\n#include <mutex>\n''',
    '''#include <cstdarg>\n#include <cstdio>\n#include <cstdlib>\n#include <mutex>\n#include <string>\n#include <unordered_set>\n#if defined(__APPLE__) || defined(__linux__)\n#include <dlfcn.h>\n#endif\n''',
    "trace includes",
)

anchor = '''void semantic_trace_eventf(const char* domain, const char* semantic,\n                           const char* api, const char* fmt, ...) {\n    static std::mutex traceMutex;\n    static bool initialized = false;\n    static FILE* traceFile = nullptr;\n\n    std::lock_guard<std::mutex> lock(traceMutex);\n    if (!initialized) {\n        initialized = true;\n        const char* path = std::getenv("MITHRIL_GL_SEMANTIC_TRACE");\n        if (path && *path) {\n            traceFile = std::fopen(path, "a");\n            if (traceFile) std::setvbuf(traceFile, nullptr, _IOLBF, 0);\n        }\n    }\n    if (!traceFile) return;\n\n    char details[1024] = {};\n    if (fmt && *fmt) {\n        va_list args;\n        va_start(args, fmt);\n        std::vsnprintf(details, sizeof(details), fmt, args);\n        va_end(args);\n    }\n    std::fprintf(traceFile, "%s\\t%s\\t%s\\t%s\\n",\n                 domain ? domain : "", semantic ? semantic : "",\n                 api ? api : "", details);\n}\n'''
addition = anchor + '''\nvoid semantic_trace_external_api_call(const char* api, const void* caller) {\n    // Keep production overhead effectively zero unless tracing was explicitly\n    // requested by CI. getenv is resolved only once per process.\n    static const bool enabled = [] {\n        const char* path = std::getenv("MITHRIL_GL_SEMANTIC_TRACE");\n        return path && *path;\n    }();\n    if (!enabled || !api || !*api) return;\n\n#if defined(__APPLE__) || defined(__linux__)\n    // A large part of the compatibility implementation delegates one GL entry\n    // point to another. Those are implementation details, not calls made by\n    // Minecraft/LWJGL. Compare image bases and suppress same-dylib callers.\n    Dl_info callerInfo{};\n    Dl_info selfInfo{};\n    if (caller && dladdr(caller, &callerInfo) != 0 &&\n        dladdr(reinterpret_cast<const void*>(&semantic_trace_external_api_call), &selfInfo) != 0 &&\n        callerInfo.dli_fbase == selfInfo.dli_fbase) {\n        return;\n    }\n#endif\n\n    thread_local std::unordered_set<std::string> seen;\n    if (!seen.emplace(api).second) return;\n    std::string semantic = std::string("api.") + api;\n    semantic_trace_eventf("api_call", semantic.c_str(), api, "external_first_call");\n}\n'''
replace_once(
    "Mithril-Wrapper-cpp/MG_State/State.cpp",
    anchor,
    addition,
    "external trace implementation",
)

print("external GL API trace transformation complete")
