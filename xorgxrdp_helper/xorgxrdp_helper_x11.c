/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2020-2022
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Currently nvenc requires GLX because NVidia's EGL does not have
 * EGL_NOK_texture_from_pixmap extension but NVidia's GLX does have
 * GLX_EXT_texture_from_pixmap.  We require one if those,
 * also, yami required EGL because it used dma bufs.
 * I do not think any vendor's GLX support dma bufs */
/* Things like render on one GPU and encode with another is possible
 * but not supported now. */
/* One suggestion about dma bufs and GLX, one can use the DRI3
 * extension to get dma buffs for pixmaps */

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>

#include <epoxy/gl.h>

#include "arch.h"
#include "os_calls.h"
#include "string_calls.h"
#include "xorgxrdp_helper.h"
#include "xorgxrdp_helper_x11.h"
#include "xorgxrdp_helper_glx.h"
#include "xorgxrdp_helper_egl.h"
#include "log.h"

#if defined(XRDP_NVENC)
#include "xorgxrdp_helper_nvenc.h"
#endif

#if defined(XRDP_YAMI)
#include "xorgxrdp_helper_yami.h"
#endif

/* X11 */
Display *g_display = NULL;
static int g_x_socket = 0;
int g_screen_num = 0;
static Screen *g_screen = NULL;
Window g_root_window = None;
static Visual *g_vis = NULL;
static GC g_gc;

/* encoders: nvenc or yami */
struct enc_funcs
{
    int (*init)(void);
    int (*create_enc)(int width, int height, int tex, int tex_format,
                      struct enc_info **ei);
    int (*destroy_enc)(struct enc_info *ei);
    enum encoder_result (*encode)(struct enc_info *ei, int tex,
                                  void *cdata, int *cdata_bytes);
};

static struct enc_funcs g_enc_funcs[] =
{
    {
#if defined(XRDP_YAMI)
        xorgxrdp_helper_yami_init,
        xorgxrdp_helper_yami_create_encoder,
        xorgxrdp_helper_yami_delete_encoder,
        xorgxrdp_helper_yami_encode
#else
        NULL, NULL, NULL, NULL
#endif
    },
    {
#if defined(XRDP_NVENC)
        xorgxrdp_helper_nvenc_init,
        xorgxrdp_helper_nvenc_create_encoder,
        xorgxrdp_helper_nvenc_delete_encoder,
        xorgxrdp_helper_nvenc_encode
#else
        NULL, NULL, NULL, NULL
#endif
    }
};

/* GL interface: EGL or GLX */
struct inf_funcs
{
    int (*init)(void);
    int (*create_image)(Pixmap pixmap, inf_image_t *inf_image);
    int (*destroy_image)(inf_image_t inf_image);
    int (*bind_tex_image)(inf_image_t inf_image);
    int (*release_tex_image)(inf_image_t inf_image);
};

static struct inf_funcs g_inf_funcs[] =
{
    {
        xorgxrdp_helper_inf_egl_init,
        xorgxrdp_helper_inf_egl_create_image,
        xorgxrdp_helper_inf_egl_destroy_image,
        xorgxrdp_helper_inf_egl_bind_tex_image,
        xorgxrdp_helper_inf_egl_release_tex_image
    },
    {
        xorgxrdp_helper_inf_glx_init,
        xorgxrdp_helper_inf_glx_create_image,
        xorgxrdp_helper_inf_glx_destroy_image,
        xorgxrdp_helper_inf_glx_bind_tex_image,
        xorgxrdp_helper_inf_glx_release_tex_image
    }
};

/* 0 = EGL, 1 = GLX */
/* 0 = yami, 1 = nvenc */
#define INF_EGL     0
#define INF_GLX     1
#define ENC_YAMI    0
#define ENC_NVENC   1
static int g_inf = INF_EGL;
static int g_enc = ENC_YAMI;

