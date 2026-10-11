/* glext.h - the few GL 2.0+ names SRB2's OpenGL renderer compiles against
 * (hardware/r_opengl/r_opengl.c, which loads every function itself by name),
 * in place of Khronos' 850 KB header. Mesa's gl.h, beside it, includes this.
 * The values are Khronos' (glext.h, MIT). */
#ifndef __gl_glext_h_
#define __gl_glext_h_ 1

typedef char GLchar;

#define GL_GENERATE_MIPMAP                0x8191
#define GL_R8                             0x8229
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84

#endif
