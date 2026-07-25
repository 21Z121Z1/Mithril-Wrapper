#ifndef __glcorearb_h_
#define __glcorearb_h_

/*
 * Mithril-Wrapper: focused OpenGL Core Profile C API surface.
 * Declares the entry points implemented by libmithril.dylib plus the enums
 * used by the implementation. Legacy (fixed-function) entry points live in
 * gl.h. The dylib exports every function declared here.
 */

#include <KHR/khrplatform.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- enums ----------------------------- */
#define GL_FALSE                        0
#define GL_TRUE                         1
#define GL_NONE                         0
#define GL_ZERO                         0
#define GL_ONE                          1

/* Data types */
#define GL_BYTE                         0x1400
#define GL_UNSIGNED_BYTE                0x1401
#define GL_SHORT                        0x1402
#define GL_UNSIGNED_SHORT               0x1403
#define GL_INT                          0x1404
#define GL_UNSIGNED_INT                 0x1405
#define GL_FLOAT                        0x1406
#define GL_2_BYTES                      0x1407
#define GL_3_BYTES                      0x1408
#define GL_4_BYTES                      0x1409
#define GL_DOUBLE                       0x140A
#define GL_HALF_FLOAT                   0x140B
#define GL_FIXED                        0x140C
#define GL_UNSIGNED_INT_2_10_10_10_REV  0x8368
#define GL_UNSIGNED_INT_10F_11F_11F_REV 0x8C3B
#define GL_INT_2_10_10_10_REV           0x8D9F

/* Primitives */
#define GL_POINTS                       0x0000
#define GL_LINES                        0x0001
#define GL_LINE_LOOP                    0x0002
#define GL_LINE_STRIP                   0x0003
#define GL_TRIANGLES                    0x0004
#define GL_TRIANGLE_STRIP               0x0005
#define GL_TRIANGLE_FAN                 0x0006
#define GL_QUADS                        0x0007
#define GL_QUAD_STRIP                   0x0008
#define GL_POLYGON                      0x0009
#define GL_LINES_ADJACENCY              0x000A
#define GL_LINE_STRIP_ADJACENCY         0x000B
#define GL_TRIANGLES_ADJACENCY          0x000C
#define GL_TRIANGLE_STRIP_ADJACENCY     0x000D
#define GL_PATCHES                      0x000E

/* Clearing / buffers */
#define GL_DEPTH_BUFFER_BIT             0x00000100
#define GL_STENCIL_BUFFER_BIT           0x00000400
#define GL_COLOR_BUFFER_BIT             0x00004000
#define GL_ACCUM_BUFFER_BIT             0x00000200

/* Enable caps */
#define GL_BLEND                        0x0BE2
#define GL_CULL_FACE                    0x0B44
#define GL_DEPTH_TEST                   0x0B71
#define GL_STENCIL_TEST                 0x0B90
#define GL_SCISSOR_TEST                 0x0C11
#define GL_DITHER                       0x0BD0
#define GL_POLYGON_OFFSET_FILL          0x8037
#define GL_POLYGON_OFFSET_LINE          0x2A02
#define GL_POLYGON_OFFSET_POINT         0x2A01
#define GL_SAMPLE_ALPHA_TO_COVERAGE     0x809E
#define GL_SAMPLE_COVERAGE              0x80A0
#define GL_SAMPLE_ALPHA_TO_ONE          0x809F
#define GL_PRIMITIVE_RESTART            0x8F9D
#define GL_PRIMITIVE_RESTART_FIXED_INDEX 0x8D69
#define GL_RASTERIZER_DISCARD           0x8C89
#define GL_FRAMEBUFFER_SRGB             0x8DB9
#define GL_LINE_SMOOTH                  0x0B20
#define GL_PROGRAM_POINT_SIZE           0x8642
#define GL_MULTISAMPLE                  0x809D
#define GL_TEXTURE_CUBE_MAP_SEAMLESS    0x884F

/* Front face / cull */
#define GL_CW                           0x0900
#define GL_CCW                          0x0901
#define GL_FRONT                        0x0404
#define GL_BACK                         0x0405
#define GL_FRONT_AND_BACK               0x0408

/* Polygon mode */
#define GL_POINT                        0x1B00
#define GL_LINE                         0x1B01
#define GL_FILL                         0x1B02

/* Depth */
#define GL_NEVER                        0x0200
#define GL_LESS                         0x0201
#define GL_EQUAL                        0x0202
#define GL_LEQUAL                       0x0203
#define GL_GREATER                      0x0204
#define GL_NOTEQUAL                     0x0205
#define GL_GEQUAL                       0x0206
#define GL_ALWAYS                       0x0207
#define GL_DEPTH_TEST                   0x0B71
#define GL_DEPTH_WRITEMASK              0x0B72
#define GL_DEPTH_FUNC                   0x0B73
#define GL_DEPTH_RANGE                  0x0B70
#define GL_DEPTH_CLEAR_VALUE            0x0B73

