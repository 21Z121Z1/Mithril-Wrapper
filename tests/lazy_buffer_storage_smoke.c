/* Backend-neutral regression for lazy glBufferData(NULL) CPU storage. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_ARRAY_BUFFER 0x8892
#define GL_COPY_READ_BUFFER 0x8F36
#define GL_COPY_WRITE_BUFFER 0x8F37
#define GL_BUFFER_SIZE 0x8764
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_MAP_WRITE_BIT 0x0002
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long long GLsizeiptr;
typedef long long GLintptr;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;

typedef void (*PFN_GenBuffers)(GLsizei, GLuint*);
typedef void (*PFN_BindBuffer)(GLenum, GLuint);
typedef void (*PFN_BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*PFN_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*PFN_GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void*);
typedef void (*PFN_GetBufferParameteriv)(GLenum, GLenum, GLint*);
typedef void* (*PFN_MapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef GLboolean (*PFN_UnmapBuffer)(GLenum);
typedef void (*PFN_CopyBufferSubData)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr);
typedef GLenum (*PFN_GetError)(void);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

int main(void) {
    const char* lib = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!lib || !*lib) lib = "./output/libmithril.dylib";
#else
    if (!lib || !*lib) lib = "./output/libmithril.so";
#endif
    void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
#define LOAD(type, name, sym) type name = (type)dlsym(h, sym)
    LOAD(PFN_GenBuffers, genBuffers, "glGenBuffers");
    LOAD(PFN_BindBuffer, bindBuffer, "glBindBuffer");
    LOAD(PFN_BufferData, bufferData, "glBufferData");
    LOAD(PFN_BufferSubData, bufferSubData, "glBufferSubData");
    LOAD(PFN_GetBufferSubData, getBufferSubData, "glGetBufferSubData");
    LOAD(PFN_GetBufferParameteriv, getBufferParameteriv, "glGetBufferParameteriv");
    LOAD(PFN_MapBufferRange, mapBufferRange, "glMapBufferRange");
    LOAD(PFN_UnmapBuffer, unmapBuffer, "glUnmapBuffer");
    LOAD(PFN_CopyBufferSubData, copyBufferSubData, "glCopyBufferSubData");
    LOAD(PFN_GetError, getError, "glGetError");
#undef LOAD
    CHECK(genBuffers && bindBuffer && bufferData && bufferSubData &&
          getBufferSubData && getBufferParameteriv && mapBufferRange &&
          unmapBuffer && copyBufferSubData && getError,
          "required buffer entry points resolve");
    if (failures) return failures;

    enum { LARGE_SIZE = 1024 * 1024 };
    GLuint a = 0, b = 0;
    genBuffers(1, &a); genBuffers(1, &b);

    bindBuffer(GL_ARRAY_BUFFER, a);
    bufferData(GL_ARRAY_BUFFER, LARGE_SIZE, NULL, GL_DYNAMIC_DRAW);
    GLint logicalSize = -1;
    getBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &logicalSize);
    CHECK(logicalSize == LARGE_SIZE, "NULL store keeps logical size (%d)", logicalSize);

    const uint8_t partial[16] = {0x10,0x11,0x12,0x13,0x20,0x21,0x22,0x23,
                                 0x30,0x31,0x32,0x33,0x40,0x41,0x42,0x43};
    bufferSubData(GL_ARRAY_BUFFER, 128, sizeof(partial), partial);
    uint8_t readPartial[16] = {0};
    getBufferSubData(GL_ARRAY_BUFFER, 128, sizeof(readPartial), readPartial);
    CHECK(memcmp(partial, readPartial, sizeof(partial)) == 0,
          "partial write materializes lazy store correctly");

    bufferData(GL_ARRAY_BUFFER, LARGE_SIZE, NULL, GL_DYNAMIC_DRAW);
    uint8_t* full = (uint8_t*)malloc(LARGE_SIZE);
    CHECK(full != NULL, "full-overwrite fixture allocates");
    if (!full) return failures;
    for (size_t i = 0; i < LARGE_SIZE; ++i) full[i] = (uint8_t)(i * 37u + 11u);
    bufferSubData(GL_ARRAY_BUFFER, 0, LARGE_SIZE, full);
    uint8_t tail[16] = {0};
    getBufferSubData(GL_ARRAY_BUFFER, LARGE_SIZE - sizeof(tail), sizeof(tail), tail);
    CHECK(memcmp(tail, full + LARGE_SIZE - sizeof(tail), sizeof(tail)) == 0,
          "full overwrite of lazy store preserves caller bytes");
    free(full);

    bufferData(GL_ARRAY_BUFFER, 4096, NULL, GL_DYNAMIC_DRAW);
    uint8_t* mapped = (uint8_t*)mapBufferRange(GL_ARRAY_BUFFER, 64, 32, GL_MAP_WRITE_BIT);
    CHECK(mapped != NULL, "mapping materializes lazy store on demand");
    if (mapped) {
        for (int i = 0; i < 32; ++i) mapped[i] = (uint8_t)(0x80 + i);
        CHECK(unmapBuffer(GL_ARRAY_BUFFER), "mapped lazy store unmaps");
        uint8_t got[32] = {0};
        getBufferSubData(GL_ARRAY_BUFFER, 64, sizeof(got), got);
        int same = 1;
        for (int i = 0; i < 32; ++i) if (got[i] != (uint8_t)(0x80 + i)) same = 0;
        CHECK(same, "mapped write remains observable");
    }

    bindBuffer(GL_COPY_READ_BUFFER, a);
    bufferData(GL_COPY_READ_BUFFER, 4096, NULL, GL_DYNAMIC_DRAW);
    const uint8_t payload[24] = {1,2,3,4,5,6,7,8,9,10,11,12,
                                 13,14,15,16,17,18,19,20,21,22,23,24};
    bufferSubData(GL_COPY_READ_BUFFER, 200, sizeof(payload), payload);
    bindBuffer(GL_COPY_WRITE_BUFFER, b);
    bufferData(GL_COPY_WRITE_BUFFER, 4096, NULL, GL_DYNAMIC_DRAW);
    copyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 200, 300, sizeof(payload));
    uint8_t copyRead[24] = {0};
    getBufferSubData(GL_COPY_WRITE_BUFFER, 300, sizeof(copyRead), copyRead);
    CHECK(memcmp(payload, copyRead, sizeof(payload)) == 0,
          "copy between lazy stores preserves copied range");

    CHECK(getError() == GL_NO_ERROR, "lazy storage scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures ? 1 : 0;
}
