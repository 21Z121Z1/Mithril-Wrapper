from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp')
s = p.read_text()
start = s.index('void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values) {')
end = s.index('\n\n} // extern "C"', start)
new = r'''void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;

    if (!sync) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (bufSize < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (bufSize == 0) return;
    if (!values) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }

    void* handle = reinterpret_cast<void*>(sync);
    auto it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }

    mithril::Sync& obj = it->second;
    if (obj.submitSerial != 0 &&
        obj.submitSerial <= mithril::vk::backend_last_completed_serial()) {
        obj.submitSerial = 0;
        obj.signaled = true;
    }

    // Use the canonical GL enum values explicitly at this ABI boundary. The
    // project ships multiple compatibility headers; keeping this switch tied
    // to the specification values prevents a partial/minimal header from
    // changing query dispatch while still exposing the named constants to
    // callers in gl.h/glcorearb.h.
    GLint result = 0;
    switch ((uint32_t)pname) {
        case 0x9112u: result = 0x9116; break; // GL_OBJECT_TYPE -> GL_SYNC_FENCE
        case 0x9113u: result = (GLint)obj.condition; break; // GL_SYNC_CONDITION
        case 0x9115u: result = (GLint)obj.flags; break;     // GL_SYNC_FLAGS
        case 0x9114u: result = (obj.submitSerial == 0) ? 0x9119 : 0x9118; break;
        default:
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
    }

    values[0] = result;
    if (length) *length = 1;
}
'''
p.write_text(s[:start] + new + s[end:])
