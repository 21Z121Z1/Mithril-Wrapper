from pathlib import Path

path = Path("Mithril-Wrapper-cpp/MG_Backend/DirectMetal/MetalCommandStream.mm")
text = path.read_text()

old = '''    const bool restartCanMatch = fixedRestart ||
        (programmableRestart && restartToken <= sourceMax);
    const bool nativeRestartToken = restartCanMatch && restartToken == sourceMax;

    // Metal only accepts U16/U32 indices and ALWAYS reserves that type's
    // maximum value as a primitive-restart sentinel. U8 therefore always
    // needs widening; programmable restart needs remapping unless its token
    // already equals the native maximum. Fixed U16/U32 can stay zero-copy.
    const bool needsRestartRemap = restartCanMatch && !nativeRestartToken;
    if (index_type != 2 && !fan && !loop && !needsRestartRemap) {
'''
new = '''    const bool restartCanMatch = fixedRestart ||
        (programmableRestart && restartToken <= sourceMax);
    const bool nativeRestartToken = restartCanMatch && restartToken == sourceMax;

    // Metal reserves the maximum U16/U32 index as a strip restart sentinel
    // even though OpenGL treats it as an ordinary index while primitive
    // restart is disabled.  Preserve the zero-copy U16 path unless this exact
    // representability conflict is present; then widen only the affected
    // index slice to U32 so 0xffff remains the numeric vertex index 65535.
    const bool stripPrimitive =
        primitive == GL_TRIANGLE_STRIP || primitive == GL_LINE_STRIP;
    bool disabledU16MaxNeedsWiden = false;
    if (!restartCanMatch && index_type == 0 && stripPrimitive) {
        const NSUInteger sourceBytes = (NSUInteger)count * sizeof(uint16_t);
        if (index_buffer->contents == nullptr || index_offset > index_buffer->capacity ||
            sourceBytes > index_buffer->capacity - index_offset) {
            static uint32_t warned = 0;
            warn_limited(warned, "dmt", "U16 strip sentinel scan needs a valid CPU-readable index slice — draw dropped");
            return;
        }
        const uint16_t* s = (const uint16_t*)((const uint8_t*)index_buffer->contents + index_offset);
        for (uint32_t i = 0; i < count; ++i) {
            if (s[i] == 0xffffu) {
                disabledU16MaxNeedsWiden = true;
                break;
            }
        }
    }

    // Metal only accepts U16/U32 indices. U8 therefore always needs widening;
    // programmable restart needs remapping unless its token already equals the
    // native maximum. Fixed U16/U32 can stay zero-copy. A restart-disabled U16
    // strip containing 0xffff takes the narrow widening path above.
    const bool needsRestartRemap = restartCanMatch && !nativeRestartToken;
    if (index_type != 2 && !fan && !loop && !needsRestartRemap &&
        !disabledU16MaxNeedsWiden) {
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one fast-path match, found {text.count(old)}")
text = text.replace(old, new, 1)

old2 = '''    const bool use32 = (index_type == 1) || (index_type == 0 && needsRestartRemap);
'''
new2 = '''    const bool use32 = (index_type == 1) ||
                       (index_type == 0 && (needsRestartRemap || disabledU16MaxNeedsWiden));
'''
if text.count(old2) != 1:
    raise SystemExit(f"expected one use32 match, found {text.count(old2)}")
text = text.replace(old2, new2, 1)

path.write_text(text)
print("U16 disabled-restart strip semantic fix applied")
