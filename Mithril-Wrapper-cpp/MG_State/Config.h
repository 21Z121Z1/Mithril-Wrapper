// Mithril-Wrapper - MG_State/Config.h
// Runtime knobs read from a plain key=value file, so the escape hatches that
// were previously environment-variable-only can be set on a device where the
// launcher gives no way to define environment variables.
//
// Mirrors the pattern Amethyst already uses for MobileGlues (it writes
// <POJAV_HOME>/MG/config.json, which MobileGlues reads at startup): a file the
// user can edit with a file manager, rather than something that has to be
// injected into the process environment.
#pragma once

namespace mithril {

// Look for the config file and parse it. Safe to call repeatedly; the first
// call does the work. Logs which path was used, so the log tells you exactly
// where to put the file.
void config_load(void);

// Value for `key`, or nullptr when the key is absent.
const char* config_get(const char* key);

} // namespace mithril