/* Blend */
#define GL_SRC_ALPHA                    0x0302
#define GL_ONE_MINUS_SRC_ALPHA          0x0303
#define GL_DST_ALPHA                    0x0304
#define GL_ONE_MINUS_DST_ALPHA          0x0305
#define GL_SRC_COLOR                    0x0300
#define GL_ONE_MINUS_SRC_COLOR          0x0301
#define GL_DST_COLOR                    0x0306
#define GL_ONE_MINUS_DST_COLOR          0x0307
#define GL_CONSTANT_COLOR               0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR     0x8002
#define GL_CONSTANT_ALPHA               0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA     0x8004
#define GL_SRC_ALPHA_SATURATE           0x0308
#define GL_SRC1_ALPHA                   0x8589
#define GL_SRC1_COLOR                   0x88F9
#define GL_ONE_MINUS_SRC1_COLOR         0x88FA
#define GL_ONE_MINUS_SRC1_ALPHA         0x88FB
#define GL_BLEND_COLOR                  0x8005
#define GL_BLEND_DST_RGB                0x80C8
#define GL_BLEND_SRC_RGB                0x80C9
#define GL_BLEND_DST_ALPHA              0x80CA
#define GL_BLEND_SRC_ALPHA              0x80CB
#define GL_BLEND_EQUATION_RGB           0x8009
#define GL_BLEND_EQUATION_ALPHA         0x883D
#define GL_BLEND_EQUATION               0x8009
#define GL_FUNC_ADD                     0x8006
#define GL_FUNC_SUBTRACT                0x800A
#define GL_FUNC_REVERSE_SUBTRACT        0x800B
#define GL_MIN                          0x8007
#define GL_MAX                          0x8008

/* Stencil */
#define GL_STENCIL_WRITEMASK            0x0D98
#define GL_STENCIL_BACK_WRITEMASK       0x8C80
#define GL_STENCIL_FUNC                 0x0B92
#define GL_STENCIL_VALUE_MASK           0x0B93
#define GL_STENCIL_REF                  0x0B97
#define GL_STENCIL_FAIL                 0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL      0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS      0x0B96
#define GL_STENCIL_BACK_FUNC            0x8800
#define GL_STENCIL_BACK_VALUE_MASK      0x8CA4
#define GL_STENCIL_BACK_REF             0x8CA3
#define GL_STENCIL_BACK_FAIL            0x8801
#define GL_STENCIL_BACK_PASS_DEPTH_FAIL 0x8802
#define GL_STENCIL_BACK_PASS_DEPTH_PASS 0x8803
#define GL_STENCIL_INDEX                0x1901
#define GL_STENCIL_INDEX8               0x8D48
#define GL_KEEP                         0x1E00
#define GL_REPLACE                      0x1E01
#define GL_INCR                         0x1E02
#define GL_DECR                         0x1E03
#define GL_INVERT                       0x150A
#define GL_INCR_WRAP                    0x8507
#define GL_DECR_WRAP                    0x8508

/* Texture targets */
#define GL_TEXTURE_1D                   0x0DE0
#define GL_TEXTURE_2D                   0x0DE1
#define GL_TEXTURE_3D                   0x806F
#define GL_TEXTURE_1D_ARRAY             0x8C18
#define GL_TEXTURE_2D_ARRAY             0x8C1A
#define GL_TEXTURE_RECTANGLE            0x84F5
#define GL_TEXTURE_CUBE_MAP             0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X  0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X  0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y  0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y  0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z  0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z  0x851A
#define GL_TEXTURE_BUFFER               0x8C2A
#define GL_TEXTURE_2D_MULTISAMPLE       0x9100
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102

/* Texture params */
#define GL_TEXTURE_MIN_FILTER           0x2801
#define GL_TEXTURE_MAG_FILTER           0x2800
#define GL_TEXTURE_WRAP_S               0x2802
#define GL_TEXTURE_WRAP_T               0x2803
#define GL_TEXTURE_WRAP_R               0x2807
#define GL_TEXTURE_BORDER_COLOR         0x1004
#define GL_TEXTURE_MIN_LOD              0x813A
#define GL_TEXTURE_MAX_LOD              0x813B
#define GL_TEXTURE_BASE_LEVEL           0x813C
#define GL_TEXTURE_MAX_LEVEL            0x813D
#define GL_TEXTURE_COMPARE_MODE         0x884C
#define GL_TEXTURE_COMPARE_FUNC         0x884D
#define GL_TEXTURE_SWIZZLE_R            0x8E42
#define GL_TEXTURE_SWIZZLE_G            0x8E43
#define GL_TEXTURE_SWIZZLE_B            0x8E44
#define GL_TEXTURE_SWIZZLE_A            0x8E45
#define GL_NEAREST                      0x2600
#define GL_LINEAR                       0x2601
#define GL_NEAREST_MIPMAP_NEAREST       0x2700
#define GL_LINEAR_MIPMAP_NEAREST        0x2701
#define GL_NEAREST_MIPMAP_LINEAR        0x2702
#define GL_LINEAR_MIPMAP_LINEAR         0x2703
#define GL_CLAMP_TO_EDGE                0x812F
#define GL_CLAMP_TO_BORDER              0x812D
#define GL_MIRRORED_REPEAT              0x8370
#define GL_REPEAT                       0x2901
#define GL_MIRROR_CLAMP_TO_EDGE         0x8743

