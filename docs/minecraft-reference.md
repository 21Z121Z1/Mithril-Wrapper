# Minecraft reference sources for agents

Mithril-Wrapper is frequently debugged against Minecraft Java 26.2 behavior. Agents that need to inspect vanilla OpenGL calls, GLFW/window behavior, framebuffer handling, GUI/text rendering, texture upload, or swap/present code should materialize a local reference tree before inferring behavior from logs alone.

Run exactly one command from the repository root:

```bash
bash scripts/minecraft-reference.sh
```

The command defaults to Minecraft `26.2`, resolves it through Mojang's official version manifest, verifies Mojang-provided SHA-1 hashes, downloads the pinned Vineflower release and verifies its pinned SHA-256, then decompiles the client into:

```text
.minecraft-reference/26.2/sources/
```

The directory is intentionally git-ignored. The Minecraft client JAR and generated source-like Java are local analysis inputs only and must not be committed, uploaded as Actions artifacts, or redistributed from this repository.

Useful forms:

```bash
# Generate if needed, then print only the source-root path.
bash scripts/minecraft-reference.sh --print-path

# Re-resolve metadata and rebuild the source tree.
bash scripts/minecraft-reference.sh --force

# Remove the generated source tree for the selected version.
bash scripts/minecraft-reference.sh --clean

# Override the version.
MINECRAFT_REFERENCE_VERSION=26.2 bash scripts/minecraft-reference.sh
```

The generated tree is decompiler output, not Mojang's original authored source. Minecraft 26.2 is non-obfuscated, so class/package names remain directly useful for code search, but comments and original formatting are not recoverable.

Typical wrapper investigations can then search the stable tree directly:

```bash
rg 'glfwSetInputMode|glfwGetInputMode' .minecraft-reference/26.2/sources/net/minecraft
rg 'RenderTarget|framebuffer' .minecraft-reference/26.2/sources/net/minecraft/client
rg 'RenderSystem' .minecraft-reference/26.2/sources/com/mojang/blaze3d
```

`REFERENCE_INFO.txt` beside the source directory records the resolved Minecraft metadata SHA-1, client SHA-1, Vineflower identity, and generated Java-file count for provenance.
