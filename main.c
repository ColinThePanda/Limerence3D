#include <stdint.h>
#include <stdlib.h>
#define RGFW_IMPLEMENTATION
#define RGFW_DEBUG
#include "third_party/RGFW.h"
#define OLIVEC_IMPLEMENTATION
#include "third_party/olive.c"
#include <math.h>

#ifdef ERROR
#undef ERROR
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#include "assets.h"

#define HANDMADE_MATH_USE_DEGREES
#include "third_party/HandmadeMath.h"

#include "assets/generated/assets_model_utah_teapot.asset.c"
#include "assets/generated/assets_font_iosevka_regular.asset.c"

#define SENSITIVITY 0.1f
#define MOVEMENT_SPEED 3.0f
#define FOREGROUND_COLOR 0xFFED9564
#define BACKGROUND_COLOR 0xFF181818
#define NEAR_PLANE 0.1f
#define FAR_PLANE 5.0f

static inline HMM_Vec3 vec3_from_array(const float arr[3]) {
    return HMM_V3(arr[0], arr[1], arr[2]);
}

static void write_text(Olivec_Canvas canvas, const Assets_Font *font, const char *text, int x, int y, int scale, uint32_t color) {
    if (font == NULL || text == NULL || scale <= 0) return;

    const float line_height = (font->ascent - font->descent + font->line_gap) * scale;
    float pen_x = (float)x;
    float baseline_y = (float)y + font->ascent * scale;

    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            pen_x = (float)x;
            baseline_y += line_height;
            continue;
        }

        int glyph_index = (int)(*cursor) - font->first_codepoint;
        if (glyph_index < 0 || glyph_index >= font->glyph_count) continue;

        const Assets_Glyph *glyph = &font->glyphs[glyph_index];
        const int glyph_x = (int)(pen_x + glyph->xoff * scale);
        const int glyph_y = (int)(baseline_y + glyph->yoff * scale);
        const int glyph_width = glyph->x1 - glyph->x0;
        const int glyph_height = glyph->y1 - glyph->y0;

        for (int src_y = 0; src_y < glyph_height; ++src_y) {
            for (int src_x = 0; src_x < glyph_width; ++src_x) {
                int atlas_x = glyph->x0 + src_x;
                int atlas_y = glyph->y0 + src_y;
                unsigned char glyph_alpha = font->atlas_alpha[atlas_y * font->atlas_stride + atlas_x];
                if (glyph_alpha == 0) continue;

                uint32_t blended_color = (color & 0x00FFFFFF) |
                    ((((color >> 24) & 0xFF) * glyph_alpha / 255U) << 24);

                for (int dy = 0; dy < scale; ++dy) {
                    int dst_y = glyph_y + src_y * scale + dy;
                    if (dst_y < 0 || dst_y >= (int)canvas.height) continue;

                    for (int dx = 0; dx < scale; ++dx) {
                        int dst_x = glyph_x + src_x * scale + dx;
                        if (dst_x < 0 || dst_x >= (int)canvas.width) continue;
                        olivec_blend_color(&OLIVEC_PIXEL(canvas, dst_x, dst_y), blended_color);
                    }
                }
            }
        }

        pen_x += glyph->xadvance * scale;
    }
}

