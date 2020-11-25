/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2019 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_XBOX_PBKIT

#include "SDL_hints.h"
#include "SDL_assert.h"
#include "../SDL_sysrender.h"

#include <stdlib.h>
#include <math.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <windows.h>
#include <x86intrin.h>

#define PB_MAXRAM 0x03FFAFFF
#define PB_MAXZ 16777215.f

#undef MASK
#define MASK(mask, val) (((val) << (__builtin_ffs(mask)-1)) & (mask))

#define NV097_SET_SPECULAR_ENABLE 0x000003B8
#define NV2A_VERTEX_ATTR_DIFFUSE  3
#define NV2A_VERTEX_ATTR_TEXTURE0 9
#define NV2A_VERTEX_ATTR_TEXTURE1 10

static float mat_identity[16] =
{
    1.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 0.f,
    0.f, 0.f, 1.f, 0.f,
    0.f, 0.f, 0.f, 1.f,
};

typedef struct
{
    SDL_BlendMode cur_blendmode;
    SDL_Texture *cur_texture;
    SDL_Rect cur_viewport;
    Uint32 cur_color_word;
    float cur_color[4];
    unsigned int buf_width;
    unsigned int buf_height;
    SDL_bool vsync;
    SDL_bool rendering;
} XBOX_PB_RenderData;

typedef struct
{
    unsigned int format;
    unsigned int width;
    unsigned int height;
    unsigned int size;
    unsigned int pitch;
    unsigned int bytespp;
    unsigned int filter;
    unsigned int addr;
    SDL_bool is_aligned;
    void *data;
} XBOX_PB_TextureData;

/* Construct a viewport transformation matrix */
/* Assumes that znear = 0, zfar = 1 because we have no use for Z */
static inline void
MatrixViewport(float *out, float x, float y, float width, float height)
{
    SDL_memset(out, 0, 4 * 4 * sizeof (float));
    out[ 0] = width / 2.0f;
    out[ 5] = height / -2.0f;
    out[10] = 1.0f;
    out[12] = x + width / 2.0f;
    out[13] = y + height / 2.0f;
    out[15] = 1.0f;
}

/* Construct an orthographic projection matrix */
/* Assumes that znear = 0, zfar = 1 because we have no use for Z */
static inline void
MatrixOrtho(float *out, float width, float height)
{
    SDL_memset(out, 0, 4 * 4 * sizeof (float));
    out[ 0] = 2.0f / width;
    out[ 5] = -2.0f / height;
    out[10] = 1.f;
    out[12] = -1.0f;
    out[13] = 1.0f;
    out[15] = 1.0f;
}

/* Multiply two 4x4 matrices into a different result matrix */
static inline void
MatrixMultiply(float *restrict out, const float *a, const float *b)
{
    for (int i = 0; i < 16; i += 4) {
        for (int j = 0; j < 4; j++) {
            out[i + j] =
                a[i + 0] * b[ 0 + j] +
                a[i + 1] * b[ 4 + j] +
                a[i + 2] * b[ 8 + j] +
                a[i + 3] * b[12 + j];
        }
    }
}

static inline unsigned int
PixelFormatToNV(const Uint32 format, const int swizzled)
{
    switch (format) {
    case SDL_PIXELFORMAT_RGB565:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5;
    case SDL_PIXELFORMAT_ARGB1555:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5;
    case SDL_PIXELFORMAT_ARGB4444:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4;
    case SDL_PIXELFORMAT_RGBA8888:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8;
    case SDL_PIXELFORMAT_ABGR8888:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8;
    case SDL_PIXELFORMAT_BGRA8888:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8;
    case SDL_PIXELFORMAT_ARGB8888:
        return NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8;
    default:
        return 0;
    }
}

