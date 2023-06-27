/*
  XU4 OpenGL Renderer
  Copyright 2021 Karl Robillard

  This file is part of XU4.

  XU4 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  XU4 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with XU4.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gpu.h"
#include "tileanim.h"
#include "tileset.h"
#include "tileview.h"

//#include "gpu_opengl.h"

extern "C" {
#include "math3d.c"
}

extern uint32_t getTicks();

#ifdef EMULATE_U4
#include "event.h"
#define MAP_ANIMATOR    &xu4.eventHandler->flourishAnim
#endif

#ifdef ANDROID
#define DVERSION    "#version 310 es\n"
#define PRECISION_F "precision mediump float;\n"
#elif defined(USE_GLES)
#define DVERSION    "#version 310 es\n"
#define PRECISION_F "precision highp float;\n"
#else
#define DVERSION    "#version 330\n"
#define PRECISION_F
#endif

#define LOC_POS     0
#define LOC_UV      1

const char* solid_vertShader =
    DVERSION
    "uniform mat4 transform;\n"
    "layout(location = 0) in vec3 position;\n"
    "void main() {\n"
    "  gl_Position = transform * vec4(position, 1.0);\n"
    "}\n";

const char* solid_fragShader =
    DVERSION PRECISION_F
    "uniform vec4 color;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  fragColor = color;\n"
    "}\n";

const char* cmap_vertShader =
    DVERSION
    "uniform mat4 transform;\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec4 uv;\n"
    "out vec4 texCoord;\n"
    "void main() {\n"
    "  texCoord = uv;\n"
    "  gl_Position = transform * vec4(position, 1.0);\n"
    "}\n";

const char* cmap_fragShader =
    DVERSION PRECISION_F
    "uniform sampler2D cmap;\n"
    "uniform vec4 tint;\n"
    "in vec4 texCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  fragColor = tint * texture(cmap, texCoord.st);\n"
    "}\n";

#define ATTR_COUNT      7
#define ATTR_STRIDE     (sizeof(float) * ATTR_COUNT)
static const float quadAttr[] = {
    // X   Y   Z       U  V  vunit  scrollSourceV
   -1.0,-1.0, 0.0,   0.0, 1.0, 0.0, 0.0,
    1.0,-1.0, 0.0,   1.0, 1.0, 0.0, 0.0,
    1.0, 1.0, 0.0,   1.0, 0.0, 0.0, 0.0,
    1.0, 1.0, 0.0,   1.0, 0.0, 0.0, 0.0,
   -1.0, 1.0, 0.0,   0.0, 0.0, 0.0, 0.0,
   -1.0,-1.0, 0.0,   0.0, 1.0, 0.0, 0.0
};

static const uint8_t whitePixels[] = {
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff
};

#define SHADOW_DIM      512


#ifdef _WIN32
#include "glad.c"
#endif

//#define DEBUG_GL
#ifdef DEBUG_GL
void _debugGL( GLenum source, GLenum type, GLuint id, GLenum severity,
               GLsizei length, const GLchar* message, const void* userParam )
{
    (void) severity;
    (void) length;
    (void) userParam;

    fprintf(stderr, "GL DEBUG %d:%s 0x%x %s\n",
            source,
            (type == GL_DEBUG_TYPE_ERROR) ? " ERROR" : "",
            id, message );
}

static void enableGLDebug()
{
    // Requires GL_KHR_debug extension
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DONT_CARE,
                          GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
    glDebugMessageCallback(_debugGL, NULL);
}
#endif

static void printInfoLog(GLuint obj, int prog)
{
    GLint infologLength;
    GLint charsWritten;
    char* infoLog;

    if( prog )
        glGetProgramiv( obj, GL_INFO_LOG_LENGTH, &infologLength );
    else
        glGetShaderiv( obj, GL_INFO_LOG_LENGTH, &infologLength );

    if( infologLength > 0 ) {
        infoLog = (char*) malloc( infologLength );

        if( prog )
            glGetProgramInfoLog( obj, infologLength, &charsWritten, infoLog );
        else
            glGetShaderInfoLog( obj, infologLength, &charsWritten, infoLog );

        fprintf(stderr, "%s\n", infoLog);
        free( infoLog );
    } else {
        fprintf(stderr, "%s failed\n", prog ? "glLinkProgram"
                                            : "glCompileShader");
    }
}

/*
 * Returns zero on success or 1-3 to indicate compile/link error.
 */
static int compileShaderParts(GLuint program, const char** src, int vcount,
                              int fcount)
{
    GLint ok;
    GLuint vobj = glCreateShader(GL_VERTEX_SHADER);
    GLuint fobj = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vobj, vcount, (const GLchar**) src, NULL);
    glCompileShader(vobj);
    glGetShaderiv(vobj, GL_COMPILE_STATUS, &ok);
    if (! ok) {
        printInfoLog(vobj, 0);
        return 1;
    }
    src += vcount;

    glShaderSource(fobj, fcount, (const GLchar**) src, NULL);
    glCompileShader(fobj);
    glGetShaderiv(fobj, GL_COMPILE_STATUS, &ok);
    if (! ok) {
        printInfoLog(fobj, 0);
        return 2;
    }

    glAttachShader(program, vobj);
    glAttachShader(program, fobj);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (! ok)
        printInfoLog(program, 1);

    // These will actually go away when the program is deleted.
    glDeleteShader(vobj);
    glDeleteShader(fobj);

    return ok ? 0 : 3;
}

static int compileShaders(GLuint program, const char* vert, const char* frag)
{
    const char* src[2];
    src[0] = vert;
    src[1] = frag;
    return compileShaderParts(program, src, 1, 1);
}

static char* readShader(const char* filename)
{
#ifdef CONF_MODULE
    const CDIEntry* ent = xu4.config->fileEntry(filename);
    if (! ent)
        return NULL;

    char* buf = (char*) malloc(ent->bytes + 1);
    if (buf) {
        FILE* fp = fopen(xu4.config->modulePath(ent), "rb");
        if (fp) {
            fseek(fp, ent->offset, SEEK_SET);

            size_t len = fread(buf, 1, ent->bytes, fp);
            fclose(fp);
            if (len == ent->bytes) {
                buf[len] = '\0';
                return buf;
            }
        }
        free(buf);
    }
#else
    char fnBuf[40];
    const size_t bsize = 4096;
    char* buf = (char*) malloc(bsize);
    if (buf) {
        strcpy(fnBuf, "graphics/shader/");
        strcat(fnBuf, filename);

        FILE* fp = fopen(fnBuf, "rb");
        if (fp) {
            size_t len = fread(buf, 1, bsize-1, fp);
            fclose(fp);
            if (len > 16) {
                buf[len] = '\0';
                return buf;
            }
        }
        free(buf);
    }
#endif
    return NULL;
}

