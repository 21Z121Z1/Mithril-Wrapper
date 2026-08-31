# Minecraft reference sources for agents

Mithril-Wrapper is frequently debugged against Minecraft Java 26.2 behavior. When a task depends on vanilla GLFW/window, framebuffer, GUI/text, texture upload, RenderSystem, or swap/present behavior, inspect the client implementation rather than inferring the call pattern from renderer logs alone.

From the repository root:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

The command defaults to Minecraft `26.2`, resolves the version through Mojang's official version manifest, verifies Mojang-provided SHA-1 hashes, downloads pinned Vineflower `1.12.0` and verifies its pinned SHA-256, then decompiles the client under:

```text
.minecraft-reference/26.2/sources/
```

The directory is intentionally git-ignored. The Minecraft client JAR and decompiler output are local analysis inputs only; do not commit them or upload them as GitHub Actions artifacts.

Useful forms:

```bash
bash scripts/minecraft-reference.sh --print-path
bash scripts/minecraft-reference.sh --force
bash scripts/minecraft-reference.sh --clean
MINECRAFT_REFERENCE_VERSION=26.2 bash scripts/minecraft-reference.sh --print-path
```

The generated tree is decompiler output rather than Mojang-authored source. Use it as behavioral/reference evidence and confirm important conclusions against runtime oracles when the distinction matters.

Typical searches:

```bash
rg 'glfwSetInputMode|glfwGetInputMode' "$SRC/net/minecraft"
rg 'RenderTarget|framebuffer' "$SRC/net/minecraft/client"
rg 'RenderSystem' "$SRC/com/mojang/blaze3d"
```

`REFERENCE_INFO.txt` beside the generated tree records the resolved Minecraft metadata SHA-1, client SHA-1, Vineflower identity and Java-file count for provenance.

This tooling was originally proven on the legacy DirectVulkan GUI lineage in PR #37. It is promoted into the clean-tree agent contract because Minecraft source inspection is a repository-wide capability, not a DirectVulkan implementation detail.