/* Slightly gutted SDL_memcpySSE */
static void
FastTextureUpdate(XBOX_PB_TextureData * xtex, const Uint8 * src)
{
    int i;
    Uint8 *dst = xtex->data;
    __m128 values[4];
    for (i = xtex->size / 64; i--;) {
        _mm_prefetch(src, _MM_HINT_NTA);
        values[0] = *(__m128 *) (src + 0);
        values[1] = *(__m128 *) (src + 16);
        values[2] = *(__m128 *) (src + 32);
        values[3] = *(__m128 *) (src + 48);
        _mm_stream_ps((float *) (dst + 0), values[0]);
        _mm_stream_ps((float *) (dst + 16), values[1]);
        _mm_stream_ps((float *) (dst + 32), values[2]);
        _mm_stream_ps((float *) (dst + 48), values[3]);
        src += 64;
        dst += 64;
    }
}

static void
SetBlendMode(XBOX_PB_RenderData *data, int blendmode)
{
    if (blendmode != data->cur_blendmode) {
        Uint32 *p = pb_begin();

        p = pb_push1(p, NV097_SET_BLEND_ENABLE, (blendmode != SDL_BLENDMODE_NONE));

        switch (blendmode) {
        case SDL_BLENDMODE_BLEND:
            p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR,
                NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA);
            p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR,
                NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA);
            break;
        case SDL_BLENDMODE_ADD:
            p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR,
                NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA);
            p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR,
                NV097_SET_BLEND_FUNC_DFACTOR_V_ONE);
            break;
        case SDL_BLENDMODE_MOD:
            p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR,
                NV097_SET_BLEND_FUNC_SFACTOR_V_ZERO);
            p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR,
                NV097_SET_BLEND_FUNC_DFACTOR_V_SRC_COLOR);
            break;
        }

        pb_end(p);

        data->cur_blendmode = blendmode;
    }
}

static inline void
SetCombinerColor(void)
{
    Uint32 *p = pb_begin();
    #include "ps_color.inl"
    pb_end(p);
}

static inline void
SetCombinerTexture(void)
{
    Uint32 *p = pb_begin();
    #include "ps_texture.inl"
    pb_end(p);
}

static inline void
SetTexture(XBOX_PB_RenderData *data, SDL_Texture *texture)
{
    if (texture != data->cur_texture) {
        if (texture) {
            XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;
            Uint32 *p = pb_begin();
            p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(0), 0x40000000); /* enable tex0 */
            p = pb_push2(p, NV20_TCL_PRIMITIVE_3D_TX_OFFSET(0), xtex->addr, xtex->format);
            p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(0), xtex->pitch << 16);
            p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(0), (xtex->width << 16) | xtex->height);
            p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_FILTER(0), xtex->filter);
            pb_end(p);
            SetCombinerTexture();
        } else {
            Uint32 *p = pb_begin();
            p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(0), 0x0003FFC0); /* disable tex0 */
            pb_end(p);
            SetCombinerColor();
        }
        data->cur_texture = texture;
    }
}

static inline void
SetViewport(XBOX_PB_RenderData *data, const SDL_Rect vrect)
{
    float mview[16], mortho[16], mproj[16];
    if (SDL_memcmp(&data->cur_viewport, &vrect, sizeof (SDL_Rect)) != 0) {
        /* construct viewport matrix */
        MatrixViewport(mview, vrect.x, -vrect.y, vrect.w, vrect.h);
        /* construct orthographic projection matrix */
        MatrixOrtho(mortho, vrect.w, vrect.h);
        /* combine them into a single projection matrix */
        MatrixMultiply(mproj, mortho, mview);
        /* update the projection matrix */
        Uint32 *p = pb_begin();
        p = pb_push_transposed_matrix(p, NV097_SET_PROJECTION_MATRIX, mproj);
        pb_end(p);
        data->cur_viewport = vrect;
    }
}

static inline void
StartDrawing(XBOX_PB_RenderData *data)
{
    if (!data->rendering) {
        pb_reset();
        pb_target_back_buffer();
        pb_erase_depth_stencil_buffer(0, 0, data->buf_width, data->buf_height);
        while (pb_busy());
        data->rendering = SDL_TRUE;
    }
}

static inline void
EndDrawing(XBOX_PB_RenderData *data)
{
    if (data->rendering) {
        while (pb_busy());
        while (pb_finished());
        data->rendering = SDL_FALSE; /* rendering has finished */
    }
}

static void
XBOX_PB_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent * event)
{
}