/* Pixel formats / types */
#define GL_RED                          0x1903
#define GL_GREEN                        0x1904
#define GL_BLUE                         0x1905
#define GL_ALPHA                        0x1906
#define GL_RGB                          0x1907
#define GL_RGBA                         0x1908
#define GL_LUMINANCE                    0x1909
#define GL_LUMINANCE_ALPHA              0x190A
#define GL_BGR                          0x80E0
#define GL_BGRA                         0x80E1
#define GL_RED_INTEGER                  0x8D94
#define GL_RGB_INTEGER                  0x8D98
#define GL_RGBA_INTEGER                 0x8D99
#define GL_RG_INTEGER                   0x8228
#define GL_RG                           0x8227
#define GL_DEPTH_COMPONENT              0x1902
#define GL_DEPTH_STENCIL                0x84F9
#define GL_STENCIL_INDEX                0x1901
#define GL_R8                           0x8229
#define GL_R8_SNORM                     0x8F94
#define GL_R16                          0x822A
#define GL_R16F                         0x822D
#define GL_R32F                         0x822E
#define GL_R8I                          0x8231
#define GL_R8UI                         0x8232
#define GL_R16I                         0x8233
#define GL_R16UI                        0x8234
#define GL_R32I                         0x8235
#define GL_R32UI                        0x8236
#define GL_RG8                          0x822B
#define GL_RG8_SNORM                    0x8F95
#define GL_RG16F                        0x822F
#define GL_RG32F                        0x8230
#define GL_RGB8                         0x8051
#define GL_RGB8_SNORM                   0x8F96
#define GL_RGBA8                        0x8058
#define GL_RGBA8_SNORM                  0x8F97
#define GL_SRGB8                        0x8C41
#define GL_SRGB8_ALPHA8                 0x8C43
#define GL_RGBA16F                      0x881A
#define GL_RGB16F                       0x881B
#define GL_RGBA32F                      0x8814
#define GL_RGB32F                       0x8815
#define GL_R11F_G11F_B10F               0x8C3A
#define GL_RGB10_A2                     0x8059
#define GL_RGB10_A2UI                   0x906F
/* Additional sized internal formats used by MC Java (glcorearb.h omits these
 * legacy/ES-style 16-bit packed formats and the RGBA16 unorm format). */
#define GL_RGBA4                        0x8056
#define GL_RGB5_A1                      0x8057
#define GL_RGB565                       0x8D62
#define GL_RGBA16                       0x805B
/* Integer internal formats (sized) */
#define GL_RG8I                         0x8237
#define GL_RG8UI                        0x8238
#define GL_RG16I                        0x8239
#define GL_RG16UI                       0x823A
#define GL_RGB8I                        0x8D8F
#define GL_RGB8UI                       0x8D7D
#define GL_RGB16UI                      0x8D77
#define GL_RGBA8I                       0x8D8E
#define GL_RGBA8UI                      0x8D7C
#define GL_RGBA16I                      0x8D88
#define GL_RGBA16UI                     0x8D76
#define GL_RGB32UI                      0x8D71
#define GL_RGB32I                       0x8D83
#define GL_RGBA32I                      0x8D82
#define GL_RGBA32UI                     0x8D70
#define GL_DEPTH_COMPONENT16            0x81A5
#define GL_DEPTH_COMPONENT24            0x81A6
#define GL_DEPTH_COMPONENT32            0x81A7
#define GL_DEPTH_COMPONENT32F           0x8CAC
#define GL_DEPTH24_STENCIL8             0x88F0
#define GL_DEPTH32F_STENCIL8            0x8CAD
#define GL_UNSIGNED_SHORT_5_6_5         0x8363
#define GL_UNSIGNED_SHORT_4_4_4_4       0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1       0x8034
#define GL_UNSIGNED_INT_24_8            0x84FA
#define GL_FLOAT_32_UNSIGNED_INT_24_8_REV 0x8DAD
/* Packed pixel types used by MC Java's texture upload paths. */
#define GL_UNSIGNED_INT_8_8_8_8         0x8035
#define GL_UNSIGNED_INT_8_8_8_8_REV     0x8367
/* EXT_texture_compression_s3tc — BC1/BC2/BC3 block-compressed formats. MC Java
 * ships DXT-compressed GUI atlases; these are the GL tokens the loader passes
 * to glCompressedTexImage2D / glInternalFormat. */
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

/* Pixel transfer / pack */
#define GL_UNPACK_ALIGNMENT             0x0CF5
#define GL_PACK_ALIGNMENT               0x0D05
#define GL_UNPACK_ROW_LENGTH            0x0CF2
#define GL_UNPACK_SKIP_ROWS             0x0CF3
#define GL_UNPACK_SKIP_PIXELS           0x0CF4
#define GL_UNPACK_SKIP_IMAGES           0x806D
#define GL_UNPACK_IMAGE_HEIGHT          0x806E
#define GL_PACK_ROW_LENGTH              0x0D02
#define GL_PACK_SKIP_ROWS               0x0D03
#define GL_PACK_SKIP_PIXELS             0x0D04