struct mon_info
{
    int width;
    int height;
    Pixmap pixmap;
    inf_image_t inf_image;
    GLuint bmp_texture;
    GLuint enc_texture;
    int tex_format[2];
    int num_tex_formats;
    GLfloat *(*get_vertices)(GLuint *vertices_bytes,
                             GLuint *vertices_pointes,
                             int num_crects, struct xh_rect *crects,
                             int width, int height);
    struct xh_rect viewport;
    struct enc_info *ei;
};

#define MAX_MON 16
static struct mon_info g_mons[MAX_MON];

static GLuint g_quad_vao = 0;
static GLuint g_fb = 0;

#define XH_SHADERCOPY           0
#define XH_SHADERRGB2YUV420     1
#define XH_SHADERRGB2YUV422     2
#define XH_SHADERRGB2YUV444     3
#define XH_SHADERRGB2YUV420MV   4
#define XH_SHADERRGB2YUV420AV   5
#define XH_SHADERRGB2YUV420AVV2 6

#define XH_NUM_SHADERS 7

struct shader_info
{
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLint tex_loc;
    GLint tex_size_loc;
    GLint ymath_loc;
    GLint umath_loc;
    GLint vmath_loc;
    int current_matrix;
};
static struct shader_info g_si[XH_NUM_SHADERS];

/* *INDENT-OFF* */
static const GLfloat g_vertices[] =
{
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
     1.0f, -1.0f
};
/* *INDENT-ON* */

struct rgb2yuv_matrix
{
    GLfloat ymath[4];
    GLfloat umath[4];
    GLfloat vmath[4];
};

static struct rgb2yuv_matrix g_rgb2yux_matrix[3] =
{
    {
        /* yuv bt601 lagecy */
        {  66.0 / 256.0,  129.0 / 256.0,   25.0 / 256.0,   16.0 / 256.0 },
        { -38.0 / 256.0,  -74.0 / 256.0,  112.0 / 256.0,  128.0 / 256.0 },
        { 112.0 / 256.0,  -94.0 / 256.0,  -18.0 / 256.0,  128.0 / 256.0 }
    },
    {
        /* yuv bt709 full range, used in gfx h264 */
        {  54.0 / 256.0,  183.0 / 256.0,   18.0 / 256.0,    0.0 / 256.0 },
        { -29.0 / 256.0,  -99.0 / 256.0,  128.0 / 256.0,  128.0 / 256.0 },
        { 128.0 / 256.0, -116.0 / 256.0,  -12.0 / 256.0,  128.0 / 256.0 }
    },
    {
        /* yuv remotefx and gfx progressive remotefx */
        {   0.299000,       0.587000,       0.114000,       0.0 },
        {  -0.168935,      -0.331665,       0.500590,       0.5 },
        {   0.499830,      -0.418531,      -0.081282,       0.5 }
    }
};

#include "xorgxrdp_helper_shaders.c"

