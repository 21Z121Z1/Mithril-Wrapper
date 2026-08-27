#!/usr/bin/env python3
from pathlib import Path

p = Path('Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp')
s = p.read_text()
anchor = '''    // Get-or-create the VkGraphicsPipeline. Blend state + colorWriteMask are\n'''
probe = r'''    // EXPERIMENT: Minecraft 26.2 GPU atlas builder probe. Program 36 is the
    // sprite-copy shader in the real E2E. Record the exact draw FBO attachment
    // subresource plus viewport/scissor so we can distinguish a missing
    // per-mip attachment view from a coordinate/load-store bug.
    if (prog->id == 36) {
        static int atlasFboProbeCount = 0;
        if (atlasFboProbeCount < 1200) {
            mithril::Framebuffer* pfbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
            GLuint targetTex = 0;
            GLint targetLevel = 0;
            GLint targetLayer = 0;
            GLboolean targetLayered = GL_FALSE;
            GLenum targetTextarget = 0;
            int targetW = 0, targetH = 0, targetLevels = 0;
            GLenum drawBuf0 = GL_NONE;
            if (pfbo) {
                drawBuf0 = pfbo->drawBuffers[0];
                int ci = 0;
                if (drawBuf0 >= GL_COLOR_ATTACHMENT0 &&
                    drawBuf0 < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
                    ci = (int)(drawBuf0 - GL_COLOR_ATTACHMENT0);
                }
                const mithril::FBOAttachment& fa = pfbo->colors[ci];
                targetTex = mithril::fbo_attachment_texture(fa);
                targetLevel = fa.level;
                targetLayer = fa.layer;
                targetLayered = fa.layered ? GL_TRUE : GL_FALSE;
                targetTextarget = fa.textarget;
                if (mithril::Texture* tt = mithril::state_get_texture(targetTex)) {
                    targetW = tt->width;
                    targetH = tt->height;
                    targetLevels = tt->levels;
                }
            }
            MITHRIL_LOG_WARN("vk",
                "ATLAS_FBO n=%d fbo=%u drawBuf0=0x%x targetTex=%u level=%d layer=%d layered=%d textarget=0x%x base=%dx%d texLevels=%d vp=(%d,%d %dx%d) scissorOn=%d sc=(%d,%d %dx%d) blend=%d",
                atlasFboProbeCount, (unsigned)g_state->currentDrawFBO,
                (unsigned)drawBuf0, (unsigned)targetTex, (int)targetLevel,
                (int)targetLayer, (int)targetLayered, (unsigned)targetTextarget,
                targetW, targetH, targetLevels,
                g_state->viewportX, g_state->viewportY,
                g_state->viewportW, g_state->viewportH,
                g_state->scissorTest ? 1 : 0,
                g_state->scissorX, g_state->scissorY,
                g_state->scissorW, g_state->scissorH,
                g_state->blends[0].enabled ? 1 : 0);
            ++atlasFboProbeCount;
        }
    }

'''
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, probe + anchor, 1)
p.write_text(s)
