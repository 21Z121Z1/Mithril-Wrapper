// Mithril-Wrapper shader module -- object tables (M2-S2).
// Single shared context: Shader/Program registries, name allocation and
// the public New/Get/Delete object-table entry points.

#include <shader/shader.h>

#include <GL/glcorearb.h>

#include <state/state.h>

#include <unordered_map>

namespace mithril::shader {

// ---- object tables ----------------------------------------------------------

struct Registry {
    std::unordered_map<GLuint, Shader> shaders;
    std::unordered_map<GLuint, Program> programs;
    GLuint next_name = 1;
};
Registry& reg() { static Registry r; return r; }

Shader* GetShader(GLuint id) {
    auto it = reg().shaders.find(id);
    return it == reg().shaders.end() ? nullptr : &it->second;
}

Program* GetProgram(GLuint id) {
    auto it = reg().programs.find(id);
    return it == reg().programs.end() ? nullptr : &it->second;
}

GLuint NewShader(GLenum type) {
    GLuint id = reg().next_name++;
    Shader s;
    s.id = id;
    s.type = type;
    reg().shaders[id] = std::move(s);
    return id;
}

GLuint NewProgram() {
    GLuint id = reg().next_name++;
    Program p;
    p.id = id;
    reg().programs[id] = std::move(p);
    return id;
}

void DeleteShader(GLuint id) { reg().shaders.erase(id); }

void DeleteProgram(GLuint id) {
    auto& st = mithril::state::GetState();
    if (st.current_program == id) st.current_program = 0;
    reg().programs.erase(id);
}

} // namespace mithril::shader
