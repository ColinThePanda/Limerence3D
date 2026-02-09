#include <stdint.h>
#include <stdlib.h>
#define RGFW_IMPLEMENTATION
#define RGFW_DEBUG
#include "RGFW.h"
#define OLIVEC_IMPLEMENTATION
#include "olive.c"
#include <math.h>

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define HANDMADE_MATH_USE_DEGREES
#include "HandmadeMath.h"

#include "assets/utahTeapot.c"

typedef enum {
    FACE_V1, FACE_V2, FACE_V3,
    FACE_VT1, FACE_VT2, FACE_VT3,
    FACE_VN1, FACE_VN2, FACE_VN3,
} Face_Index;

static inline HMM_Vec3 vec3_from_array(float arr[3]) {
    return HMM_V3(arr[0], arr[1], arr[2]);
}

void draw_model(Olivec_Canvas canvas, float *zbuffer, HMM_Mat4 model, HMM_Mat4 vp, HMM_Vec3 camera) {
    HMM_Mat4 mvp = HMM_MulM4(vp, model);
    
    for (size_t i = 0; i < faces_count; ++i) {
        HMM_Vec4 v1_clip = HMM_MulM4V4(mvp, HMM_V4V(vec3_from_array(vertices[faces[i][FACE_V1]]), 1.0f));
        HMM_Vec4 v2_clip = HMM_MulM4V4(mvp, HMM_V4V(vec3_from_array(vertices[faces[i][FACE_V2]]), 1.0f));
        HMM_Vec4 v3_clip = HMM_MulM4V4(mvp, HMM_V4V(vec3_from_array(vertices[faces[i][FACE_V3]]), 1.0f));
        
        HMM_Vec3 v1_ndc = HMM_V3(v1_clip.X / v1_clip.W, v1_clip.Y / v1_clip.W, v1_clip.Z / v1_clip.W);
        HMM_Vec3 v2_ndc = HMM_V3(v2_clip.X / v2_clip.W, v2_clip.Y / v2_clip.W, v2_clip.Z / v2_clip.W);
        HMM_Vec3 v3_ndc = HMM_V3(v3_clip.X / v3_clip.W, v3_clip.Y / v3_clip.W, v3_clip.Z / v3_clip.W);
        
        HMM_Vec2 p1 = HMM_V2((v1_ndc.X + 1.0f) * canvas.width * 0.5f, (1.0f - v1_ndc.Y) * canvas.height * 0.5f);
        HMM_Vec2 p2 = HMM_V2((v2_ndc.X + 1.0f) * canvas.width * 0.5f, (1.0f - v2_ndc.Y) * canvas.height * 0.5f);
        HMM_Vec2 p3 = HMM_V2((v3_ndc.X + 1.0f) * canvas.width * 0.5f, (1.0f - v3_ndc.Y) * canvas.height * 0.5f);
        
        HMM_Vec3 triAB = HMM_SubV3(v1_ndc, v2_ndc);
        HMM_Vec3 triAC = HMM_SubV3(v1_ndc, v3_ndc);
        HMM_Vec3 triNormal = HMM_Cross(triAB, triAC);
        
        if (triNormal.Z < 0.0f) continue;
        
        HMM_Vec3 vn1 = HMM_MulM4V4(model, HMM_V4V(vec3_from_array(normals[faces[i][FACE_VN1]]), 0.0f)).XYZ;
        HMM_Vec3 vn2 = HMM_MulM4V4(model, HMM_V4V(vec3_from_array(normals[faces[i][FACE_VN2]]), 0.0f)).XYZ;
        HMM_Vec3 vn3 = HMM_MulM4V4(model, HMM_V4V(vec3_from_array(normals[faces[i][FACE_VN3]]), 0.0f)).XYZ;
        
        int lx, hx, ly, hy;
        if (olivec_normalize_triangle(canvas.width, canvas.height, (int)p1.X, (int)p1.Y, (int)p2.X, (int)p2.Y, (int)p3.X, (int)p3.Y, &lx, &hx, &ly, &hy)) {
            for (int y = ly; y <= hy; ++y) {
                for (int x = lx; x <= hx; ++x) {
                    int u1, u2, det;
                    if (olivec_barycentric((int)p1.X, (int)p1.Y, (int)p2.X, (int)p2.Y, (int)p3.X, (int)p3.Y, x, y, &u1, &u2, &det)) {
                        int u3 = det - u1 - u2;
                        float z = v1_ndc.Z * u1 / det + v2_ndc.Z * u2 / det + v3_ndc.Z * u3 / det;
                        
                        if (z >= zbuffer[y * canvas.width + x]) continue;
                        
                        zbuffer[y * canvas.width + x] = z;
                        olivec_blend_color(&OLIVEC_PIXEL(canvas, x, y), mix_colors3(0xFF1818FF, 0xFF18FF18, 0xFFFF1818, u1, u2, det));
                    }
                }
            }
        }
    }
}

int main(void) {
    RGFW_window *win = RGFW_createWindow("Spinning Triangle", 100, 100, 800, 600, RGFW_windowCenter | RGFW_windowNoResize);
    u8 *pixels = (u8 *)RGFW_alloc(win->w * win->h * 4);
    float *zbuffer = (float *)malloc(win->w * win->h * sizeof(float));
    RGFW_surface *surface = RGFW_window_createSurface(win, pixels, win->w, win->h, RGFW_formatRGBA8);
    RGFW_window_setExitKey(win, RGFW_escape);
    Olivec_Canvas canvas = olivec_canvas((uint32_t *)pixels, win->w, win->h, win->w);

    float angle = 0.0f;
    uint64_t last_frame = nanos_since_unspecified_epoch();

    HMM_Mat4 projection = HMM_Perspective_RH_NO(70.0f, (float)win->w / (float)win->h, 0.1f, 100.0f);
    HMM_Mat4 view = HMM_LookAt_RH(HMM_V3(0, 0, 2), HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));
    HMM_Mat4 vp = HMM_MulM4(projection, view);

    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;
        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit) break;
        }

        uint64_t current_time = nanos_since_unspecified_epoch();
        float dt = (float)(current_time - last_frame) / NANOS_PER_SEC;
        last_frame = current_time;

        olivec_fill(canvas, 0xFF181818);
        for (size_t i = 0; i < win->w * win->h; ++i) zbuffer[i] = 1.0f;

        HMM_Mat4 model = HMM_Rotate_RH(angle, HMM_V3(0, 1, 0));
        draw_model(canvas, zbuffer, model, vp, HMM_V3(0, 0, 1));

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