static int
XBOX_PB_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    const unsigned int fmt = PixelFormatToNV(texture->format, SDL_FALSE);

    if (fmt == 0)
    {
        /* unsupported format; don't even bother with anything else */
        SDL_SetError("unsupported texture format: 0x%08x", texture->format);
        return -1;
    }

    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) SDL_calloc(1, sizeof(XBOX_PB_TextureData));

    if (!xtex)
    {
        SDL_OutOfMemory();
        return -1;
    }

    xtex->width = texture->w;
    xtex->height = texture->h;
    xtex->format = MASK(NV097_SET_TEXTURE_FORMAT_COLOR, fmt) |
        MASK(NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY, 2) |
        MASK(NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS, 1) |
        0xA; /* dma context and other shit here, presumably */
    xtex->filter = (texture->scaleMode == SDL_ScaleModeNearest) ? 0x01014000 : 0x02072000;
    xtex->bytespp = SDL_BYTESPERPIXEL(texture->format);
    xtex->pitch = xtex->bytespp * xtex->width;
    xtex->size = xtex->height * xtex->pitch;
    xtex->data = MmAllocateContiguousMemoryEx(xtex->size, 0, PB_MAXRAM, 16, 0x404);
    xtex->is_aligned = (xtex->size & 63) == 0;

    if (!xtex->data)
    {
        SDL_free(xtex);
        SDL_OutOfMemory();
        return -1;
    }

    xtex->addr = ((unsigned int) xtex->data) & 0x03FFFFFF;

    SDL_memset(xtex->data, 0, xtex->size);

    texture->driverdata = xtex;

    return 0;
}

static int
XBOX_PB_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                      const SDL_Rect *rect, const void *pixels, int pitch)
{
    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;
    const Uint8 *src;
    Uint8 *dst;
    int row;
    size_t length;

    /* If it's a whole texture update, just do one big memcpy */
    if (rect->w == xtex->width && rect->h == xtex->height && pitch == xtex->pitch) {
        /* If src is 16 bytes aligned and everything else is 64 bytes aligned, use SSE copy */
        if (((unsigned int)pixels & 15) == 0 && xtex->is_aligned)
            FastTextureUpdate(xtex, pixels);
        else
            SDL_memcpy(xtex->data, pixels, xtex->size);
    } else {
        src = pixels;
        dst = (Uint8 *) xtex->data +
            rect->y * xtex->pitch +
            rect->x * xtex->bytespp;
        length = rect->w * xtex->bytespp;
        for (row = 0; row < rect->h; ++row) {
            SDL_memcpy(dst, src, length);
            src += pitch;
            dst += xtex->pitch;
        }
    }

    return 0;
}

static int
XBOX_PB_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                 const SDL_Rect *rect, void **pixels, int *pitch)
{
    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;

    if (!xtex->data) {
        SDL_SetError("texture with NULL data");
        return -1;
    }

    *pixels = (void *) ((Uint8 *) xtex->data + rect->y * xtex->pitch +
        rect->x * xtex->bytespp);

    *pitch = xtex->pitch;

    return 0;
}

static void
XBOX_PB_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;
    SDL_Rect rect;

    /* We do whole texture updates, at least for now */
    rect.x = 0;
    rect.y = 0;
    rect.w = texture->w;
    rect.h = texture->h;
    XBOX_PB_UpdateTexture(renderer, texture, &rect, xtex->data, xtex->pitch);
}

static int
XBOX_PB_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return 0;
}

static int
XBOX_PB_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    /* no op */
    return 0;
}

static int
XBOX_PB_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    /* no op */
    return 0;
}

static int
XBOX_PB_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint * points, int count)
{
    float *verts = (float *) SDL_AllocateRenderVertices(renderer, count * 2 * sizeof (float), 0, &cmd->data.draw.first);
    int i;

    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = count;
    for (i = 0; i < count; i++) {
        *(verts++) = 0.5f + points[i].x;
        *(verts++) = 0.5f + points[i].y;
    }

    return 0;
}