/*
 * Returns zero on success or 1-4 to indicate compile/link/read error.
 */
static int compileSLFile(GLuint program, const char* filename, int scale)
{
    const char* src[4];
    int res = 4;
    char* buf = readShader(filename);

    if (buf) {
        if (scale > 2) {
            char* spos = strstr(buf, "SCALE 2");
            if (spos)
                spos[6] = '0' + scale;
        }

        src[0] = DVERSION "#define VERTEX\n";
        src[1] = buf;
        src[2] = DVERSION PRECISION_F "#define FRAGMENT\n";
        src[3] = buf;

        res = compileShaderParts(program, src, 2, 2);
        free(buf);
    }
    return res;
}

static void _defineAttributeLayout(GLuint vao, GLuint vbo)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(LOC_POS);
    glVertexAttribPointer(LOC_POS, 3, GL_FLOAT, GL_FALSE, ATTR_STRIDE, 0);
    glEnableVertexAttribArray(LOC_UV);
    glVertexAttribPointer(LOC_UV,  4, GL_FLOAT, GL_FALSE, ATTR_STRIDE,
                          (const GLvoid*) 12);
}

#ifdef GPU_RENDER
static GLuint _makeFramebuffer(GLuint texId)
{
    GLuint fbo;
    GLenum status;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texId, 0);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer invalid: 0x%04X\n", status);
        return 0;
    }
    return fbo;
}
#endif

/*
 * Define 2D texture storage.
 *
 * \param data  Pointer to RGBA uint8_t pixels or NULL.
 */
static void gpu_defineTex(GLuint tex, int w, int h, const void* data,
                          GLint internalFormat, GLenum filter)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

extern Image* loadImage_png(U4FILE *file);

/*
 * \param useTex    Texture to define, or zero to generate a new texture name.
 *
 * \return Texture name or zero if loading failed.
 */
static GLuint loadTexture(const char* file, GLuint useTex, GLenum filter,
                          int* texSize)
{
    GLuint texId = 0;
    const CDIEntry* ent = xu4.config->fileEntry(file);
    if (ent) {
        U4FILE* uf = u4fopen_stdio(xu4.config->modulePath(ent));
        if (uf) {
            u4fseek(uf, ent->offset, SEEK_SET);
            Image* img = loadImage_png(uf);
            u4fclose(uf);
            if (img) {
                if (! useTex)
                    glGenTextures(1, &useTex);
                //printf("KR loadTexture %s %d,%d\n", file, img->w, img->h);
                gpu_defineTex(useTex, img->w, img->h, img->pixels,
                              GL_RGBA, filter);
                texId = useTex;
                if (texSize) {
                    texSize[0] = img->w;
                    texSize[1] = img->h;
                }
                delete img;
            }
        }
    }
    return texId;
}

static GLuint loadHQXTableImage(int scale)
{
#ifdef CONF_MODULE
    char lutFile[16];
    strcpy(lutFile, "hq2x.png");
    lutFile[2] = '0' + scale;
#else
    char lutFile[32];
    strcpy(lutFile, "graphics/shader/hq2x.png");
    lutFile[18] = '0' + scale;
#endif
    return loadTexture(lutFile, 0, GL_NEAREST, NULL);
}

static void reserveDrawList(const GLuint* vbo, int byteSize)
{
    int i;
    for (i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, byteSize, NULL, GL_DYNAMIC_DRAW);
    }
}

const char* gpu_init(void* res, int w, int h, int scale, int filter)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    GLuint sh;
    GLint cmap, mmap;
#ifdef GPU_RENDER
    GLint noise;
#endif
    int tsize[2];

    assert(sizeof(GLuint) == sizeof(uint32_t));

    memset(gr, 0, sizeof(OpenGLResources));
    /*
    gr->scalerLut = 0;
    gr->scaler = 0;
    gr->blockCount = 0;
    gr->tilesTex = 0;
    */

    gr->dl[0].buf = GLOB_GUI_LIST0;
    gr->dl[0].byteSize = ATTR_STRIDE * 6 * 400;
    gr->dl[1].buf = GLOB_HUD_LIST0;
    gr->dl[1].byteSize = ATTR_STRIDE * 6 * 400;
#ifdef GPU_RENDER
    gr->dl[2].buf = GLOB_DRAW_LIST0;
    gr->dl[2].byteSize = ATTR_STRIDE * 6 * 400;
    gr->dl[3].buf = GLOB_FX_LIST0;
    gr->dl[3].byteSize = ATTR_STRIDE * 6 * 20;
    gr->dl[4].buf = GLOB_MAPFX_LIST0;
    gr->dl[4].byteSize = ATTR_STRIDE * 6 * 8;
#endif

#ifdef DEBUG_GL
    enableGLDebug();
#endif

    // Create screen, white, noise & shadow textures.
    glGenTextures(TEXTURE_COUNT, &gr->screenTex);
    gpu_defineTex(gr->screenTex, 320, 200, NULL,
#if defined(ANDROID) || defined(USE_GLES)
                  GL_RGBA,  // Must match glTexSubImage2D format.
#else
                  GL_RGB,
#endif
                  GL_NEAREST);
    gpu_defineTex(gr->whiteTex, 2, 2, whitePixels, GL_RGBA, GL_NEAREST);
    gpu_defineTex(gr->shadowTex, SHADOW_DIM, SHADOW_DIM, NULL,
                  GL_RGBA, GL_LINEAR);

    if (! loadTexture("cfont.png", gr->fontTex, GL_LINEAR, NULL))
        return "cfont.png";

    if (! loadTexture("gui.png", gr->guiTex, GL_LINEAR, tsize))
        return "gui.png";
    gr->guiTexSize[0] = float(tsize[0]);
    gr->guiTexSize[1] = float(tsize[1]);

