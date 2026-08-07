#include "state.h"

namespace mithril::state {

void ErrorQueue::Push(GLenum e) {
    if (count >= kErrStack) {
        // Full: drop the oldest, keep the newest.
        head = (head + 1) % kErrStack;
        --count;
    }
    stack[(head + count) % kErrStack] = e;
    ++count;
}

GLenum ErrorQueue::Pop() {
    if (count == 0) return GL_NO_ERROR;
    GLenum e = stack[head];
    head = (head + 1) % kErrStack;
    --count;
    return e;
}

GLState& GetState() {
    static GLState s;
    return s;
}

}  // namespace mithril::state