static int
XBOX_PB_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FRect * rects, int count)
{
    float *verts = (float *) SDL_AllocateRenderVertices(renderer, count * 8 * sizeof (float), 0, &cmd->data.draw.first);
    int i;

    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = count * 4;

    for (i = 0; i < count; i++) {
        const SDL_FRect *rect = &rects[i];
        const float minx = rect->x;
        const float maxx = rect->x + rect->w;
        const float miny = rect->y;
        const float maxy = rect->y + rect->h;

        *(verts++) = minx;
        *(verts++) = miny;

        *(verts++) = minx;
        *(verts++) = maxy;

        *(verts++) = maxx;
        *(verts++) = maxy;

        *(verts++) = maxx;
        *(verts++) = miny;
    }

    return 0;
}

static int
XBOX_PB_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
             const SDL_Rect * srcrect, const SDL_FRect * dstrect)
{
    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;
    float minx, miny, maxx, maxy;
    float minu, maxu, minv, maxv;
    float *verts = (float *) SDL_AllocateRenderVertices(renderer, 16 * sizeof (float), 0, &cmd->data.draw.first);

    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = 4;

    minx = dstrect->x;
    miny = dstrect->y;
    maxx = dstrect->x + dstrect->w;
    maxy = dstrect->y + dstrect->h;

    minu = (float) srcrect->x;
    maxu = (float) (srcrect->x + srcrect->w);
    minv = (float) srcrect->y;
    maxv = (float) (srcrect->y + srcrect->h);

    /* texcoords first, position last */

    *(verts++) = minu;
    *(verts++) = minv;
    *(verts++) = minx;
    *(verts++) = miny;

    *(verts++) = minu;
    *(verts++) = maxv;
    *(verts++) = minx;
    *(verts++) = maxy;

    *(verts++) = maxu;
    *(verts++) = maxv;
    *(verts++) = maxx;
    *(verts++) = maxy;

    *(verts++) = maxu;
    *(verts++) = minv;
    *(verts++) = maxx;
    *(verts++) = miny;

    return 0;
}

static int
XBOX_PB_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
               const SDL_Rect * srcrect, const SDL_FRect * dstrect,
               const double angle, const SDL_FPoint *center, const SDL_RendererFlip flip)
{
    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;
    float *verts = (float *) SDL_AllocateRenderVertices(renderer, 16 * sizeof (float), 0, &cmd->data.draw.first);
    const float centerx = center->x;
    const float centery = center->y;
    const float x = dstrect->x + centerx;
    const float y = dstrect->y + centery;
    const float width = dstrect->w - centerx;
    const float height = dstrect->h - centery;
    float s, c;

    float t;
    float u0 = srcrect->x;
    float v0 = srcrect->y;
    float u1 = srcrect->x + srcrect->w;
    float v1 = srcrect->y + srcrect->h;

    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = 4;

    const float anglerad = angle * M_PI / 180.f;
    s = sinf(anglerad);
    c = cosf(anglerad);

    const float cw = c * width;
    const float sw = s * width;
    const float ch = c * height;
    const float sh = s * height;

    if (flip & SDL_FLIP_VERTICAL) {
        t = v0;
        v0 = v1;
        v1 = t;
    }

    if (flip & SDL_FLIP_HORIZONTAL) {
        t = u0;
        u0 = u1;
        u1 = t;
    }

    /* texcoords first, positions second */

    *(verts++) = u0;
    *(verts++) = v0;
    *(verts++) = x - cw + sh;
    *(verts++) = y - sw - ch;

    *(verts++) = u0;
    *(verts++) = v1;
    *(verts++) = x - cw - sh;
    *(verts++) = y - sw + ch;

    *(verts++) = u1;
    *(verts++) = v1;
    *(verts++) = x + cw - sh;
    *(verts++) = y + sw + ch;

    *(verts++) = u1;
    *(verts++) = v0;
    *(verts++) = x + cw + sh;
    *(verts++) = y + sw - ch;

    return 0;
}

