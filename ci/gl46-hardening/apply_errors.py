#!/usr/bin/env python3
"""Restore spec-observable OpenGL error semantics.

Mithril historically swallowed the queued GL error in glGetError() to reduce
Minecraft log noise.  That makes any conformance claim impossible and also
hides real renderer bugs.  The state machine already has a bounded FIFO error
queue, so expose it exactly through glGetError instead of discarding it.
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[2]
GETTER = ROOT / 'Mithril-Wrapper-cpp/MG_Impl/Getter.cpp'
AUDIT = ROOT / 'verify/gl46_semantic_audit.py'

OLD = '''GLenum glGetError(void) {
    MITHRIL_ENSURE_INIT();
    // Mirror MobileGlues: always return GL_NO_ERROR to prevent Minecraft from
    // spamming the log with GL errors that are harmless in the translation layer.
    mithril::state_take_error();
    return GL_NO_ERROR;
}
'''
NEW = '''GLenum glGetError(void) {
    MITHRIL_ENSURE_INIT();
    /* OpenGL exposes the oldest pending error and removes exactly that error
     * from the context queue.  The state machine already maintains the queue;
     * never discard it merely to silence a workload's diagnostics. */
    return mithril::state_take_error();
}
'''

AUDIT_CHECK = '''\nrequire("return mithril::state_take_error();" in (root / "Mithril-Wrapper-cpp/MG_Impl/Getter.cpp").read_text(),\n        "glGetError must expose the context error queue instead of swallowing it")\n'''


def apply():
    text = GETTER.read_text()
    if OLD in text:
        text = text.replace(OLD, NEW, 1)
        GETTER.write_text(text)
    elif NEW not in text:
        raise SystemExit('glGetError anchor not found')
    audit = AUDIT.read_text()
    if 'glGetError must expose the context error queue' not in audit:
        audit += AUDIT_CHECK
        AUDIT.write_text(audit)
    verify()


def verify():
    text = GETTER.read_text()
    if NEW not in text:
        raise SystemExit('glGetError is not spec-observable')
    if 'mithril::state_take_error();\n    return GL_NO_ERROR;' in text:
        raise SystemExit('legacy error swallowing remains')
    compile(AUDIT.read_text(), str(AUDIT), 'exec')
    print('GL error semantics: PASS')


def main():
    p = argparse.ArgumentParser()
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('--apply', action='store_true')
    g.add_argument('--verify', action='store_true')
    a = p.parse_args()
    apply() if a.apply else verify()

if __name__ == '__main__':
    main()