/* Buffer targets / usage */
#define GL_ARRAY_BUFFER                 0x8892
#define GL_ELEMENT_ARRAY_BUFFER         0x8893
#define GL_PIXEL_PACK_BUFFER            0x88EB
#define GL_PIXEL_UNPACK_BUFFER          0x88EC
#define GL_UNIFORM_BUFFER               0x8A11
#define GL_TEXTURE_BUFFER               0x8C2A
#define GL_TRANSFORM_FEEDBACK_BUFFER    0x8C8E
#define GL_COPY_READ_BUFFER             0x8F36
#define GL_COPY_WRITE_BUFFER            0x8F37
#define GL_DRAW_INDIRECT_BUFFER         0x8F3F
#define GL_SHADER_STORAGE_BUFFER        0x90D2
#define GL_ATOMIC_COUNTER_BUFFER        0x92C0
#define GL_STREAM_DRAW                  0x88E0
#define GL_STREAM_READ                  0x88E1
#define GL_STREAM_COPY                  0x88E2
#define GL_STATIC_DRAW                  0x88E4
#define GL_STATIC_READ                  0x88E5
#define GL_STATIC_COPY                  0x88E6
#define GL_DYNAMIC_DRAW                 0x88E8
#define GL_DYNAMIC_READ                 0x88E9
#define GL_DYNAMIC_COPY                 0x88EA
#define GL_BUFFER_SIZE                  0x8764
#define GL_BUFFER_USAGE                 0x8765
#define GL_BUFFER_ACCESS                0x8824

/* Map */
#define GL_READ_ONLY                    0x88B8
#define GL_WRITE_ONLY                   0x88B9
#define GL_READ_WRITE                   0x88BA
#define GL_MAP_READ_BIT                 0x0001
#define GL_MAP_WRITE_BIT                0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT     0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT    0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT       0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT       0x0020

/* Framebuffer */
#define GL_FRAMEBUFFER                  0x8D40
#define GL_READ_FRAMEBUFFER             0x8CA8
#define GL_DRAW_FRAMEBUFFER             0x8CA9
#define GL_RENDERBUFFER                 0x8D41
#define GL_DRAW_BUFFER                  0x0C01
#define GL_DRAW_BUFFER0                 0x8825
#define GL_DRAW_BUFFER1                 0x8826
#define GL_DRAW_BUFFER2                 0x8827
#define GL_DRAW_BUFFER3                 0x8828
#define GL_DRAW_BUFFER4                 0x8829
#define GL_DRAW_BUFFER5                 0x882A
#define GL_DRAW_BUFFER6                 0x882B
#define GL_DRAW_BUFFER7                 0x882C
#define GL_READ_BUFFER                  0x0C02
#define GL_COLOR_ATTACHMENT0            0x8CE0
#define GL_COLOR_ATTACHMENT1            0x8CE1
#define GL_COLOR_ATTACHMENT2            0x8CE2
#define GL_COLOR_ATTACHMENT3            0x8CE3
#define GL_COLOR_ATTACHMENT4            0x8CE4
#define GL_COLOR_ATTACHMENT5            0x8CE5
#define GL_COLOR_ATTACHMENT6            0x8CE6
#define GL_COLOR_ATTACHMENT7            0x8CE7
#define GL_DEPTH_ATTACHMENT             0x8D00
#define GL_STENCIL_ATTACHMENT           0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT     0x821A
#define GL_FRAMEBUFFER_COMPLETE         0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_UNSUPPORTED      0x8CDD
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER 0x8CDC
#define GL_MAX_COLOR_ATTACHMENTS        0x8CDF
#define GL_MAX_DRAW_BUFFERS             0x8824

/* Shader / program */
#define GL_VERTEX_SHADER                0x8B31
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_GEOMETRY_SHADER              0x8DD9
#define GL_TESS_CONTROL_SHADER          0x8E88
#define GL_TESS_EVALUATION_SHADER       0x8E87
#define GL_COMPUTE_SHADER               0x91B9
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_VALIDATE_STATUS              0x8B83
#define GL_SHADER_TYPE                  0x8B4F
#define GL_INFO_LOG_LENGTH              0x8B84
#define GL_SHADER_SOURCE_LENGTH         0x8B88
#define GL_SHADER_COMPILER              0x8DFA
#define GL_ATTACHED_SHADERS             0x8B85
#define GL_ACTIVE_UNIFORMS              0x8B86
#define GL_ACTIVE_ATTRIBUTES            0x8B89
#define GL_ACTIVE_UNIFORM_MAX_LENGTH    0x8B87
#define GL_ACTIVE_ATTRIBUTE_MAX_LENGTH  0x8B8A
#define GL_UNIFORM_SIZE                 0x8A38
#define GL_UNIFORM_NAME_LENGTH          0x8A39
#define GL_UNIFORM_BLOCK_INDEX          0x8A3A
#define GL_UNIFORM_OFFSET               0x8A3B
#define GL_UNIFORM_ARRAY_STRIDE         0x8A3C
#define GL_UNIFORM_MATRIX_STRIDE        0x8A3D
#define GL_UNIFORM_IS_ROW_MAJOR         0x8A3E
#define GL_UNIFORM_ATOMIC_COUNTER_BUFFER_INDEX 0x92DA
#define GL_UNIFORM_BLOCK_NAME_LENGTH    0x8A41
#define GL_UNIFORM_BLOCK_DATA_SIZE      0x8A40
#define GL_UNIFORM_BLOCK_BINDING        0x8A3F
#define GL_UNIFORM_BUFFER_BINDING       0x8A28
#define GL_MAX_UNIFORM_BLOCK_SIZE       0x8A30
#define GL_MAX_VERTEX_UNIFORM_BLOCKS    0x8A2B
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS  0x8A2D
#define GL_INVALID_OPERATION            0x0502
#define GL_CURRENT_PROGRAM              0x8B8D