/*****************************************************************************/
int
xorgxrdp_helper_x11_init(void)
{
    const GLchar *vsource[XH_NUM_SHADERS];
    const GLchar *fsource[XH_NUM_SHADERS];
    GLint linked;
    GLint compiled;
    GLint vlength;
    GLint flength;
    GLuint quad_vbo;
    int index;
    int gl_ver;
    int major_opcode, first_event, first_error;

    /* x11 */
    g_display = XOpenDisplay(0);
    if (g_display == NULL)
    {
        return 1;
    }
    g_x_socket = XConnectionNumber(g_display);
    g_screen_num = DefaultScreen(g_display);
    g_screen = ScreenOfDisplay(g_display, g_screen_num);
    g_root_window = RootWindowOfScreen(g_screen);
    g_vis = XDefaultVisual(g_display, g_screen_num);
    g_gc = DefaultGC(g_display, 0);
    if (XQueryExtension(g_display, "NV-CONTROL", &major_opcode, &first_event,
                        &first_error))
    {
        LOG(LOG_LEVEL_INFO, "detected NVIDIA XServer");
        g_inf = INF_GLX;
        g_enc = ENC_NVENC;
        if (g_inf_funcs[g_inf].init() != 0)
        {
            LOG(LOG_LEVEL_ERROR, "GLX init failed");
            return 1;
        }
        LOG(LOG_LEVEL_INFO, "using GLX");
    }
    else
    {
        g_inf = INF_EGL;
        g_enc = ENC_YAMI;
        if (g_inf_funcs[g_inf].init() != 0)
        {
            LOG(LOG_LEVEL_ERROR, "EGL init failed");
            return 1;
        }
        LOG(LOG_LEVEL_INFO, "using EGL");
    }
    gl_ver = epoxy_gl_version();
    LOG(LOG_LEVEL_INFO, "gl_ver %d", gl_ver);
    if (gl_ver < 30)
    {
        LOG(LOG_LEVEL_ERROR, "gl_ver too old %d", gl_ver);
        return 1;
    }
    LOG(LOG_LEVEL_INFO, "vendor: %s",
        (const char *) glGetString(GL_VENDOR));
    LOG(LOG_LEVEL_INFO, "version: %s",
        (const char *) glGetString(GL_VERSION));
    /* create vertex array */
    glGenVertexArrays(1, &g_quad_vao);
    glBindVertexArray(g_quad_vao);
    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), g_vertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, NULL);
    glGenFramebuffers(1, &g_fb);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDeleteBuffers(1, &quad_vbo);

    /* create copy shader */
    vsource[XH_SHADERCOPY] = g_vs;
    fsource[XH_SHADERCOPY] = g_fs_copy;
    /* create rgb2yuv shader */
    vsource[XH_SHADERRGB2YUV420] = g_vs;
    fsource[XH_SHADERRGB2YUV420] = g_fs_rgb_to_yuv420;
    /* create rgb2yuv shader */
    vsource[XH_SHADERRGB2YUV422] = g_vs;
    fsource[XH_SHADERRGB2YUV422] = g_fs_rgb_to_yuv422;
    /* create rgb2yuv shader */
    vsource[XH_SHADERRGB2YUV444] = g_vs;
    fsource[XH_SHADERRGB2YUV444] = g_fs_rgb_to_yuv444;

    vsource[XH_SHADERRGB2YUV420MV] = g_vs;
    fsource[XH_SHADERRGB2YUV420MV] = g_fs_rgb_to_yuv420_mv;

    vsource[XH_SHADERRGB2YUV420AV] = g_vs;
    fsource[XH_SHADERRGB2YUV420AV] = g_fs_rgb_to_yuv420_av;

    vsource[XH_SHADERRGB2YUV420AVV2] = g_vs;
    fsource[XH_SHADERRGB2YUV420AVV2] = g_fs_rgb_to_yuv420_av_v2;

    for (index = 0; index < XH_NUM_SHADERS; index++)
    {
        g_si[index].vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        g_si[index].fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        vlength = g_strlen(vsource[index]);
        flength = g_strlen(fsource[index]);
        glShaderSource(g_si[index].vertex_shader, 1,
                       &(vsource[index]), &vlength);
        glShaderSource(g_si[index].fragment_shader, 1,
                       &(fsource[index]), &flength);
        glCompileShader(g_si[index].vertex_shader);
        glGetShaderiv(g_si[index].vertex_shader, GL_COMPILE_STATUS,
                      &compiled);
        LOG(LOG_LEVEL_INFO, "vertex_shader compiled %d", compiled);
        glCompileShader(g_si[index].fragment_shader);
        glGetShaderiv(g_si[index].fragment_shader, GL_COMPILE_STATUS,
                      &compiled);
        LOG(LOG_LEVEL_INFO, "fragment_shader compiled %d", compiled);
        g_si[index].program = glCreateProgram();
        glAttachShader(g_si[index].program, g_si[index].vertex_shader);
        glAttachShader(g_si[index].program, g_si[index].fragment_shader);
        glLinkProgram(g_si[index].program);
        glGetProgramiv(g_si[index].program, GL_LINK_STATUS, &linked);
        LOG(LOG_LEVEL_INFO, "linked %d", linked);
        g_si[index].tex_loc =
            glGetUniformLocation(g_si[index].program, "tex");
        g_si[index].tex_size_loc =
            glGetUniformLocation(g_si[index].program, "tex_size");
        g_si[index].ymath_loc =
            glGetUniformLocation(g_si[index].program, "ymath");
        g_si[index].umath_loc =
            glGetUniformLocation(g_si[index].program, "umath");
        g_si[index].vmath_loc =
            glGetUniformLocation(g_si[index].program, "vmath");
        LOG(LOG_LEVEL_INFO, "tex_loc %d "
            "tex_size_loc %d ymath_loc %d umath_loc %d vmath_loc %d",
            g_si[index].tex_loc, g_si[index].tex_size_loc,
            g_si[index].ymath_loc, g_si[index].umath_loc,
            g_si[index].vmath_loc);
        /* set default matrix */
        glUseProgram(g_si[index].program);
        if (g_si[index].ymath_loc >= 0)
        {
            glUniform4fv(g_si[index].ymath_loc, 1, g_rgb2yux_matrix[1].ymath);
        }
        if (g_si[index].umath_loc >= 0)
        {
            glUniform4fv(g_si[index].umath_loc, 1, g_rgb2yux_matrix[1].umath);
        }
        if (g_si[index].vmath_loc >= 0)
        {
            glUniform4fv(g_si[index].vmath_loc, 1, g_rgb2yux_matrix[1].vmath);
        }
        glUseProgram(0);
    }
    g_memset(g_mons, 0, sizeof(g_mons));
    if (g_enc_funcs[g_enc].init() != 0)
    {
        LOG(LOG_LEVEL_ERROR, "encoder init failed");
        return 1;
    }
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_get_wait_objs(intptr_t *objs, int *obj_count)
{
    objs[*obj_count] = g_x_socket;
    (*obj_count)++;
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_check_wait_objs(void)
{
    XEvent xevent;

    while (XPending(g_display) > 0)
    {
        LOG_DEVEL(LOG_LEVEL_INFO, "loop");
        XNextEvent(g_display, &xevent);
    }
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_delete_all_pixmaps(void)
{
    int index;
    struct mon_info *mi;

    for (index = 0; index < MAX_MON; index++)
    {
        mi = g_mons + index;
        if (mi->pixmap != 0)
        {
            g_enc_funcs[g_enc].destroy_enc(mi->ei);
            glDeleteTextures(1, &(mi->bmp_texture));
            glDeleteTextures(1, &(mi->enc_texture));
            g_inf_funcs[g_inf].destroy_image(mi->inf_image);
            XFreePixmap(g_display, mi->pixmap);
            mi->pixmap = 0;
        }
    }
    return 0;
}

/*****************************************************************************/
static GLfloat *
get_vertices_all(GLuint *vertices_bytes, GLuint *vertices_pointes,
                 int num_crects, struct xh_rect *crects,
                 int width, int height)
{
    GLfloat *vertices;

    (void)num_crects;
    (void)crects;
    (void)width;
    (void)height;

    vertices = g_new(GLfloat, 12);
    if (vertices == NULL)
    {
        return NULL;
    }
    vertices[0]  = -1;
    vertices[1]  =  1;
    vertices[2]  = -1;
    vertices[3]  = -1;
    vertices[4]  =  1;
    vertices[5]  =  1;
    vertices[6]  = -1;
    vertices[7]  = -1;
    vertices[8]  =  1;
    vertices[9]  =  1;
    vertices[10] =  1;
    vertices[11] = -1;
    *vertices_bytes = sizeof(GLfloat) * 12;
    *vertices_pointes = 6;
    return vertices;
}

/*****************************************************************************/
static GLfloat *
get_vertices420(GLuint *vertices_bytes, GLuint *vertices_pointes,
                int num_crects, struct xh_rect *crects,
                int width, int height)
{
    GLfloat *vertices;
    GLfloat *vert;
    GLfloat x1;
    GLfloat x2;
    GLfloat y1;
    GLfloat y2;
    int index;
    GLfloat fwidth;
    GLfloat fheight;
    const GLfloat fac13 = 1.0 / 3.0;
    const GLfloat fac23 = 2.0 / 3.0;
    const GLfloat fac43 = 4.0 / 3.0;
    struct xh_rect *crect;

    if (num_crects < 1)
    {
        return get_vertices_all(vertices_bytes, vertices_pointes,
                                num_crects, crects, width, height);
    }
    vertices = g_new(GLfloat, num_crects * 24);
    if (vertices == NULL)
    {
        return NULL;
    }
    fwidth = width  / 2.0;
    fheight = height / 2.0;
    for (index = 0; index < num_crects; index++)
    {
        crect = crects + index;
        LOG_DEVEL(LOG_LEVEL_INFO, "rect index %d x %d y %d w %d h %d",
                  index, crect->x, crect->y, crect->w, crect->h);
        x1 = crect->x / fwidth;
        y1 = crect->y / fheight;
        x2 = (crect->x + crect->w) / fwidth;
        y2 = (crect->y + crect->h) / fheight;
        vert = vertices + index * 24;
        /* y box */
        vert[0]  =  x1 - 1.0;
        vert[1]  =  y1 * fac23 - 1.0;
        vert[2]  =  x1 - 1.0;
        vert[3]  =  y2 * fac23 - 1.0;
        vert[4]  =  x2 - 1.0;
        vert[5]  =  y1 * fac23 - 1.0;
        vert[6]  =  x1 - 1.0;
        vert[7]  =  y2 * fac23 - 1.0;
        vert[8]  =  x2 - 1.0;
        vert[9]  =  y1 * fac23 - 1.0;
        vert[10] =  x2 - 1.0;
        vert[11] =  y2 * fac23 - 1.0;
        /* uv box */
        vert[12] =  x1 - 1.0;
        vert[13] = (y1 * fac13 + fac43) - 1.0;
        vert[14] =  x1 - 1.0;
        vert[15] = (y2 * fac13 + fac43) - 1.0;
        vert[16] =  x2 - 1.0;
        vert[17] = (y1 * fac13 + fac43) - 1.0;
        vert[18] =  x1  - 1.0;
        vert[19] = (y2 * fac13 + fac43) - 1.0;
        vert[20] =  x2 - 1.0;
        vert[21] = (y1 * fac13 + fac43) - 1.0;
        vert[22] =  x2 - 1.0;
        vert[23] = (y2 * fac13 + fac43) - 1.0;
    }
    *vertices_bytes = sizeof(GLfloat) * num_crects * 24;
    *vertices_pointes = num_crects * 12;
    return vertices;
}

/*****************************************************************************/
static GLfloat *
get_vertices444(GLuint *vertices_bytes, GLuint *vertices_pointes,
                int num_crects, struct xh_rect *crects,
                int width, int height)
{
    GLfloat *vertices;
    GLfloat *vert;
    GLfloat x1;
    GLfloat x2;
    GLfloat y1;
    GLfloat y2;
    int index;
    GLfloat fwidth;
    GLfloat fheight;
    struct xh_rect *crect;

    if (num_crects < 1)
    {
        return get_vertices_all(vertices_bytes, vertices_pointes,
                                num_crects, crects, width, height);
    }
    vertices = g_new(GLfloat, num_crects * 12);
    if (vertices == NULL)
    {
        return NULL;
    }
    fwidth = width  / 2.0;
    fheight = height / 2.0;
    for (index = 0; index < num_crects; index++)
    {
        crect = crects + index;
        x1 = crect->x / fwidth;
        y1 = crect->y / fheight;
        x2 = (crect->x + crect->w) / fwidth;
        y2 = (crect->y + crect->h) / fheight;
        vert = vertices + index * 12;
        vert[0]  = x1 - 1.0;
        vert[1]  = y1 - 1.0;
        vert[2]  = x1 - 1.0;
        vert[3]  = y2 - 1.0;
        vert[4]  = x2 - 1.0;
        vert[5]  = y1 - 1.0;
        vert[6]  = x1 - 1.0;
        vert[7]  = y2 - 1.0;
        vert[8]  = x2 - 1.0;
        vert[9]  = y1 - 1.0;
        vert[10] = x2 - 1.0;
        vert[11] = y2 - 1.0;
    }
    *vertices_bytes = sizeof(GLfloat) * num_crects * 12;
    *vertices_pointes = num_crects * 6;
    return vertices;
}

/*****************************************************************************/
int
xorgxrdp_helper_x11_create_pixmap(int width, int height, int magic,
                                  int con_id, int mon_id)
{
    struct mon_info *mi;
    Pixmap pixmap;
    XImage *ximage;
    int img[64];
    inf_image_t inf_image;
    GLuint bmp_texture;
    GLuint enc_texture;

    mi = g_mons + mon_id % MAX_MON;
    if (mi->pixmap != 0)
    {
        LOG(LOG_LEVEL_ERROR, "error already setup");
        return 1;
    }
    LOG(LOG_LEVEL_INFO, "width %d height %d, "
        "magic 0x%8.8x, con_id %d mod_id %d", width, height,
        magic, con_id, mon_id);
    pixmap = XCreatePixmap(g_display, g_root_window, width, height, 24);
    LOG(LOG_LEVEL_INFO, "pixmap %d", (int) pixmap);

    if (g_inf_funcs[g_inf].create_image(pixmap, &inf_image) != 0)
    {
        return 1;
    }
    LOG(LOG_LEVEL_INFO, "inf_image %p", (void *) inf_image);

    g_memset(img, 0, sizeof(img));
    img[0] = magic;
    img[1] = con_id;
    img[2] = mon_id;
    ximage = XCreateImage(g_display, g_vis, 24, ZPixmap, 0, (char *) img,
                          4, 4, 32, 0);
    XPutImage(g_display, pixmap, g_gc, ximage, 0, 0, 0, 0, 4, 4);
    XFree(ximage);

    glEnable(GL_TEXTURE_2D);
    /* texture that gets encoded */
    glGenTextures(1, &enc_texture);
    glBindTexture(GL_TEXTURE_2D, enc_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (g_enc == ENC_NVENC)
    {
#if XRDP_AVC444
        mi->tex_format[0] = XH_YUV444_V2_MV;
        mi->tex_format[1] = XH_YUV444_V2_AUX;
        mi->num_tex_formats = 2;
        LOG(LOG_LEVEL_INFO, "using XH_YUV444_V2_MV and XH_YUV444_V2_AUX");
#else
        mi->tex_format[0] = XH_YUV420;
        mi->num_tex_formats = 1;
        LOG(LOG_LEVEL_INFO, "using XH_YUV420");
#endif
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height * 3 / 2, 0,
                     GL_RED, GL_UNSIGNED_BYTE, NULL);
        mi->get_vertices = get_vertices420;
        mi->viewport.x = 0;
        mi->viewport.y = 0;
        mi->viewport.w = width;
        mi->viewport.h = height * 3 / 2;
    }
    else if (g_enc == ENC_YAMI)
    {
        LOG(LOG_LEVEL_INFO, "using XH_YUV422");
        mi->tex_format[0] = XH_YUV422;
        mi->num_tex_formats = 1;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width / 2, height, 0,
                     GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, NULL);
        mi->get_vertices = get_vertices444; /* same as 444 */
        mi->viewport.x = 0;
        mi->viewport.y = 0;
        mi->viewport.w = width / 2;
        mi->viewport.h = height;
    }
    else
    {
        LOG(LOG_LEVEL_INFO, "using XH_YUV444");
        mi->tex_format[0] = XH_YUV444;
        mi->num_tex_formats = 1;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, NULL);
        mi->get_vertices = get_vertices444;
        mi->viewport.x = 0;
        mi->viewport.y = 0;
        mi->viewport.w = width;
        mi->viewport.h = height;
    }
    /* texture that binds with pixmap */
    glGenTextures(1, &bmp_texture);
    glBindTexture(GL_TEXTURE_2D, bmp_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (g_enc_funcs[g_enc].create_enc(width, height,
                                      enc_texture, mi->tex_format[0],
                                      &(mi->ei)) != 0)
    {
        return 1;
    }

    mi->pixmap = pixmap;
    mi->inf_image = inf_image;
    mi->enc_texture = enc_texture;
    mi->width = width;
    mi->height = height;
    mi->bmp_texture = bmp_texture;

    return 0;
}

/*****************************************************************************/
enum encoder_result
xorgxrdp_helper_x11_encode_pixmap(int left, int top, int width, int height,
                                  int mon_id, int num_crects,
                                  struct xh_rect *crects,
                                  char *cdata, int *cdata_bytes)
{
    struct mon_info *mi;
    struct shader_info *si;
    enum encoder_result rv;
    GLuint vao;
    GLuint vbo;
    GLfloat *vertices;
    GLuint vertices_bytes;
    GLuint vertices_pointes;

    mi = g_mons + mon_id % MAX_MON;
    int running_size = 0;
    if ((width != mi->width) || (height != mi->height))
    {
        LOG(LOG_LEVEL_ERROR, "error width %d should be %d "
            "height %d should be %d",
            width, mi->width, height, mi->height);
        return ENCODER_ERROR;
    }
    char *enc_write_location;

    /* rgb to yuv */
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mi->bmp_texture);
    g_inf_funcs[g_inf].bind_tex_image(mi->inf_image);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, mi->enc_texture, 0);

    /* setup vertices from crects */
    vertices = mi->get_vertices(&vertices_bytes, &vertices_pointes,
                                num_crects, crects, width, height);
    if (vertices == NULL)
    {
        LOG(LOG_LEVEL_ERROR, "error get_vertices failed num_crects %d",
            num_crects);
        return ENCODER_ERROR;
    }
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices_bytes, vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, NULL);
    glViewport(mi->viewport.x, mi->viewport.y, mi->viewport.w, mi->viewport.h);
    XFlush(g_display);

    for (int i = 0; i < mi->num_tex_formats; ++i)
    {
        int index = mi->tex_format[i] % XH_NUM_SHADERS;
        si = g_si + index;
        LOG(LOG_LEVEL_TRACE, "Running shader ID: %d", index);
        glUseProgram(si->program);
        /* uniforms */
        glUniform2f(si->tex_size_loc, mi->width, mi->height);
        /* draw */
        glDrawArrays(GL_TRIANGLES, 0, vertices_pointes);
        /* sync before encoding */
        glFinish();
        /* encode */
        int quad_index = 4 * (i + 1);
        enc_write_location = cdata + running_size + quad_index;
        rv = g_enc_funcs[g_enc].encode(mi->ei, mi->enc_texture,
                                       enc_write_location, &(cdata_bytes[i]));
        if (rv == ENCODER_ERROR)
        {
            return ENCODER_ERROR;
        }
        running_size += cdata_bytes[i];
    }

    /* cleanup */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    g_free(vertices);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_inf_funcs[g_inf].release_tex_image(mi->inf_image);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    //XFlush(g_display);
    glFinish();

    return INCREMENTAL_FRAME_ENCODED;
}
