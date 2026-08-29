# Mithril-Wrapper agent entry

Mithril-Wrapper is an OpenGL 4.6 Core compatibility layer for Apple platforms with DirectMetal and Vulkan/MoltenVK backends. Read `README.md` before changing backend contracts, EGL behavior, GL state ownership, shader translation, or CI.

## Minecraft 26.2 reference source

When a task depends on Minecraft client behavior, do not guess from logs if the local vanilla reference tree is absent. Materialize it once from the repository root:

```bash
bash scripts/minecraft-reference.sh
```

The command downloads the official Minecraft client for version 26.2, verifies Mojang-provided hashes, verifies the pinned Vineflower decompiler, and produces a stable local source tree at:

```text
.minecraft-reference/26.2/sources/
```

Use that tree for GLFW/window, framebuffer, GUI/text, texture, RenderSystem, and swap/present investigations. It is decompiler output for local analysis only. `.minecraft-reference/` is git-ignored and must never be committed or uploaded as an Actions artifact.

See `docs/minecraft-reference.md` for provenance, cache behavior, and overrides.