/* Get pnames */
#define GL_MAX_TEXTURE_SIZE             0x0D33
#define GL_MAX_TEXTURE_IMAGE_UNITS      0x8872
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_VERTEX_ATTRIBS           0x8869
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B18
#define GL_MAX_VIEWPORT_DIMS            0x0D3A
#define GL_MAX_3D_TEXTURE_SIZE          0x8073
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE    0x851C
#define GL_MAX_ARRAY_TEXTURE_LAYERS     0x88FF
#define GL_MAX_RENDERBUFFER_SIZE        0x84E8
#define GL_MAX_ELEMENTS_VERTICES        0x80E8
#define GL_MAX_ELEMENTS_INDICES         0x80E9
#define GL_SUBPIXEL_BITS                0x0D50
#define GL_RED_BITS                     0x0D52
#define GL_GREEN_BITS                   0x0D53
#define GL_BLUE_BITS                    0x0D54
#define GL_ALPHA_BITS                   0x0D55
#define GL_DEPTH_BITS                   0x0D56
#define GL_STENCIL_BITS                 0x0D57
#define GL_NUM_EXTENSIONS               0x821D
#define GL_MAJOR_VERSION                0x821B
#define GL_MINOR_VERSION                0x821C
#define GL_CONTEXT_FLAGS                0x821E
#define GL_CONTEXT_PROFILE_MASK         0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT     0x00000001
#define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT 0x00000002
#define GL_DOUBLEBUFFER                 0x0C32
#define GL_STEREO                       0x0C33
#define GL_VENDOR                       0x1F00
#define GL_RENDERER                     0x1F01
#define GL_VERSION                      0x1F02
#define GL_EXTENSIONS                   0x1F03
#define GL_SHADING_LANGUAGE_VERSION     0x8B8C
#define GL_ARRAY_BUFFER_BINDING         0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_VERTEX_ARRAY_BINDING         0x85B5
#define GL_CURRENT_TEXTURE_COORDS       0x0B03
#define GL_TEXTURE_BINDING_2D           0x8069
#define GL_TEXTURE_BINDING_3D           0x806A
#define GL_TEXTURE_BINDING_CUBE_MAP     0x8514
#define GL_TEXTURE_BINDING_2D_ARRAY     0x8C1D
#define GL_DRAW_FRAMEBUFFER_BINDING     0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING     0x8CAA
#define GL_FRAMEBUFFER_BINDING          0x8CA6
#define GL_RENDERBUFFER_BINDING         0x8CA7
#define GL_ACTIVE_TEXTURE               0x84E0
#define GL_MAX_TEXTURE_UNITS            0x84E2
#define GL_VIEWPORT                     0x0BA2
#define GL_SCISSOR_BOX                  0x0C10
#define GL_COLOR_CLEAR_VALUE            0x0C22
#define GL_COLOR_WRITEMASK              0x0C23
#define GL_CURRENT_COLOR                0x0B00
#define GL_CURRENT_NORMAL               0x0B02
#define GL_LINE_WIDTH                   0x0B21
#define GL_POINT_SIZE                   0x0B11
#define GL_POLYGON_MODE                 0x0B40
#define GL_CULL_FACE_MODE               0x0B45
#define GL_FRONT_FACE                   0x0B46
#define GL_POLYGON_OFFSET_FACTOR        0x8038
#define GL_POLYGON_OFFSET_UNITS         0x2A00

/* Hints */
#define GL_DONT_CARE                    0x1100
#define GL_FASTEST                      0x1101
#define GL_NICEST                       0x1102
#define GL_PERSPECTIVE_CORRECTION_HINT  0x0C50
#define GL_LINE_SMOOTH_HINT             0x0C52
#define GL_POLYGON_SMOOTH_HINT          0x0C53
#define GL_TEXTURE_COMPRESSION_HINT     0x84EF
#define GL_GENERATE_MIPMAP_HINT         0x8192

/* Errors */
#define GL_NO_ERROR                     0x0000
#define GL_INVALID_ENUM                 0x0500
#define GL_INVALID_VALUE                0x0501
#define GL_INVALID_OPERATION            0x0502
#define GL_STACK_OVERFLOW               0x0503
#define GL_STACK_UNDERFLOW              0x0504
#define GL_OUT_OF_MEMORY                0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

/* Sync */
#define GL_ALREADY_SIGNALED             0x911A
#define GL_TIMEOUT_EXPIRED              0x911B
#define GL_CONDITION_SATISFIED          0x911C
#define GL_WAIT_FAILED                  0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT      0x00000001
#define GL_TIMEOUT_IGNORED              ((GLuint64)-1)
#define GL_FENCE_CONDITION              0x1184

#define GL_TEXTURE0                     0x84C0
#define GL_TEXTURE1                     0x84C1
#define GL_TEXTURE2                     0x84C2
#define GL_TEXTURE3                     0x84C3
#define GL_TEXTURE4                     0x84C4
#define GL_TEXTURE5                     0x84C5
#define GL_TEXTURE6                     0x84C6
#define GL_TEXTURE7                     0x84C7

/* KHR_debug — advertised via GL_KHR_debug in the extension string, so the
 * corresponding entry points must resolve via dlsym. LWJGL probes for
 * glDebugMessageControl during GLX._init; if it returns NULL the
 * org.lwjgl.system.Checks.check() guard throws NullPointerException and
 * crashes Minecraft at startup. These no-op stubs keep the surface alive. */