#ifdef GPU_RENDER
    if (! loadTexture("noise_2d.png", gr->noiseTex, GL_NEAREST, NULL))
        return "noise_2d.png";

    gr->shadowFbo = _makeFramebuffer(gr->shadowTex);
    if (! gr->shadowFbo)
        return "shadow FBO";
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
#endif


    // Set default state.
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, w, h);


    // Create scaler shader.
    if (filter == 1 && scale > 1) {
        if (scale > 4)
            scale = 4;

        gr->scalerLut = loadHQXTableImage(scale);
        if (! gr->scalerLut)
            return "hq2x.png";

        gr->scaler = sh = glCreateProgram();
        if (compileSLFile(sh, "hq2x.glsl", scale))
            return "hq2x.glsl";

        gr->slocScMat = glGetUniformLocation(sh, "MVPMatrix");
        gr->slocScDim = glGetUniformLocation(sh, "TextureSize");
        gr->slocScTex = glGetUniformLocation(sh, "Texture");
        gr->slocScLut = glGetUniformLocation(sh, "LUT");

        glUseProgram(sh);
        glUniformMatrix4fv(gr->slocScMat, 1, GL_FALSE, m4_identity);
        glUniform2f(gr->slocScDim, (float) (w / scale), (float) (h / scale));
        glUniform1i(gr->slocScTex, GTU_CMAP);
        glUniform1i(gr->slocScLut, GTU_SCALER_LUT);
    }
    else if (filter == 2 && scale > 1) {
        gr->scaler = sh = glCreateProgram();
        if (compileSLFile(sh, "xbr-lv2.glsl", scale))
            return "xbr-lv2.glsl";

        gr->slocScMat = 0;
        gr->slocScDim = glGetUniformLocation(sh, "TextureSize");
        gr->slocScTex = glGetUniformLocation(sh, "Texture");
        gr->slocScLut = 0;

        glUseProgram(sh);
        glUniform2f(gr->slocScDim, (float) (w / scale), (float) (h / scale));
        glUniform1i(gr->slocScTex, GTU_CMAP);
    }


    // Create colormap shader.
    gr->shadeColor = sh = glCreateProgram();
    if (compileShaders(sh, cmap_vertShader, cmap_fragShader))
        return "colormap shader";

    gr->slocTrans   = glGetUniformLocation(sh, "transform");
    cmap            = glGetUniformLocation(sh, "cmap");
    gr->slocTint    = glGetUniformLocation(sh, "tint");

    glUseProgram(sh);
    glUniformMatrix4fv(gr->slocTrans, 1, GL_FALSE, m4_identity);
    glUniform1i(cmap, GTU_CMAP);
    glUniform4f(gr->slocTint, 1.0, 1.0, 1.0, 1.0);


    // Create solid shader.
    gr->shadeSolid = sh = glCreateProgram();
    if (compileShaders(sh, solid_vertShader, solid_fragShader))
        return "solid shader";

    gr->solidTrans  = glGetUniformLocation(sh, "transform");
    gr->solidColor  = glGetUniformLocation(sh, "color");

    glUseProgram(sh);
    glUniformMatrix4fv(gr->solidTrans, 1, GL_FALSE, m4_identity);
    glUniform4f(gr->solidColor, 1.0, 1.0, 1.0, 1.0);


    // Create glyph (text) shader.
    gr->shadeGlyph = sh = glCreateProgram();
    if (compileSLFile(sh, "msdf.glsl", 0))
        return "msdf.glsl";

    gr->glyphTrans  = glGetUniformLocation(sh, "transform");
    cmap            = glGetUniformLocation(sh, "cmap");
    mmap            = glGetUniformLocation(sh, "msdf");
    //gr->glyphRange  = glGetUniformLocation(sh, "screenPxRange");
    gr->glyphBg     = glGetUniformLocation(sh, "bgColor");
    gr->glyphFg     = glGetUniformLocation(sh, "fgColor");

    glUseProgram(sh);
    {
    float ortho[16];
    m4_ortho(ortho, 0.0f, (float) w, 0.0f, (float) h, -1.0f, 1.0f);
    glUniformMatrix4fv(gr->glyphTrans, 1, GL_FALSE, ortho);
    glUniform1i(cmap, GTU_CMAP);
    glUniform1i(mmap, GTU_MATERIAL);
    glUniform4f(gr->glyphBg, 0.0, 0.0, 0.0, 0.0);
    glUniform4f(gr->glyphFg, 1.0, 1.0, 1.0, 1.0);
    //glUniform1f(gr->glyphRange, 2.0);
    }


#ifdef GPU_RENDER
    // Create shadowcast shader.
    gr->shadow = sh = glCreateProgram();
    if (compileSLFile(sh, "shadowcast.glsl", 0))
        return "shadowcast.glsl";

    gr->shadowTrans  = glGetUniformLocation(sh, "transform");
    gr->shadowVport  = glGetUniformLocation(sh, "vport");
    gr->shadowViewer = glGetUniformLocation(sh, "viewer");
    gr->shadowCounts = glGetUniformLocation(sh, "shape_count");
    gr->shadowShapes = glGetUniformLocation(sh, "shapes");


    // Create world shader.
    gr->shadeWorld = sh = glCreateProgram();
    if (compileSLFile(sh, "world.glsl", 0))
        return "world.glsl";

    gr->worldTrans     = glGetUniformLocation(sh, "transform");
    cmap               = glGetUniformLocation(sh, "cmap");
    mmap               = glGetUniformLocation(sh, "mmap");
    noise              = glGetUniformLocation(sh, "noise2D");
    gr->worldShadowMap = glGetUniformLocation(sh, "shadowMap");
    gr->worldScroll    = glGetUniformLocation(sh, "scroll");

    glUseProgram(sh);
    glUniformMatrix4fv(gr->worldTrans, 1, GL_FALSE, m4_identity);
    glUniform1i(cmap, GTU_CMAP);
    glUniform1i(mmap, GTU_MATERIAL);
    glUniform1i(noise, GTU_NOISE);
    glUniform1i(gr->worldShadowMap, GTU_SHADOW);
