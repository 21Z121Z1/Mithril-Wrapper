from pathlib import Path

# Public header: expose the standard GL 3.2 sync query surface consistently.
p = Path('Mithril-Wrapper-cpp/include/GL/glcorearb.h')
s = p.read_text()
anchor = '''#define GL_WAIT_FAILED                  0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT      0x00000001
#define GL_TIMEOUT_IGNORED              ((GLuint64)-1)
#define GL_FENCE_CONDITION              0x1184
'''
replacement = '''#define GL_WAIT_FAILED                  0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT      0x00000001
#define GL_TIMEOUT_IGNORED              ((GLuint64)-1)
#define GL_FENCE_CONDITION              0x1184
#ifndef GL_OBJECT_TYPE
#define GL_OBJECT_TYPE                  0x9112
#endif
#ifndef GL_SYNC_CONDITION
#define GL_SYNC_CONDITION               0x9113
#endif
#ifndef GL_SYNC_STATUS
#define GL_SYNC_STATUS                  0x9114
#endif
#ifndef GL_SYNC_FLAGS
#define GL_SYNC_FLAGS                   0x9115
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE   0x9117
#endif
#ifndef GL_UNSIGNALED
#define GL_UNSIGNALED                   0x9118
#endif
#ifndef GL_SIGNALED
#define GL_SIGNALED                     0x9119
#endif
#ifndef GL_SYNC_FENCE
#define GL_SYNC_FENCE                   0x9116
#endif
'''
if s.count(anchor) != 1:
    raise SystemExit(f'glcore sync enum anchor matches={s.count(anchor)}')
s = s.replace(anchor, replacement, 1)
anchor = '''GLAPI GLenum GLAPIENTRY glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
GLAPI void GLAPIENTRY glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
GLAPI GLboolean GLAPIENTRY glIsSync(GLsync sync);
'''
replacement = anchor + '''GLAPI void GLAPIENTRY glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values);
'''
if s.count(anchor) != 1:
    raise SystemExit(f'glcore sync declaration anchor matches={s.count(anchor)}')
p.write_text(s.replace(anchor, replacement, 1))

# Runtime smoke diagnostics: prove symbol ownership and same-handle visibility.
p = Path('tests/render_smoke.c')
s = p.read_text()
s = s.replace(
    'typedef void      (*deleteSync_fn)(GLsync);\ntypedef void      (*getSynciv_fn)(GLsync, GLenum, GLsizei, GLsizei*, GLint*);\n',
    'typedef void      (*deleteSync_fn)(GLsync);\ntypedef GLboolean (*isSync_fn)(GLsync);\ntypedef void      (*getSynciv_fn)(GLsync, GLenum, GLsizei, GLsizei*, GLint*);\n',
    1)
s = s.replace(
    '    deleteSync_fn         deleteSync         = NULL;\n    getSynciv_fn          getSynciv          = NULL;\n',
    '    deleteSync_fn         deleteSync         = NULL;\n    isSync_fn             isSync             = NULL;\n    getSynciv_fn          getSynciv          = NULL;\n',
    1)
s = s.replace(
    '    RESOLVE(deleteSync, "glDeleteSync");\n    RESOLVE(getSynciv, "glGetSynciv");\n',
    '    RESOLVE(deleteSync, "glDeleteSync");\n    RESOLVE(isSync, "glIsSync");\n    RESOLVE(getSynciv, "glGetSynciv");\n',
    1)
old = '''        GLsync sync = fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        CHECK(sync != NULL, "glFenceSync created a real submission fence");
        GLenum wait = clientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
        CHECK(wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED,
              "glClientWaitSync observes GPU completion (result=0x%x)", wait);
        GLint status = GL_UNSIGNALED;
        GLsizei length = 0;
        getSynciv(sync, GL_SYNC_STATUS, 1, &length, &status);
        CHECK(length == 1 && status == GL_SIGNALED,
              "glGetSynciv reports GL_SIGNALED after wait (status=0x%x)", status);
        deleteSync(sync);
        CHECK(getError() == GL_NO_ERROR, "GLsync lifecycle leaves no error");
'''
new = '''        GLsync sync = fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        CHECK(sync != NULL, "glFenceSync created a real submission fence");
        CHECK(sync != NULL && isSync(sync) == GL_TRUE,
              "glIsSync recognizes the same fence handle before wait");

        Dl_info syncInfo = {0};
        int dladdrOk = dladdr((const void*)getSynciv, &syncInfo);
        CHECK(dladdrOk != 0 && syncInfo.dli_fname && strstr(syncInfo.dli_fname, "libmithril"),
              "glGetSynciv resolves to Mithril dylib (%s)",
              (dladdrOk && syncInfo.dli_fname) ? syncInfo.dli_fname : "(unknown)");

        GLint beforeStatus = -1;
        GLsizei beforeLength = 0;
        getSynciv(sync, GL_SYNC_STATUS, 1, &beforeLength, &beforeStatus);
        CHECK(beforeLength == 1 && (beforeStatus == GL_UNSIGNALED || beforeStatus == GL_SIGNALED),
              "glGetSynciv can query fence before wait (length=%d status=0x%x)",
              (int)beforeLength, beforeStatus);

        GLenum wait = clientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
        CHECK(wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED,
              "glClientWaitSync observes GPU completion (result=0x%x)", wait);
        CHECK(isSync(sync) == GL_TRUE, "glIsSync still recognizes fence after wait");
        GLint status = -1;
        GLsizei length = 0;
        getSynciv(sync, GL_SYNC_STATUS, 1, &length, &status);
        CHECK(length == 1 && status == GL_SIGNALED,
              "glGetSynciv reports GL_SIGNALED after wait (length=%d status=0x%x)",
              (int)length, status);
        deleteSync(sync);
        CHECK(getError() == GL_NO_ERROR, "GLsync lifecycle leaves no error");
'''
if s.count(old) != 1:
    raise SystemExit(f'render sync block matches={s.count(old)}')
p.write_text(s.replace(old, new, 1))
