#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Buffer.cpp')
s = p.read_text()
old = '''    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT is implementation-defined; approximate\n    // with 256 (a common desktop value). Enforced only for uniform buffers.\n    if (target == GL_UNIFORM_BUFFER && (offset % 256) != 0) {\n        mithril::state_set_error(GL_INVALID_VALUE);\n        return;\n    }\n'''
new = '''    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT must be the SAME value we expose via\n    // glGetIntegerv. DirectVulkan reports VkPhysicalDeviceLimits::\n    // minUniformBufferOffsetAlignment (often 16 on MoltenVK); rejecting those\n    // legal offsets against a hard-coded 256 leaves the previous indexed UBO\n    // range bound and makes subsequent draws read stale transforms.\n    if (target == GL_UNIFORM_BUFFER) {\n        GLint alignment = backend_device_limit(MITHRIL_LIMIT_UNIFORM_BUFFER_ALIGNMENT, 256);\n        if (alignment < 1) alignment = 1;\n        if (offset < 0 || (offset % alignment) != 0) {\n            static int alignRejectLog = 0;\n            if (alignRejectLog++ < 16) {\n                MITHRIL_LOG_WARN(\"gl\", \"glBindBufferRange UBO alignment reject: off=%lld align=%d index=%u buffer=%u size=%lld\",\n                                 (long long)offset, (int)alignment, (unsigned)index,\n                                 (unsigned)buffer, (long long)size);\n            }\n            mithril::state_set_error(GL_INVALID_VALUE);\n            return;\n        }\n    }\n'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
p.write_text(s)
'''