static inline void
DrawObjectsFlat(const Uint32 type, const float *verts, const size_t count, const float *cur_color) {
    Uint32 *p = pb_begin();
    p = pb_push1(p, NV097_SET_BEGIN_END, type);
    pb_end(p);

    for (Uint32 i = 0; i < count; ++i) {
        p = pb_begin();
            /* color; same for every vertex */
            pb_push(p++, NV097_SET_VERTEX_DATA4F_M
                + NV2A_VERTEX_ATTR_DIFFUSE * 4 * sizeof (float), 4);
            *(float*)(p++) = cur_color[0];
            *(float*)(p++) = cur_color[1];
            *(float*)(p++) = cur_color[2];
            *(float*)(p++) = cur_color[3];
            /* position */
            pb_push(p++, NV097_SET_VERTEX4F, 4);
            *(float*)(p++) = *(verts++);
            *(float*)(p++) = *(verts++);
            *(float*)(p++) = 0.f;
            *(float*)(p++) = 1.f;
        pb_end(p);
    }

    p = pb_begin();
    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    pb_end(p);
}

static inline void
DrawObjectsTextured(const Uint32 type, const float *verts, const size_t count, const float *cur_color) {
    Uint32 *p = pb_begin();
    p = pb_push1(p, NV097_SET_BEGIN_END, type);
    pb_end(p);

    for (Uint32 i = 0; i < count; ++i) {
        p = pb_begin();
            /* texcoords */
            pb_push(p++, NV097_SET_VERTEX_DATA2F_M
                + NV2A_VERTEX_ATTR_TEXTURE0 * 2 * sizeof(float), 2);
            *(float*)(p++) = *(verts++);
            *(float*)(p++) = *(verts++);
            /* color; same for every vertex */
            pb_push(p++, NV097_SET_VERTEX_DATA4F_M
                + NV2A_VERTEX_ATTR_DIFFUSE * 4 * sizeof (float), 4);
            *(float*)(p++) = cur_color[0];
            *(float*)(p++) = cur_color[1];
            *(float*)(p++) = cur_color[2];
            *(float*)(p++) = cur_color[3];
            /* position */
            pb_push(p++, NV097_SET_VERTEX4F, 4);
            *(float*)(p++) = *(verts++);
            *(float*)(p++) = *(verts++);
            *(float*)(p++) = 0.f;
            *(float*)(p++) = 1.f;
        pb_end(p);
    }

    p = pb_begin();
    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    pb_end(p);
}

static int
XBOX_PB_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    XBOX_PB_RenderData *data = (XBOX_PB_RenderData *) renderer->driverdata;
    size_t i;

    StartDrawing(data);

    Uint8 *vtxbuf = (Uint8 *) vertices;
    Uint32 *p;

    while (cmd) {
        switch (cmd->command) {
            case SDL_RENDERCMD_SETDRAWCOLOR: {
                const Uint8 r = cmd->data.color.r;
                const Uint8 g = cmd->data.color.g;
                const Uint8 b = cmd->data.color.b;
                const Uint8 a = cmd->data.color.a;
                const Uint32 color = ((a << 24) | (r << 16) | (g << 8) | b);
                if (color != data->cur_color_word) {
                    data->cur_color[0] = cmd->data.color.r / 255.f;
                    data->cur_color[1] = cmd->data.color.g / 255.f;
                    data->cur_color[2] = cmd->data.color.b / 255.f;
                    data->cur_color[3] = cmd->data.color.a / 255.f;
                    data->cur_color_word = color;
                }
                break;
            }

            case SDL_RENDERCMD_SETVIEWPORT: {
                SetViewport(data, cmd->data.viewport.rect);
                break;
            }

            case SDL_RENDERCMD_SETCLIPRECT: {
                /* !!! FIXME: don't know how to do scissor test yet */
                break;
            }

            case SDL_RENDERCMD_CLEAR: {
                const Uint8 r = cmd->data.color.r;
                const Uint8 g = cmd->data.color.g;
                const Uint8 b = cmd->data.color.b;
                const Uint8 a = cmd->data.color.a;
                const Uint32 color = ((a << 24) | (r << 16) | (g << 8) | b);
                pb_fill(0, 0, data->buf_width, data->buf_height, color);
                break;
            }

            case SDL_RENDERCMD_DRAW_POINTS: {
                SetTexture(data, NULL);
                SetBlendMode(data, cmd->data.draw.blend);
                DrawObjectsFlat(NV097_SET_BEGIN_END_OP_POINTS,
                    (const float *) (vtxbuf + cmd->data.draw.first),
                    cmd->data.draw.count, data->cur_color);
                break;
            }

            case SDL_RENDERCMD_DRAW_LINES: {
                SetTexture(data, NULL);
                SetBlendMode(data, cmd->data.draw.blend);
                DrawObjectsFlat(NV097_SET_BEGIN_END_OP_LINES,
                    (const float *) (vtxbuf + cmd->data.draw.first),
                    cmd->data.draw.count, data->cur_color);
                break;
            }

            case SDL_RENDERCMD_FILL_RECTS:  {
                SetTexture(data, NULL);
                SetBlendMode(data, cmd->data.draw.blend);
                DrawObjectsFlat(NV097_SET_BEGIN_END_OP_QUADS,
                    (const float *) (vtxbuf + cmd->data.draw.first),
                    cmd->data.draw.count, data->cur_color);
                break;
            }

            case SDL_RENDERCMD_COPY_EX:
            case SDL_RENDERCMD_COPY: {
                const size_t count = cmd->data.draw.count;
                const float *verts = (const float *) (vtxbuf + cmd->data.draw.first);
                SetTexture(data, cmd->data.draw.texture);
                SetBlendMode(data, cmd->data.draw.blend);
                DrawObjectsTextured(NV097_SET_BEGIN_END_OP_QUADS,
                    verts, count, data->cur_color);
                break;
            }

            case SDL_RENDERCMD_NO_OP:
                break;
        }

        cmd = cmd->next;
    }

    return 0;
}

