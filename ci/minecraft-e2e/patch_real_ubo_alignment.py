#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Buffer.cpp')
s = p.read_text()
old = '''    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT is implementation-defined; approximate
    // with 256 (a common desktop value). Enforced only for uniform buffers.
    if (target == GL_UNIFORM_BUFFER && (offset % 256) != 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
'''
new = '''    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT must be the SAME value we expose via
    // glGetIntegerv. DirectVulkan reports VkPhysicalDeviceLimits::
    // minUniformBufferOffsetAlignment (often 16 on MoltenVK); rejecting those
    // legal offsets against a hard-coded 256 leaves the previous indexed UBO
    // range bound and makes subsequent draws read stale transforms.
    if (target == GL_UNIFORM_BUFFER) {
        GLint alignment = backend_device_limit(MITHRIL_LIMIT_UNIFORM_BUFFER_ALIGNMENT, 256);
        if (alignment < 1) alignment = 1;
        if (offset < 0 || (offset % alignment) != 0) {
            static int alignRejectLog = 0;
            if (alignRejectLog++ < 16) {
                MITHRIL_LOG_WARN("gl", "glBindBufferRange UBO alignment reject: off=%lld align=%d index=%u buffer=%u size=%lld",
                                 (long long)offset, (int)alignment, (unsigned)index,
                                 (unsigned)buffer, (long long)size);
            }
            mithril::state_set_error(GL_INVALID_VALUE);
            return;
        }
    }
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