#define GL_DEBUG_OUTPUT                 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS     0x8242
#define GL_DEBUG_NEXT_TRIGGER           0x8243
#define GL_DEBUG_CALLBACK_FUNCTION      0x8244
#define GL_DEBUG_CALLBACK_USER_PARAM    0x8245
#define GL_DEBUG_SOURCE_API             0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM   0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY    0x8249
#define GL_DEBUG_SOURCE_APPLICATION    0x824A
#define GL_DEBUG_SOURCE_OTHER          0x824B
#define GL_DEBUG_TYPE_ERROR             0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#define GL_DEBUG_TYPE_PORTABILITY       0x824F
#define GL_DEBUG_TYPE_PERFORMANCE        0x8250
#define GL_DEBUG_TYPE_OTHER             0x8251
#define GL_DEBUG_TYPE_MARKER            0x8268
#define GL_DEBUG_TYPE_PUSH_GROUP        0x8269
#define GL_DEBUG_TYPE_POP_GROUP         0x826A
#define GL_DEBUG_SEVERITY_HIGH          0x9146
#define GL_DEBUG_SEVERITY_MEDIUM        0x9147
#define GL_DEBUG_SEVERITY_LOW           0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION  0x826B
#define GL_MAX_DEBUG_MESSAGE_LENGTH     0x9143
#define GL_MAX_DEBUG_GROUP_STACK_DEPTH  0x826C
#define GL_DEBUG_GROUP_STACK_DEPTH      0x826D

/* ----------------------------- API ----------------------------- */

GLAPI void GLAPIENTRY glClear(GLbitfield mask);
GLAPI void GLAPIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
GLAPI void GLAPIENTRY glClearDepth(GLclampd depth);
GLAPI void GLAPIENTRY glClearDepthf(GLclampf depth);
GLAPI void GLAPIENTRY glClearStencil(GLint s);

GLAPI void GLAPIENTRY glEnable(GLenum cap);
GLAPI void GLAPIENTRY glDisable(GLenum cap);
GLAPI GLboolean GLAPIENTRY glIsEnabled(GLenum cap);
GLAPI void GLAPIENTRY glEnablei(GLenum cap, GLuint index);
GLAPI void GLAPIENTRY glDisablei(GLenum cap, GLuint index);
GLAPI GLboolean GLAPIENTRY glIsEnabledi(GLenum cap, GLuint index);

GLAPI void GLAPIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
GLAPI void GLAPIENTRY glDepthRange(GLclampd near_val, GLclampd far_val);
GLAPI void GLAPIENTRY glDepthRangef(GLclampf near_val, GLclampf far_val);
GLAPI void GLAPIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height);

GLAPI void GLAPIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor);
GLAPI void GLAPIENTRY glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
GLAPI void GLAPIENTRY glBlendEquation(GLenum mode);
GLAPI void GLAPIENTRY glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
GLAPI void GLAPIENTRY glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
GLAPI void GLAPIENTRY glBlendFunci(GLuint buf, GLenum src, GLenum dst);
GLAPI void GLAPIENTRY glBlendFuncSeparatei(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA);
GLAPI void GLAPIENTRY glBlendEquationi(GLuint buf, GLenum mode);

GLAPI void GLAPIENTRY glDepthFunc(GLenum func);
GLAPI void GLAPIENTRY glDepthMask(GLboolean flag);
GLAPI void GLAPIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
GLAPI void GLAPIENTRY glStencilMask(GLuint mask);
GLAPI void GLAPIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask);
GLAPI void GLAPIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
GLAPI void GLAPIENTRY glStencilMaskSeparate(GLenum face, GLuint mask);
GLAPI void GLAPIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
GLAPI void GLAPIENTRY glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);

GLAPI void GLAPIENTRY glCullFace(GLenum mode);
GLAPI void GLAPIENTRY glFrontFace(GLenum mode);
GLAPI void GLAPIENTRY glPolygonMode(GLenum face, GLenum mode);
GLAPI void GLAPIENTRY glPolygonOffset(GLfloat factor, GLfloat units);
GLAPI void GLAPIENTRY glLineWidth(GLfloat width);
GLAPI void GLAPIENTRY glPointSize(GLfloat size);
GLAPI void GLAPIENTRY glHint(GLenum target, GLenum mode);

GLAPI void GLAPIENTRY glPixelStorei(GLenum pname, GLint param);
GLAPI void GLAPIENTRY glPixelStoref(GLenum pname, GLfloat param);

GLAPI void GLAPIENTRY glActiveTexture(GLenum texture);

/* Buffers */
GLAPI void GLAPIENTRY glGenBuffers(GLsizei n, GLuint* buffers);
GLAPI void GLAPIENTRY glDeleteBuffers(GLsizei n, const GLuint* buffers);
GLAPI void GLAPIENTRY glBindBuffer(GLenum target, GLuint buffer);
GLAPI void GLAPIENTRY glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
GLAPI void GLAPIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
GLAPI void GLAPIENTRY glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
GLAPI void* GLAPIENTRY glMapBuffer(GLenum target, GLenum access);
GLAPI void* GLAPIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
GLAPI GLboolean GLAPIENTRY glUnmapBuffer(GLenum target);
GLAPI void GLAPIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length);
GLAPI void GLAPIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data);
GLAPI void GLAPIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
GLAPI void GLAPIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);