#endif


    // Create our vertex buffers.
    glGenBuffers(GLOB_COUNT, gr->vbo);

    // Reserve space in the double-buffered draw lists.
    reserveDrawList(gr->vbo + GLOB_GUI_LIST0, gr->dl[0].byteSize);
    reserveDrawList(gr->vbo + GLOB_HUD_LIST0, gr->dl[1].byteSize);
#ifdef GPU_RENDER
    reserveDrawList(gr->vbo + GLOB_DRAW_LIST0, gr->dl[2].byteSize);
    reserveDrawList(gr->vbo + GLOB_FX_LIST0,   gr->dl[3].byteSize);
    reserveDrawList(gr->vbo + GLOB_MAPFX_LIST0,gr->dl[4].byteSize);
#endif

    // Create quad geometry.
    glBindBuffer(GL_ARRAY_BUFFER, gr->vbo[GLOB_QUAD]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadAttr), quadAttr, GL_STATIC_DRAW);

    // Create vertex attribute layouts.
    glGenVertexArrays(GLOB_COUNT, gr->vao);
    for(int i = 0; i < GLOB_COUNT; ++i)
        _defineAttributeLayout(gr->vao[i], gr->vbo[i]);
    glBindVertexArray(0);

    return NULL;
}

void gpu_free(void* res)
{
    OpenGLResources* gr = (OpenGLResources*) res;

    if (gr->scaler) {
        glDeleteProgram(gr->scaler);
        glDeleteTextures(1, &gr->scalerLut);
    }

    glDeleteVertexArrays(GLOB_COUNT, gr->vao);
    glDeleteBuffers(GLOB_COUNT, gr->vbo);
    glDeleteProgram(gr->shadeColor);
    glDeleteProgram(gr->shadeSolid);
    glDeleteProgram(gr->shadeGlyph);
#ifdef GPU_RENDER
    glDeleteProgram(gr->shadeWorld);
    glDeleteProgram(gr->shadow);
    glDeleteFramebuffers(1, &gr->shadowFbo);
#endif
    glDeleteTextures(TEXTURE_COUNT, &gr->screenTex);
}

void gpu_viewport(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
}

#if 0
/*
 * Load a texture from a module file.
 *
 * \param linear    Non-zero to enable linear filtering.
 *
 * Return the identifier of the new texture.
 */
uint32_t gpu_loadTexture(const char* file, int linear)
{
    return loadTexture(file, 0, linear ? GL_LINEAR : GL_NEAREST, NULL);
}
#endif

uint32_t gpu_makeTexture(const Image32* img)
{
    GLuint tex;
    glGenTextures(1, &tex);
    gpu_defineTex(tex, img->w, img->h, img->pixels, GL_RGBA, GL_NEAREST);
    return tex;
}

void gpu_blitTexture(uint32_t tex, int x, int y, const Image32* img)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, img->w, img->h,
                    GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);
}

/*
 * Release texture created with gpu_makeTexture() or gpu_loadTexture().
 */
void gpu_freeTexture(uint32_t tex)
{
    glDeleteTextures(1, &tex);
}

/*
 * Return the identifier of the screen texture which is created by gpu_init().
 */
uint32_t gpu_screenTexture(void* res)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    return gr->screenTex;
}

#ifdef GPU_RENDER
void gpu_setTilesTexture(void* res, uint32_t tex, uint32_t mat, float vDim)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    gr->tilesTex = tex;
    gr->tilesMat = mat;
    gr->tilesVDim = vDim;
}
#endif

/*
 * Render a background image using the scale defined with gpu_init().
 */
