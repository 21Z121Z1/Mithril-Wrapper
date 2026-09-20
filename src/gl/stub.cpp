// Mithril-Wrapper GL stub support: logs the first call to any unimplemented
// GL function. Kept in its own TU to avoid recompiling the whole stub table.

#include "internal.h"

#include <util/log.h>

namespace mithril {

void GlStubCalled(const char* name) {
    ML_LOG_WARN("GL stub called (unimplemented, failing closed): %s()", name);
    PUSH_ERROR(GL_INVALID_OPERATION);
}

} // namespace mithril