/* Vertex arrays */
GLAPI void GLAPIENTRY glGenVertexArrays(GLsizei n, GLuint* arrays);
GLAPI void GLAPIENTRY glDeleteVertexArrays(GLsizei n, const GLuint* arrays);
GLAPI void GLAPIENTRY glBindVertexArray(GLuint array);
GLAPI void GLAPIENTRY glEnableVertexAttribArray(GLuint index);
GLAPI void GLAPIENTRY glDisableVertexAttribArray(GLuint index);
GLAPI void GLAPIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);
GLAPI void GLAPIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor);
GLAPI void GLAPIENTRY glVertexAttrib1f(GLuint index, GLfloat x);
GLAPI void GLAPIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
GLAPI void GLAPIENTRY glVertexAttrib4fv(GLuint index, const GLfloat* v);

/* Textures */
GLAPI void GLAPIENTRY glGenTextures(GLsizei n, GLuint* textures);
GLAPI void GLAPIENTRY glDeleteTextures(GLsizei n, const GLuint* textures);
GLAPI void GLAPIENTRY glBindTexture(GLenum target, GLuint texture);
GLAPI void GLAPIENTRY glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
GLAPI void GLAPIENTRY glTexImage3D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void* pixels);
GLAPI void GLAPIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
GLAPI void GLAPIENTRY glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels);
GLAPI void GLAPIENTRY glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
GLAPI void GLAPIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height);
GLAPI void GLAPIENTRY glTexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth);
GLAPI void GLAPIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param);
GLAPI void GLAPIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param);
GLAPI void GLAPIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params);
GLAPI void GLAPIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint* params);
GLAPI void GLAPIENTRY glGenerateMipmap(GLenum target);
GLAPI void GLAPIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels);
GLAPI void GLAPIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
GLAPI void GLAPIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);

/* Framebuffers / renderbuffers */
GLAPI void GLAPIENTRY glGenFramebuffers(GLsizei n, GLuint* framebuffers);
GLAPI void GLAPIENTRY glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
GLAPI void GLAPIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer);
GLAPI void GLAPIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
GLAPI void GLAPIENTRY glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
GLAPI void GLAPIENTRY glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level);
GLAPI void GLAPIENTRY glDrawBuffers(GLsizei n, const GLenum* bufs);
GLAPI void GLAPIENTRY glDrawBuffer(GLenum mode);
GLAPI void GLAPIENTRY glReadBuffer(GLenum mode);
GLAPI GLenum GLAPIENTRY glCheckFramebufferStatus(GLenum target);
GLAPI void GLAPIENTRY glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
GLAPI void GLAPIENTRY glGenRenderbuffers(GLsizei n, GLuint* renderbuffers);
GLAPI void GLAPIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers);
GLAPI void GLAPIENTRY glBindRenderbuffer(GLenum target, GLuint renderbuffer);
GLAPI void GLAPIENTRY glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
GLAPI void GLAPIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
GLAPI void GLAPIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);

/* Program / shader */
GLAPI GLuint GLAPIENTRY glCreateProgram(void);
GLAPI void GLAPIENTRY glDeleteProgram(GLuint program);
GLAPI GLuint GLAPIENTRY glCreateShader(GLenum type);
GLAPI void GLAPIENTRY glDeleteShader(GLuint shader);
GLAPI void GLAPIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
GLAPI void GLAPIENTRY glShaderBinary(GLsizei count, const GLuint* shaders, GLenum binaryformat, const void* binary, GLsizei length);
GLAPI void GLAPIENTRY glCompileShader(GLuint shader);
GLAPI void GLAPIENTRY glReleaseShaderCompiler(void);
GLAPI void GLAPIENTRY glAttachShader(GLuint program, GLuint shader);
GLAPI void GLAPIENTRY glDetachShader(GLuint program, GLuint shader);
GLAPI void GLAPIENTRY glLinkProgram(GLuint program);
GLAPI void GLAPIENTRY glUseProgram(GLuint program);
GLAPI void GLAPIENTRY glValidateProgram(GLuint program);
GLAPI void GLAPIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
GLAPI void GLAPIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source);
GLAPI void GLAPIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
GLAPI void GLAPIENTRY glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders);
GLAPI GLint GLAPIENTRY glGetUniformLocation(GLuint program, const GLchar* name);
GLAPI GLint GLAPIENTRY glGetAttribLocation(GLuint program, const GLchar* name);
GLAPI void GLAPIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar* name);
GLAPI void GLAPIENTRY glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name);
GLAPI void GLAPIENTRY glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name);
GLAPI void GLAPIENTRY glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name);
GLAPI void GLAPIENTRY glGetUniformfv(GLuint program, GLint location, GLfloat* params);
GLAPI void GLAPIENTRY glGetUniformiv(GLuint program, GLint location, GLint* params);
GLAPI GLuint GLAPIENTRY glGetUniformBlockIndex(GLuint program, const GLchar* uniformBlockName);
GLAPI void GLAPIENTRY glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);