void draw_triangle(
    Olivec_Canvas canvas, float *zbuffer,
    HMM_Vec3 v1_view, HMM_Vec3 v2_view, HMM_Vec3 v3_view,
    HMM_Mat4 projection, HMM_Mat4 model,
    HMM_Vec3 vn1, HMM_Vec3 vn2, HMM_Vec3 vn3,
    int width, int height
) {
    (void)model;
    (void)vn1;
    (void)vn2;
    (void)vn3;
    // Backface culling
    HMM_Vec3 edge1 = HMM_SubV3(v2_view, v1_view);
    HMM_Vec3 edge2 = HMM_SubV3(v3_view, v1_view);
    HMM_Vec3 normal = HMM_Cross(edge1, edge2);
    HMM_Vec3 viewDir = HMM_V3(-v1_view.X, -v1_view.Y, -v1_view.Z);
    if (HMM_DotV3(normal, viewDir) <= 0.0f) return;

    // Project to clip space
    HMM_Vec4 v1_clip = HMM_MulM4V4(projection, HMM_V4V(v1_view, 1.0f));
    HMM_Vec4 v2_clip = HMM_MulM4V4(projection, HMM_V4V(v2_view, 1.0f));
    HMM_Vec4 v3_clip = HMM_MulM4V4(projection, HMM_V4V(v3_view, 1.0f));
    HMM_Vec3 v1_ndc = HMM_V3(v1_clip.X/v1_clip.W, v1_clip.Y/v1_clip.W, v1_clip.Z/v1_clip.W);
    HMM_Vec3 v2_ndc = HMM_V3(v2_clip.X/v2_clip.W, v2_clip.Y/v2_clip.W, v2_clip.Z/v2_clip.W);
    HMM_Vec3 v3_ndc = HMM_V3(v3_clip.X/v3_clip.W, v3_clip.Y/v3_clip.W, v3_clip.Z/v3_clip.W);

    // Screen coords
    HMM_Vec2 p1 = HMM_V2((v1_ndc.X + 1.0f) * width  * 0.5f, (1.0f - v1_ndc.Y) * height * 0.5f);
    HMM_Vec2 p2 = HMM_V2((v2_ndc.X + 1.0f) * width  * 0.5f, (1.0f - v2_ndc.Y) * height * 0.5f);
    HMM_Vec2 p3 = HMM_V2((v3_ndc.X + 1.0f) * width  * 0.5f, (1.0f - v3_ndc.Y) * height * 0.5f);

    // Rasterize
    int lx, hx, ly, hy;
    if (olivec_normalize_triangle(width, height, (int)p1.X, (int)p1.Y, (int)p2.X, (int)p2.Y, (int)p3.X, (int)p3.Y, &lx, &hx, &ly, &hy)) {
        for (int y = ly; y <= hy; ++y) {
            for (int x = lx; x <= hx; ++x) {
                int u1, u2, det;
                if (olivec_barycentric((int)p1.X, (int)p1.Y, (int)p2.X, (int)p2.Y, (int)p3.X, (int)p3.Y, x, y, &u1, &u2, &det)) {
                    int u3 = det - u1 - u2;
                    float z = v1_ndc.Z * u1/det + v2_ndc.Z * u2/det + v3_ndc.Z * u3/det;
                    
                    if (z >= -1.0f && z <= 1.0f && z < zbuffer[y*canvas.width + x]) {
                        zbuffer[y*canvas.width + x] = z;
                        float fog = (z + 1.0f) * 0.5f; // 0 = near, 1 = far
                        fog = fog * fog;
                        float brightness = 1.0f - fog * 0.8f;
                        uint8_t r = ((FOREGROUND_COLOR >> 16) & 0xFF) * brightness;
                        uint8_t g = ((FOREGROUND_COLOR >>  8) & 0xFF) * brightness;
                        uint8_t b = ((FOREGROUND_COLOR      ) & 0xFF) * brightness;
                        OLIVEC_PIXEL(canvas, x, y) = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
}

void draw_model(Olivec_Canvas canvas, float *zbuffer, HMM_Mat4 model, HMM_Mat4 view, HMM_Mat4 projection, HMM_Vec3 camera) {
    (void)camera;
    HMM_Mat4 mv  = HMM_MulM4(view, model);
    const Assets_Model *asset = &assets_model_utah_teapot;

    for (size_t i = 0; i < asset->face_count; ++i) {
        const int *face = asset->faces[i];
        HMM_Vec3 v1_view = HMM_MulM4V4(mv, HMM_V4V(vec3_from_array(asset->vertices[face[ASSETS_FACE_V1]]), 1.0f)).XYZ;
        HMM_Vec3 v2_view = HMM_MulM4V4(mv, HMM_V4V(vec3_from_array(asset->vertices[face[ASSETS_FACE_V2]]), 1.0f)).XYZ;
        HMM_Vec3 v3_view = HMM_MulM4V4(mv, HMM_V4V(vec3_from_array(asset->vertices[face[ASSETS_FACE_V3]]), 1.0f)).XYZ;

        HMM_Vec3 vn1 = face[ASSETS_FACE_VN1] >= 0 ? HMM_MulM4V4(model, HMM_V4V(vec3_from_array(asset->normals[face[ASSETS_FACE_VN1]]), 0.0f)).XYZ : HMM_V3(0, 0, 0);
        HMM_Vec3 vn2 = face[ASSETS_FACE_VN2] >= 0 ? HMM_MulM4V4(model, HMM_V4V(vec3_from_array(asset->normals[face[ASSETS_FACE_VN2]]), 0.0f)).XYZ : HMM_V3(0, 0, 0);
        HMM_Vec3 vn3 = face[ASSETS_FACE_VN3] >= 0 ? HMM_MulM4V4(model, HMM_V4V(vec3_from_array(asset->normals[face[ASSETS_FACE_VN3]]), 0.0f)).XYZ : HMM_V3(0, 0, 0);

        HMM_Vec3 clipped[3], unclipped[3];
        int cc = 0, uc = 0;
        if (v1_view.Z > -NEAR_PLANE) clipped[cc++] = v1_view; else unclipped[uc++] = v1_view;
        if (v2_view.Z > -NEAR_PLANE) clipped[cc++] = v2_view; else unclipped[uc++] = v2_view;
        if (v3_view.Z > -NEAR_PLANE) clipped[cc++] = v3_view; else unclipped[uc++] = v3_view;

        switch (cc) {
            case 0:
                draw_triangle(canvas, zbuffer, v1_view, v2_view, v3_view, projection, model, vn1, vn2, vn3, canvas.width, canvas.height);
                break;
            case 1: {
                // 1 vertex clipped, 2 visible -> interpolate 2 new verts, make 2 triangles
                HMM_Vec3 a = unclipped[0], b = unclipped[1], c = clipped[0];
                float tA = (-NEAR_PLANE - a.Z) / (c.Z - a.Z);
                float tB = (-NEAR_PLANE - b.Z) / (c.Z - b.Z);
                HMM_Vec3 newA = HMM_LerpV3(a, tA, c);
                HMM_Vec3 newB = HMM_LerpV3(b, tB, c);
                draw_triangle(canvas, zbuffer, a,    b, newA, projection, model, vn1, vn2, vn3, canvas.width, canvas.height);
                draw_triangle(canvas, zbuffer, newA, b, newB, projection, model, vn1, vn2, vn3, canvas.width, canvas.height);
                break;
            }
            case 2: {
                // 2 vertices clipped, 1 visible -> interpolate 2 new verts, make 1 triangle
                HMM_Vec3 a = unclipped[0], b = clipped[0], c = clipped[1];
                float tB = (-NEAR_PLANE - a.Z) / (b.Z - a.Z);
                float tC = (-NEAR_PLANE - a.Z) / (c.Z - a.Z);
                HMM_Vec3 newB = HMM_LerpV3(a, tB, b);
                HMM_Vec3 newC = HMM_LerpV3(a, tC, c);
                draw_triangle(canvas, zbuffer, a, newB, newC, projection, model, vn1, vn2, vn3, canvas.width, canvas.height);
                break;
            }
            case 3: continue; // fully clipped do not draw
        }
    }
}

typedef enum { MOUSE_NORMAL, MOUSE_DISABLED } MouseMode;
MouseMode mouseMode = MOUSE_NORMAL;

void set_mouse_mode(RGFW_window* win, MouseMode mode) {
    mouseMode = mode;
    if (mode == MOUSE_DISABLED) {
        RGFW_window_showMouse(win, 0);
        RGFW_window_holdMouse(win);
    } else {
        RGFW_window_showMouse(win, 1);
        RGFW_window_unholdMouse(win);
    }
}

int main(void) {
    RGFW_window *win = RGFW_createWindow("Spinning Triangle", 100, 100, 800, 600, RGFW_windowCenter | RGFW_windowNoResize);
    set_mouse_mode(win, MOUSE_DISABLED);

    u8 *pixels = (u8 *)RGFW_alloc(win->w * win->h * 4);
    float *zbuffer = (float *)malloc(win->w * win->h * sizeof(float));
    RGFW_surface *surface = RGFW_window_createSurface(win, pixels, win->w, win->h, RGFW_formatRGBA8);
    RGFW_window_setExitKey(win, RGFW_escape);
    Olivec_Canvas canvas = olivec_canvas((uint32_t *)pixels, win->w, win->h, win->w);

    float angle = 0.0f;
    uint64_t last_frame = nanos_since_unspecified_epoch();

    HMM_Vec3 camera_pos = HMM_V3(0, 0, 2);
    float camera_pitch = 0.0f;
    float camera_yaw = 180.0f;

    HMM_Mat4 projection = HMM_Perspective_RH_NO(70.0f, (float)win->w / (float)win->h, 0.1f, 100.0f);
    HMM_Mat4 view = HMM_LookAt_RH(camera_pos, HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));

    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;
        float mouse_dx = 0, mouse_dy = 0;
        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit) break;
            if (event.type == RGFW_focusIn && mouseMode == MOUSE_DISABLED)
                RGFW_window_holdMouse(win);
            if (event.type == RGFW_mousePosChanged && mouseMode == MOUSE_DISABLED) {
                camera_yaw   -= event.mouse.vecX * SENSITIVITY;
                camera_pitch += event.mouse.vecY * SENSITIVITY;
            }
        }

        if (RGFW_isKeyPressed(RGFW_escape))
            set_mouse_mode(win, MOUSE_NORMAL);
        if (RGFW_isMousePressed(RGFW_mouseLeft) && mouseMode == MOUSE_NORMAL)
            set_mouse_mode(win, MOUSE_DISABLED);
        
        uint64_t current_time = nanos_since_unspecified_epoch();
        float dt = (float)(current_time - last_frame) / NANOS_PER_SEC;
        last_frame = current_time;

        olivec_fill(canvas, BACKGROUND_COLOR);
        for (size_t i = 0; i < (size_t)(win->w * win->h); ++i) zbuffer[i] = 1.0f;

        camera_yaw   -= mouse_dx * SENSITIVITY;
        camera_pitch += mouse_dy * SENSITIVITY;

        if (camera_pitch > 90.0f) camera_pitch = 90.0f;
        if (camera_pitch < -90.0f) camera_pitch = -90.0f;

        HMM_Vec3 forward = {
            HMM_CosF(camera_pitch) * HMM_SinF(camera_yaw),
           -HMM_SinF(camera_pitch),
            HMM_CosF(camera_pitch) * HMM_CosF(camera_yaw)
        };

        HMM_Vec3 flat_forward = HMM_NormV3(HMM_V3(forward.X, 0, forward.Z));
        HMM_Vec3 right = HMM_Cross(flat_forward, HMM_V3(0, 1, 0));
        
        if (RGFW_isKeyDown(RGFW_w))
            camera_pos = HMM_AddV3(camera_pos, HMM_MulV3F(flat_forward, MOVEMENT_SPEED * dt));
        if (RGFW_isKeyDown(RGFW_s))
            camera_pos = HMM_SubV3(camera_pos, HMM_MulV3F(flat_forward, MOVEMENT_SPEED * dt));
        if (RGFW_isKeyDown(RGFW_a))
            camera_pos = HMM_SubV3(camera_pos, HMM_MulV3F(right, MOVEMENT_SPEED * dt));
        if (RGFW_isKeyDown(RGFW_d))
            camera_pos = HMM_AddV3(camera_pos, HMM_MulV3F(right, MOVEMENT_SPEED * dt));
        if (RGFW_isKeyDown(RGFW_space))
            camera_pos = HMM_AddV3(camera_pos, HMM_MulV3F(HMM_V3(0.0f, 1.0f, 0.0f), MOVEMENT_SPEED * dt));
         if (RGFW_isKeyDown(RGFW_shiftL) || RGFW_isKeyDown(RGFW_shiftR))
            camera_pos = HMM_AddV3(camera_pos, HMM_MulV3F(HMM_V3(0.0f, 1.0f, 0.0f), -MOVEMENT_SPEED * dt));

        HMM_Vec3 center = HMM_AddV3(camera_pos, forward);
        view = HMM_LookAt_RH(camera_pos, center, HMM_V3(0, 1, 0));

        HMM_Mat4 model = HMM_Rotate_RH(angle, HMM_V3(0, 1, 0));
        draw_model(canvas, zbuffer, model, view, projection, HMM_V3(0, 0, 1));

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "fps: %d", (int)(1.0f / dt));
        write_text(canvas, &assets_font_iosevka_regular, buffer, 8, 8, 1, 0xFFFFFFFF);

        angle += 90.0f * dt;
        if (angle >= 360.0f) angle -= 360.0f;

        RGFW_window_blitSurface(win, surface);
    }

    RGFW_surface_free(surface);
    RGFW_free(pixels);
    free(zbuffer);
    RGFW_window_close(win);

    return 0;
}