static int
XBOX_PB_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect * rect,
    Uint32 pixel_format, void * pixels, int pitch)
{
    return SDL_Unsupported();
}

static void
XBOX_PB_RenderPresent(SDL_Renderer *renderer)
{
    XBOX_PB_RenderData *data = (XBOX_PB_RenderData *) renderer->driverdata;

    EndDrawing(data);

    if(data->vsync)
        pb_wait_for_vbl();
}

static void
XBOX_PB_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XBOX_PB_RenderData *renderdata = (XBOX_PB_RenderData *) renderer->driverdata;
    XBOX_PB_TextureData *xtex = (XBOX_PB_TextureData *) texture->driverdata;

    if (!renderdata)
        return;

    if (!xtex)
        return;

    if (renderdata->cur_texture == texture) {
        /* wait till all render operations with this texture finish */
        EndDrawing(renderdata);
        /* then unbind it */
        SetTexture(renderdata, NULL);
    }

    if (xtex->data)
        MmFreeContiguousMemory(xtex->data);

    SDL_free(xtex);

    texture->driverdata = NULL;
}

static void
XBOX_PB_DestroyRenderer(SDL_Renderer *renderer)
{
    XBOX_PB_RenderData *data = (XBOX_PB_RenderData *) renderer->driverdata;
    if (data) {
        EndDrawing(data);
        pb_kill();
        XVideoFlushFB();
        SDL_free(data);
    }
    SDL_free(renderer);
}

