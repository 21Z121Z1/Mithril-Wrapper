// Mithril-Wrapper - MG_State/Caps.h
// Single source of truth for the GL version / GLSL version / extension set we
// advertise to the host. Modelled on MobileGL's RendererGLInfo (TargetGLVersion
// + dynamic Extensions): NEVER hardcode a version in the getters. The host is
// told ONLY what is actually implemented, so applications like Minecraft take
// code paths we can service instead of silently sampling undefined resources
// (which renders pure red).
#pragma once
#include <string>
#include <vector>

namespace mithril {

// Advertised level. 4.6 matches MobileGL's RendererGLInfo::TargetGLVersion,
// which is the configuration verified on device with Sodium + Iris + BSL.
//
// The level is only as honest as the entry points behind it, so it is
// overridable: MITHRIL_GL_VERSION=3.3 (or 4.6) switches the advertisement
// without a rebuild. That matters because a wrong level in either direction is
// a failure - too high and hosts take paths with no implementation behind them
// (the original solid-red frame), too low and Sodium refuses to load at all.
struct Caps {
    int gl_major   = 4;
    int gl_minor   = 6;
    int glsl_major = 4;
    int glsl_minor = 60;   // GLSL 460 (matches GL 4.6)
};

// Process-wide capability set. Safe to call from any thread after init.
const Caps& caps();

// "OpenGL <major>.<minor>.0 Mithril-Wrapper ..." built from caps().
const std::string& version_string();
const std::string& glsl_version_string();

// Extensions we actually implement (curated; unsupported entries removed).
const std::vector<const char*>& extensions();
bool has_extension(const char* name);

} // namespace mithril