void gpu_drawTextureScaled(void* res, uint32_t tex)
{
    OpenGLResources* gr = (OpenGLResources*) res;

    glActiveTexture(GL_TEXTURE0 + GTU_CMAP);
    glBindTexture(GL_TEXTURE_2D, tex);

    if (gr->scaler) {
        glUseProgram(gr->scaler);
        glActiveTexture(GL_TEXTURE0 + GTU_SCALER_LUT);
        glBindTexture(GL_TEXTURE_2D, gr->scalerLut);
    } else {
        glUseProgram(gr->shadeColor);
        glUniformMatrix4fv(gr->slocTrans, 1, GL_FALSE, m4_identity);
    }

    glDisable(GL_BLEND);
    glBindVertexArray(gr->vao[ GLOB_QUAD ]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/*
 * Begin a rendered frame cleared to a solid color.
 */
void gpu_clear(void* res, const float* color)
{
    glClearColor(color[0], color[1], color[2], color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

/*
 * Invert the colors of all pixels in the current viewport.
 */
void gpu_invertColors(void* res)
{
    OpenGLResources* gr = (OpenGLResources*) res;

    glUseProgram(gr->shadeSolid);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
    glBindVertexArray(gr->vao[ GLOB_QUAD ]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void gpu_setScissor(int* box)
{
    if (box) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(box[0], box[1], box[2], box[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

/*
 * Begin adding triangles to a double-buffered draw list.
 *
 * Returns a pointer to the start of the attributes buffer.
 * This should be advanced and passed to gpu_endTris() when all triangles
 * have been generated.
 *
 * /param list  The GpuDrawList identifier.
 */
float* gpu_beginTris(void* res, int list)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    DrawList* dl = gr->dl + list;

    dl->buf ^= 1;
    glBindBuffer(GL_ARRAY_BUFFER, gr->vbo[ dl->buf ]);
    gr->dptr = (GLfloat*) glMapBufferRange(GL_ARRAY_BUFFER, 0, dl->byteSize,
                                           GL_MAP_WRITE_BIT);
    return gr->dptr;
}

/*
 * Complete the list of triangles generated since gpu_beginTris().
 * Each call to gpu_beginTris() must be paired with gpu_endTris().
 * Each list must be created separately; these calls cannot be interleaved.
 */
void gpu_endTris(void* res, int list, float* attr)
{
    OpenGLResources* gr = (OpenGLResources*) res;

    glUnmapBuffer(GL_ARRAY_BUFFER);

    assert(gr->dptr);
    gr->dl[ list ].count = attr - gr->dptr;
    gr->dptr = NULL;
}

/*
 * Set the triangle count for the list to zero.  gpu_drawTris() becomes a
 * no-op until a new list is created.
 */
void gpu_clearTris(void* res, int list)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    gr->dl[list].count = 0;
}

/*
 * Copy the current buffer of a list to its other buffer.
 * It is intended to be used after gpu_endTris() but before the list is drawn
 * for the first time.
 */
void gpu_mirrorList(void* res, int list)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    const DrawList* dl = gr->dl + list;
    int bn = dl->buf;

    glBindBuffer(GL_COPY_READ_BUFFER, gr->vbo[bn]);
    glBindBuffer(GL_COPY_WRITE_BUFFER, gr->vbo[bn ^ 1]);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0,
                        dl->count * sizeof(float));
}

/*
 * Draw any triangles created between the last gpu_beginTris/endTris calls.
 */
void gpu_drawTris(void* res, int list)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    DrawList* dl = gr->dl + list;

    if (! dl->count)
        return;
    //printf("gpu_drawTris %d\n", dl->count);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    glBindVertexArray(gr->vao[ dl->buf ]);
    glDrawArrays(GL_TRIANGLES, 0, dl->count / ATTR_COUNT);
}

void gpu_drawGui(void* res, int list, const PrimGroup* groups, int count)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    DrawList* dl = gr->dl + list;

#if 0
    printf("gpu_drawGui list: %d count: %d (%d vtx) groups: [",
            list, dl->count, dl->count / ATTR_COUNT);
    int vbytes = ATTR_COUNT * sizeof(float);
    int usedBytes = 0;
    for (int i = 0; i < count; ++i) {
        printf("%d,%d ", groups[i].first, groups[i].count);
        usedBytes = (groups[i].first + groups[i].count) * vbytes;
        if (usedBytes > dl->byteSize)
            printf("!! ");
    }
    printf("]\n    excess: %d vtx\n", (dl->byteSize - usedBytes) / vbytes);
#endif

    if (! dl->count)
        return;

    glUseProgram(gr->shadeGlyph);
    /*
    glUniform4f(gr->glyphBg, 0.0, 0.0, 0.0, 1.0);
    glUniform4f(gr->glyphFg, 1.0, 1.0, 1.0, 1.0);
    */
    glActiveTexture(GL_TEXTURE0 + GTU_CMAP);
    glBindTexture(GL_TEXTURE_2D, gr->guiTex);
    glActiveTexture(GL_TEXTURE0 + GTU_MATERIAL);
    glBindTexture(GL_TEXTURE_2D, gr->fontTex);

    glEnable(GL_BLEND);

    // Similar to gpu_drawTris(gr, list).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    glBindVertexArray(gr->vao[ dl->buf ]);
    if (count) {
        const PrimGroup* end = groups + count;
        for (; groups != end; ++groups)
            glDrawArrays(GL_TRIANGLES, groups->first, groups->count);
    } else
        glDrawArrays(GL_TRIANGLES, 0, dl->count / ATTR_COUNT);
}

void gpu_guiClutUV(void* res, float* uv, float colorIndex)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    uv[0] = (colorIndex + 0.5f) / gr->guiTexSize[0];
    uv[1] = 0.5f / gr->guiTexSize[1];
}

float* gpu_emitQuad(float* attr, const float* drawRect, const float* uvRect)
{
    float w = drawRect[2];
    float h = drawRect[3];
    int i;

    /*
   -1.0,-1.0, 0.0,   0.0, 1.0,
    1.0,-1.0, 0.0,   1.0, 1.0,
    1.0, 1.0, 0.0,   1.0, 0.0,
    1.0, 1.0, 0.0,   1.0, 0.0,
   -1.0, 1.0, 0.0,   0.0, 0.0,
   -1.0,-1.0, 0.0,   0.0, 1.0
    */

#if 0
    printf( "gpu_emitQuad %f,%f,%f,%f  %f,%f,%f,%f\n",
            drawRect[0], drawRect[1], drawRect[2], drawRect[3],
            uvRect[0], uvRect[1], uvRect[2], uvRect[3]);
#endif

#define EMIT_POS(x,y) \
    *attr++ = x; \
    *attr++ = y; \
    *attr++ = 0.0f

#define EMIT_UV(u,v) \
    *attr++ = u; \
    *attr++ = v; \
    *attr++ = 0.0f; \
    *attr++ = 0.0f

    // NOTE: We only do writes to attr here (avoid memcpy).

    // First vertex, lower-left corner
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UV(uvRect[0], uvRect[3]);

    // Lower-right corner
    EMIT_POS(drawRect[0] + w, drawRect[1]);
    EMIT_UV(uvRect[2], uvRect[3]);

    // Top-right corner
    for (i = 0; i < 2; ++i) {
        EMIT_POS(drawRect[0] + w, drawRect[1] + h);
        EMIT_UV(uvRect[2], uvRect[1]);
    }

    // Top-left corner
    EMIT_POS(drawRect[0], drawRect[1] + h);
    EMIT_UV(uvRect[0], uvRect[1]);

    // Repeat first vertex
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UV(uvRect[0], uvRect[3]);

    return attr;
}

#ifdef GPU_RENDER
float* gpu_emitQuadScroll(float* attr, const float* drawRect,
                          const float* uvRect, float scrollSourceV)
{
    float w = drawRect[2];
    float h = drawRect[3];
    int i;

#define EMIT_UVS(u,v,vunit) \
    *attr++ = u; \
    *attr++ = v; \
    *attr++ = vunit; \
    *attr++ = scrollSourceV

    // NOTE: We only do writes to attr here (avoid memcpy).

    // First vertex, lower-left corner
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UVS(uvRect[0], uvRect[3], 1.0f);

    // Lower-right corner
    EMIT_POS(drawRect[0] + w, drawRect[1]);
    EMIT_UVS(uvRect[2], uvRect[3], 1.0f);

    // Top-right corner
    for (i = 0; i < 2; ++i) {
        EMIT_POS(drawRect[0] + w, drawRect[1] + h);
        EMIT_UVS(uvRect[2], uvRect[1], 0.0f);
    }

    // Top-left corner
    EMIT_POS(drawRect[0], drawRect[1] + h);
    EMIT_UVS(uvRect[0], uvRect[1], 0.0f);

    // Repeat first vertex
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UVS(uvRect[0], uvRect[3], 1.0f);

    return attr;
}

float* gpu_emitQuadFire(float* attr, const float* drawRect,
                        const float* uvRect, float uOff)
{
    float w = drawRect[2];
    float h = drawRect[3];
    int i;

#define EMIT_UVF(u,v,uUnit,vUnit) \
    *attr++ = u; \
    *attr++ = v; \
    *attr++ = uUnit; \
    *attr++ = vUnit

    // NOTE: We only do writes to attr here (avoid memcpy).

    // First vertex, lower-left corner
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UVF(uvRect[0], uvRect[3], uOff, 0.0f);

    // Lower-right corner
    EMIT_POS(drawRect[0] + w, drawRect[1]);
    EMIT_UVF(uvRect[2], uvRect[3], 1.0f+uOff, 0.0f);

    // Top-right corner
    for (i = 0; i < 2; ++i) {
        EMIT_POS(drawRect[0] + w, drawRect[1] + h);
        EMIT_UVF(uvRect[2], uvRect[1], 1.0f+uOff, 1.0f);
    }

    // Top-left corner
    EMIT_POS(drawRect[0], drawRect[1] + h);
    EMIT_UVF(uvRect[0], uvRect[1], uOff, 1.0f);

    // Repeat first vertex
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UVF(uvRect[0], uvRect[3], uOff, 0.0f);

    return attr;
}

float* gpu_emitQuadFlag(float* attr, const float* drawRect)
{
    float w = drawRect[2];
    float h = drawRect[3];
    int i;

    // NOTE: We only do writes to attr here (avoid memcpy).

    // First vertex, lower-left corner
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UVF(0.0f, 0.0f, 2.0f, 0.0f);

    // Lower-right corner
    EMIT_POS(drawRect[0] + w, drawRect[1]);
    EMIT_UVF(1.0f, 0.0f, 2.0f, 0.0f);

    // Top-right corner
    for (i = 0; i < 2; ++i) {
        EMIT_POS(drawRect[0] + w, drawRect[1] + h);
        EMIT_UVF(1.0f, 1.0f, 2.0f, 0.0f);
    }

    // Top-left corner
    EMIT_POS(drawRect[0], drawRect[1] + h);
    EMIT_UVF(0.0f, 1.0f, 2.0f, 0.0f);

    // Repeat first vertex
    EMIT_POS(drawRect[0], drawRect[1]);
    EMIT_UVF(0.0f, 0.0f, 2.0f, 0.0f);

    return attr;
}

//--------------------------------------
// Map Rendering

#define CHUNK_CACHE_SIZE    4

#ifdef MAP_ANIMATOR
static void stopChunkAnimations(Animator* animator, MapFx* it, int count)
{
    MapFx* end = it + count;
    for (; it != end; ++it) {
        if (it->anim != ANIM_UNUSED) {
            anim_setState(animator, it->anim, ANIM_FREE);
            it->anim = ANIM_UNUSED;
        }
    }
}
#endif

void gpu_resetMap(void* res, const Map* map)
{
    OpenGLResources* gr = (OpenGLResources*) res;

    gr->blockCount = 0;
    gr->mapData    = map->data;
    gr->renderData = map->tileset->render;
    gr->mapW       = map->width;
    gr->mapH       = map->height;

    // Initialize map chunks.
    assert(map->chunk_height == map->chunk_width);
    gr->mapChunkDim = map->chunk_width;
    gr->mapChunkVertCount = gr->mapChunkDim * gr->mapChunkDim * 6;

    for (int i = 0; i < CHUNK_CACHE_SIZE; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, gr->vbo[ GLOB_MAP_CHUNK0+i ]);
        glBufferData(GL_ARRAY_BUFFER, gr->mapChunkVertCount * ATTR_STRIDE,
                     NULL, GL_DYNAMIC_DRAW);

#ifdef MAP_ANIMATOR
        int fxUsed = gr->mapChunkFxUsed[i];
        if (fxUsed)
            stopChunkAnimations(MAP_ANIMATOR,
                                gr->mapChunkFx + i*CHUNK_FX_LIMIT, fxUsed);
#endif
    }

    // Clear chunk cache.
    memset(gr->mapChunkId, 0xff, CHUNK_CACHE_SIZE*sizeof(uint16_t));
    memset(gr->mapChunkFxUsed, 0, CHUNK_CACHE_SIZE*sizeof(uint16_t));
}

struct ChunkLoc {
    int16_t x, y;
};

struct ChunkInfo {
    OpenGLResources* gr;
    const float* uvs;
    uint16_t* mapChunkId;
    ChunkLoc* chunkLoc;
    int geoUsedMask;
};

#define VIEW_TILE_SIZE  1.0f

static void _initFxInvert(MapFx* fx, TileId tid, float* drawRect,
                          const float* uvCur)
{
    const Tile* tile = Tileset::findTileById(tid);
    float pixelW = tile->w;
    float pixelH = tile->h;
    float tileTexCoordW = (uvCur[2] - uvCur[0]) / pixelW;
    float tileTexCoordH = (uvCur[3] - uvCur[1]) / pixelH;
    float tileViewW = VIEW_TILE_SIZE / pixelW;
    float tileViewH = VIEW_TILE_SIZE / pixelH;

    assert(tile->anim);
    TileAnimTransform* tf = tile->anim->transforms[0];
    float px = (float) tf->var.invert.x;
    float py = (float) tf->var.invert.y;
    float pw = (float) tf->var.invert.w;
    float ph = (float) tf->var.invert.h;

    //printf("KR tr %d,%d %f,%f,%f,%f\n", tile->w, tile->h, px, py, pw, ph);
    //printf("KR uv %f,%f,%f,%f\n", uvCur[0], uvCur[1], uvCur[2], uvCur[3]);

    fx->x  = drawRect[0] + tileViewW * px;
    fx->y  = drawRect[1] + tileViewH * (pixelH - (py + ph));
    fx->w  = tileViewW * pw;
    fx->h  = tileViewH * ph;
    fx->u  = uvCur[0] + tileTexCoordW * px;
    fx->u2 =    fx->u + tileTexCoordW * pw;
#if 0
    fx->v  = uvCur[1] + tileTexCoordH * py;
    fx->v2 =    fx->v + tileTexCoordH * ph;
#else
    fx->v2 = uvCur[1] + tileTexCoordH * py;
    fx->v  =   fx->v2 + tileTexCoordH * ph;
#endif

#ifdef EMULATE_U4
    fx->anim = anim_startCycleRandomI(MAP_ANIMATOR,
                                      0.25, ANIM_FOREVER, 0,
                                      0, 2, tile->anim->random);
#endif
}

/*
 * \param chunk    Map data aligned at top-left of chunk.
 */
static void _buildChunkGeo(ChunkInfo* ci, int i, const TileId* chunk)
{
    float drawRect[4];  // x, y, width, height
    const float* uvCur;
    const float* uvScroll;
    float* attr;
    const TileId* ip;
    const TileRenderData* tr;
    const float* uvTable = ci->uvs;
    OpenGLResources* gr = ci->gr;
    float startX;
    int x, y;
    int stride = gr->mapW;          // Map tile width
    int cdim   = gr->mapChunkDim;   // Chunk tile dimensions
    int fxUsed;


#ifdef MAP_ANIMATOR
    // Stop any running animations.
    fxUsed = gr->mapChunkFxUsed[i];
    if (fxUsed) {
        stopChunkAnimations(MAP_ANIMATOR,
                            gr->mapChunkFx + i*CHUNK_FX_LIMIT, fxUsed);
        fxUsed = 0;
    }
#else
    fxUsed = 0;
#endif

    glBindBuffer(GL_ARRAY_BUFFER, gr->vbo[GLOB_MAP_CHUNK0 + i]);
    attr = (float*) glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                     gr->mapChunkVertCount * ATTR_STRIDE,
                                     GL_MAP_WRITE_BIT);
    if (! attr) {
        fprintf(stderr, "buildChunkGeo: glMapBufferRange failed\n");
        return;
    }

    // Placing center of the top left tile at the origin.
    startX = -0.5f * VIEW_TILE_SIZE;
    drawRect[1] = startX;
    drawRect[2] = VIEW_TILE_SIZE;
    drawRect[3] = VIEW_TILE_SIZE;

    for (y = 0; y < cdim; ++y) {
        drawRect[0] = startX;
        ip = chunk;
        for (x = 0; x < cdim; ++x) {
            tr = gr->renderData + *ip++;
            uvCur = uvTable + tr->vid*4;
            if (tr->animType == ATYPE_SCROLL) {
                uvScroll = uvTable + tr->animData.scroll*4;
                attr = gpu_emitQuadScroll(attr, drawRect, uvCur, uvScroll[1]);
            } else if (tr->animType == ATYPE_PIXEL_COLOR) {
                float centerX = (float) tr->animData.hot[0];
                float tileW   = (float) tr->animData.hot[1];
                float uOff  = (tileW*0.5 - centerX) / tileW;
                attr = gpu_emitQuadFire(attr, drawRect, uvCur, uOff);
            } else {
                if (tr->animType == ATYPE_INVERT && fxUsed < CHUNK_FX_LIMIT) {
                    _initFxInvert(gr->mapChunkFx + i*CHUNK_FX_LIMIT + fxUsed,
                                  ip[-1], drawRect, uvCur);
                    ++fxUsed;
                }
                attr = gpu_emitQuad(attr, drawRect, uvCur);
            }
            drawRect[0] += drawRect[2];
        }
        drawRect[1] -= drawRect[3];
        chunk += stride;
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);
    gr->mapChunkFxUsed[i] = fxUsed;
}