GLAPI void GLAPIENTRY glUniform1f(GLint location, GLfloat v0);
GLAPI void GLAPIENTRY glUniform2f(GLint location, GLfloat v0, GLfloat v1);
GLAPI void GLAPIENTRY glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
GLAPI void GLAPIENTRY glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
GLAPI void GLAPIENTRY glUniform1i(GLint location, GLint v0);
GLAPI void GLAPIENTRY glUniform2i(GLint location, GLint v0, GLint v1);
GLAPI void GLAPIENTRY glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
GLAPI void GLAPIENTRY glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
GLAPI void GLAPIENTRY glUniform1ui(GLint location, GLuint v0);
GLAPI void GLAPIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1);
GLAPI void GLAPIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2);
GLAPI void GLAPIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3);
GLAPI void GLAPIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat* value);
GLAPI void GLAPIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat* value);
GLAPI void GLAPIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat* value);
GLAPI void GLAPIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat* value);
GLAPI void GLAPIENTRY glUniform1iv(GLint location, GLsizei count, const GLint* value);
GLAPI void GLAPIENTRY glUniform2iv(GLint location, GLsizei count, const GLint* value);
GLAPI void GLAPIENTRY glUniform3iv(GLint location, GLsizei count, const GLint* value);
GLAPI void GLAPIENTRY glUniform4iv(GLint location, GLsizei count, const GLint* value);
GLAPI void GLAPIENTRY glUniform1uiv(GLint location, GLsizei count, const GLuint* value);
GLAPI void GLAPIENTRY glUniform2uiv(GLint location, GLsizei count, const GLuint* value);
GLAPI void GLAPIENTRY glUniform3uiv(GLint location, GLsizei count, const GLuint* value);
GLAPI void GLAPIENTRY glUniform4uiv(GLint location, GLsizei count, const GLuint* value);
GLAPI void GLAPIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
GLAPI void GLAPIENTRY glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

/* Drawing */
GLAPI void GLAPIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count);
GLAPI void GLAPIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount);
GLAPI void GLAPIENTRY glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei primcount, GLuint baseinstance);
GLAPI void GLAPIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices);
GLAPI void GLAPIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex);
GLAPI void GLAPIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount);
GLAPI void GLAPIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount, GLint basevertex);
GLAPI void GLAPIENTRY glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount, GLuint baseinstance);
GLAPI void GLAPIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices);
GLAPI void GLAPIENTRY glDrawElementsBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex, GLuint baseinstance);
GLAPI void GLAPIENTRY glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount);
GLAPI void GLAPIENTRY glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const void* const* indices, GLsizei drawcount);
GLAPI void GLAPIENTRY glPrimitiveRestartIndex(GLuint index);

/* Flush / finish */
GLAPI void GLAPIENTRY glFlush(void);
GLAPI void GLAPIENTRY glFinish(void);

/* Getters / strings */
GLAPI void GLAPIENTRY glGetIntegerv(GLenum pname, GLint* params);
GLAPI void GLAPIENTRY glGetFloatv(GLenum pname, GLfloat* params);
GLAPI void GLAPIENTRY glGetDoublev(GLenum pname, GLdouble* params);
GLAPI void GLAPIENTRY glGetBooleanv(GLenum pname, GLboolean* params);
GLAPI void GLAPIENTRY glGetInteger64v(GLenum pname, GLint64* params);
GLAPI void GLAPIENTRY glGetIntegeri_v(GLenum pname, GLuint index, GLint* params);
GLAPI const GLubyte* GLAPIENTRY glGetString(GLenum name);
GLAPI const GLubyte* GLAPIENTRY glGetStringi(GLenum name, GLuint index);
GLAPI GLenum GLAPIENTRY glGetError(void);

/* Sync */
GLAPI GLsync GLAPIENTRY glFenceSync(GLenum condition, GLbitfield flags);
GLAPI void GLAPIENTRY glDeleteSync(GLsync sync);
GLAPI GLenum GLAPIENTRY glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
GLAPI void GLAPIENTRY glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
GLAPI GLboolean GLAPIENTRY glIsSync(GLsync sync);

/* KHR_debug entry points — no-op stubs, see gl/debug.cpp.
 * Advertised via GL_KHR_debug so hosts (LWJGL) dlsym them during init. */
GLAPI void GLAPIENTRY glDebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled);
GLAPI void GLAPIENTRY glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* buf);
GLAPI void GLAPIENTRY glDebugMessageCallback(GLDEBUGPROC callback, const void* userParam);
GLAPI GLuint GLAPIENTRY glGetDebugMessageLog(GLuint count, GLsizei bufSize, GLenum* sources, GLenum* types, GLuint* ids, GLenum* severities, GLsizei* lengths, GLchar* messageLog);
GLAPI void GLAPIENTRY glPushDebugGroup(GLenum source, GLuint id, GLsizei length, const GLchar* message);
GLAPI void GLAPIENTRY glPopDebugGroup(void);
GLAPI void GLAPIENTRY glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar* label);
GLAPI void GLAPIENTRY glGetObjectLabel(GLenum identifier, GLuint name, GLsizei bufSize, GLsizei* length, GLchar* label);
GLAPI void GLAPIENTRY glObjectPtrLabel(const void* ptr, GLsizei length, const GLchar* label);
GLAPI void GLAPIENTRY glGetObjectPtrLabel(const void* ptr, GLsizei bufSize, GLsizei* length, GLchar* label);

/* glX (symbol lookup) — implemented in MG_Impl/lookup.cpp */
GLAPI void* GLAPIENTRY glXGetProcAddress(const char* name);
GLAPI void* GLAPIENTRY glXGetProcAddressARB(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* __glcorearb_h_ */