SDL_Renderer *
XBOX_PB_CreateRenderer(SDL_Window * window, Uint32 flags)
{
    SDL_Renderer *renderer;
    XBOX_PB_RenderData *data;
    int err;

    if ((err = pb_init()) != 0) {
        SDL_SetError("pb_init() returned %d", err);
        return NULL;
    }

    renderer = (SDL_Renderer *) SDL_calloc(1, sizeof(*renderer));
    if (!renderer) {
        SDL_OutOfMemory();
        return NULL;
    }

    data = (XBOX_PB_RenderData *) SDL_calloc(1, sizeof(*data));
    if (!data) {
        XBOX_PB_DestroyRenderer(renderer);
        SDL_OutOfMemory();
        return NULL;
    }

    renderer->WindowEvent = XBOX_PB_WindowEvent;
    renderer->CreateTexture = XBOX_PB_CreateTexture;
    renderer->UpdateTexture = XBOX_PB_UpdateTexture;
    renderer->LockTexture = XBOX_PB_LockTexture;
    renderer->UnlockTexture = XBOX_PB_UnlockTexture;
    renderer->SetRenderTarget = XBOX_PB_SetRenderTarget;
    renderer->QueueSetViewport = XBOX_PB_QueueSetViewport;
    renderer->QueueSetDrawColor = XBOX_PB_QueueSetDrawColor;
    renderer->QueueDrawPoints = XBOX_PB_QueueDrawPoints;
    renderer->QueueDrawLines = XBOX_PB_QueueDrawPoints;  /* lines and points queue vertices the same way. */
    renderer->QueueFillRects = XBOX_PB_QueueFillRects;
    renderer->QueueCopy = XBOX_PB_QueueCopy;
    renderer->QueueCopyEx = XBOX_PB_QueueCopyEx;
    renderer->RunCommandQueue = XBOX_PB_RunCommandQueue;
    renderer->RenderReadPixels = XBOX_PB_RenderReadPixels;
    renderer->RenderPresent = XBOX_PB_RenderPresent;
    renderer->DestroyTexture = XBOX_PB_DestroyTexture;
    renderer->DestroyRenderer = XBOX_PB_DestroyRenderer;
    renderer->info = XBOX_PB_RenderDriver.info;
    renderer->info.flags = (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    renderer->driverdata = data;
    renderer->window = window;

    data->buf_width = pb_back_buffer_width();
    data->buf_height = pb_back_buffer_height();

    if (flags & SDL_RENDERER_PRESENTVSYNC) {
        data->vsync = SDL_TRUE;
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    }

    pb_show_front_screen();

    Uint32 *p = pb_begin();

    /* set fixed pipeline mode */
    p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
        MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_FIXED) |
        MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));

    /* set unused matrices to identity */
    p = pb_push_transposed_matrix(p, NV097_SET_MODEL_VIEW_MATRIX, mat_identity);
    p = pb_push_transposed_matrix(p, NV097_SET_INVERSE_MODEL_VIEW_MATRIX, mat_identity);
    p = pb_push_transposed_matrix(p, NV097_SET_COMPOSITE_MATRIX, mat_identity);

    /* turn off stuff we don't need */
    p = pb_push4(p, NV097_SET_VIEWPORT_OFFSET, 0, 0, 0, 0);
    p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, 0);
    p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, 0);
    p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_ALPHA_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, 0);
    p = pb_push1(p, NV097_SET_DEPTH_MASK, 0);

    /* default to BLENDMODE_NONE */
    p = pb_push1(p, NV097_SET_BLEND_ENABLE, 0);

    pb_end(p);

    /* set default viewport */
    const SDL_Rect vrect = { 0, 0, data->buf_width, data->buf_height };
    SetViewport(data, vrect);

    /* reset all texture units */
    p = pb_begin();
    for (unsigned int i = 0; i < 4; i++) {
        p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(i), 0x0003FFC0); /* disable */
        p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_MATRIX_ENABLE(i), 0);
        p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_WRAP(i), 0x030303); /* clamp */
    }
    pb_end(p);

    /* set all attribute types to float */
    p = pb_begin();
    pb_push(p++, NV097_SET_VERTEX_DATA_ARRAY_FORMAT, 16);
    for(unsigned int i = 0; i < 16; i++)
        *(p++) = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F;
    pb_end(p);

    /* set base pixel combiner */
    SetCombinerColor();

    while (pb_busy());

    return renderer;
}

SDL_RenderDriver XBOX_PB_RenderDriver = {
    .CreateRenderer = XBOX_PB_CreateRenderer,
    .info = {
        .name = "xbox_pbkit",
        .flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        .num_texture_formats = 7,
        .texture_formats = {
            SDL_PIXELFORMAT_ARGB8888,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_PIXELFORMAT_BGRA8888,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_PIXELFORMAT_ARGB4444,
            SDL_PIXELFORMAT_ARGB1555,
            SDL_PIXELFORMAT_RGB565,
        },
        .max_texture_width = 2048,
        .max_texture_height = 2048,
     }
};

#endif /* SDL_VIDEO_RENDER_XBOX_PBKIT */

/* vi: set ts=4 sw=4 expandtab: */