#define WRAP(x,loc,limit) \
    if (x < 0) { \
        x += limit; \
        loc = -limit; \
    } else if (x >= limit) { \
        x -= limit; \
        loc = limit; \
    } else \
        loc = 0;

// LIMIT: Maximum of 256x256 chunks.
#define CHUNK_ID(c,r)       (c<<8 | r)

/*
 * Find (or create) chunk geometry in the four reserved VBO slots.
 * Return the chunk vertex buffer used (0-3), or -1 if no slot is available.
 *
 * \param x         Map tile column.
 * \param y         Map tile row.
 * \param bumpPass  Replace a cached chunk.
 */
static int _obtainChunkGeo(ChunkInfo* ci, int x, int y, int bumpPass)
{
    OpenGLResources* gr = ci->gr;
    ChunkLoc* loc;
    int i;
    int ccol, crow;
    int cdim = gr->mapChunkDim;
    int wx, wy;
    uint16_t chunkId;

    WRAP(x, wx, gr->mapW);
    WRAP(y, wy, gr->mapH);
    ccol = x / cdim;
    crow = y / cdim;
    chunkId = CHUNK_ID(ccol, crow);

    if (bumpPass)
    {
        // Find unassigned chunk to replace.
        for (i = 0; i < CHUNK_CACHE_SIZE; ++i) {
            if ((ci->geoUsedMask & (1 << i)) == 0)
                goto build;
        }
        // This should never be reached unless the function is called more
        // than CHUNK_CACHE_SIZE times.
        assert(0 && "bumpPass failed");
        return -1;
    }
    else
    {
        // Check if already made.
        for (i = 0; i < CHUNK_CACHE_SIZE; ++i) {
            if (ci->mapChunkId[i] == chunkId)
                goto used;
        }

        // Find unused VBO.
        for (i = 0; i < CHUNK_CACHE_SIZE; ++i) {
            if (ci->mapChunkId[i] == 0xffff)
                goto build;
        }

        // No unused VBO, fail.
        return -1;
    }

build:
    ci->mapChunkId[i] = chunkId;
    _buildChunkGeo(ci, i, gr->mapData + (crow * gr->mapW + ccol) * cdim);
used:
    loc = ci->chunkLoc + i;
    loc->x = wx + (ccol * cdim);
    loc->y = wy + (crow * cdim);
    ci->geoUsedMask |= 1 << i;
    return i;
}

/*
 * \param view          Pointer to TileView with a valid map.
 * \param tileUVs       Table of four floats (minU,minV,maxU,maxV) per tile.
 * \param blocks        Sets the occluder shapes for shadowcasting.
 *                      Pass NULL to reuse any previously set shapes.
 * \param cx            Map tile row to center view on.
 * \param cy            Map tile column to center view on.
 * \param scale         Normal = 2.0 / view->columns.
 */
void gpu_drawMap(void* res, const TileView* view, const float* tileUVs,
                 const BlockingGroups* blocks,
                 int cx, int cy, float scale)
{
    OpenGLResources* gr = (OpenGLResources*) res;
    ChunkLoc cloc[4];   // Tile location of chunks on the map.
    int i, usedMask;

    // Render shadows.
    if (blocks) {
        gr->blockCount = blocks->left + blocks->center + blocks->right;
        if (gr->blockCount) {
            glUseProgram(gr->shadow);
            glUniformMatrix4fv(gr->shadowTrans, 1, GL_FALSE, m4_identity);
            glUniform4f(gr->shadowVport, 0.0f, 0.0f, SHADOW_DIM, SHADOW_DIM);
            glUniform3f(gr->shadowViewer, 0.0f, 0.0f, 11.0f);
            glUniform3i(gr->shadowCounts, blocks->left, blocks->center,
                                          blocks->right);
            glUniform3fv(gr->shadowShapes, gr->blockCount, blocks->tilePos);

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gr->shadowFbo);
            glViewport(0, 0, SHADOW_DIM, SHADOW_DIM);

            glDisable(GL_BLEND);
            glBindVertexArray(gr->vao[ GLOB_QUAD ]);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }
    }

    {
    ChunkInfo ci;
    int bindex[4];  // Chunk vertex buffer index (0-3) at view corner.
    int left, top, right, bot;
    int halfW, halfH;

    ci.gr = gr;
    ci.uvs = tileUVs;
    ci.mapChunkId = gr->mapChunkId;
    ci.chunkLoc = cloc;
    ci.geoUsedMask = 0;

    // FIXME: Apply scale.
    halfW = view->columns / 2;
    halfH = view->rows / 2;

    left  = cx - halfW;
    right = cx + halfW;
    top   = cy - halfH;
    bot   = cy + halfH;

    // First pass to see what chunks are cached.
    bindex[0] = _obtainChunkGeo(&ci, left,  top, 0);
    bindex[1] = _obtainChunkGeo(&ci, right, top, 0);
    bindex[2] = _obtainChunkGeo(&ci, left,  bot, 0);
    bindex[3] = _obtainChunkGeo(&ci, right, bot, 0);

    // Second pass to bump any cached chunks (if needed).
    if (bindex[0] < 0)
        _obtainChunkGeo(&ci, left,  top, 1);
    if (bindex[1] < 0)
        _obtainChunkGeo(&ci, right, top, 1);
    if (bindex[2] < 0)
        _obtainChunkGeo(&ci, left,  bot, 1);
    if (bindex[3] < 0)
        _obtainChunkGeo(&ci, right, bot, 1);

    usedMask = ci.geoUsedMask;
    }

    {
    const int* vrect = view->screenRect;
    glViewport(vrect[0], vrect[1], vrect[2], vrect[3]);
    }

    {
    float matrix[16];
    float scaleY = scale * view->aspect;
    int fxUsed = 0;

    gr->time = ((float) getTicks()) * 0.001;
    m4_loadIdentity(matrix);
    matrix[0] = matrix[10] = scale;
    matrix[5] = scaleY;

    glUseProgram(gr->shadeWorld);
    glUniform2f(gr->worldScroll, gr->tilesVDim, gr->time);
    glActiveTexture(GL_TEXTURE0 + GTU_SHADOW);
    if (gr->blockCount)
        glBindTexture(GL_TEXTURE_2D, gr->shadowTex);
    else
        glBindTexture(GL_TEXTURE_2D, gr->whiteTex);

    glActiveTexture(GL_TEXTURE0 + GTU_CMAP);
    glBindTexture(GL_TEXTURE_2D, gr->tilesTex);
    glActiveTexture(GL_TEXTURE0 + GTU_MATERIAL);
    glBindTexture(GL_TEXTURE_2D, gr->tilesMat);
    glActiveTexture(GL_TEXTURE0 + GTU_NOISE);
    glBindTexture(GL_TEXTURE_2D, gr->noiseTex);

    glDisable(GL_BLEND);

    for (i = 0; i < 4; ++i) {
        if (usedMask & (1 << i)) {
            // Position chunk in viewport.
            matrix[ kX ] = (float) (cloc[i].x - cx) * scale;
            matrix[ kY ] = (float) (cy - cloc[i].y) * scaleY;
            glUniformMatrix4fv(gr->worldTrans, 1, GL_FALSE, matrix);

            glBindVertexArray(gr->vao[ GLOB_MAP_CHUNK0 + i ]);
            glDrawArrays(GL_TRIANGLES, 0, gr->mapChunkVertCount);

            if (gr->mapChunkFxUsed[i])
                fxUsed = 1;
        }
    }

    matrix[kX] = matrix[kY] = 0.0f;
    glUniformMatrix4fv(gr->worldTrans, 1, GL_FALSE, matrix);

    if (fxUsed) {
        const int MAPFX_LIST = GLOB_MAPFX_LIST0 / 2;
        float rect[4];
        float xoff, yoff;
        float* fxAttr = gpu_beginTris(gr, MAPFX_LIST);
        for (i = 0; i < 4; ++i) {
            if (usedMask & (1 << i) && gr->mapChunkFxUsed[i]) {
                xoff = (float) (cloc[i].x - cx);
                yoff = (float) (cy - cloc[i].y);

                const MapFx* it  = gr->mapChunkFx + i*CHUNK_FX_LIMIT;
                const MapFx* end = it + gr->mapChunkFxUsed[i];
                for (; it != end; ++it) {
                    // Assuming only ATYPE_INVERT effects are used for now.
#ifdef EMULATE_U4
                    if (anim_valueI(MAP_ANIMATOR, it->anim))
                        continue;

                    rect[0] = xoff + it->x;
                    rect[1] = yoff + it->y;
                    rect[2] = it->w;
                    rect[3] = it->h;

                    //printf("KR fx %f,%f %f,%f\n", it->x, it->y, it->w, it->h);
                    fxAttr = gpu_emitQuad(fxAttr, rect, &it->u);
#else
                    // NOTE: Width is doubled to allow flags to change
                    // direction in the shader.
                    rect[0] = xoff + it->x - it->w;
                    rect[1] = yoff + it->y;
                    rect[2] = it->w * 2.0f;
                    rect[3] = it->h;

                    fxAttr = gpu_emitQuadFlag(fxAttr, rect);
#endif
                }
            }
        }
        gpu_endTris(gr, MAPFX_LIST, fxAttr);

        glEnable(GL_BLEND);
        gpu_drawTris(gr, MAPFX_LIST);
    }
    }
}
#endif
