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

#include "assets/utahTeapot.c"

#define M_PI 3.14159265358979323846
#define DEG2RAD(x) ((x) * M_PI / 180.0f)

typedef struct {
    float x, y;
} Vector2;

static Vector2 make_vector2(float x, float y) {
    Vector2 v2;
    v2.x = x;
    v2.y = y;
    return v2;
}

typedef struct {
    float x, y, z;
} Vector3;

static Vector3 make_vector3(float x, float y, float z) {
    Vector3 v3;
    v3.x = x;
    v3.y = y;
    v3.z = z;
    return v3;
}

#define EPSILON 1e-6

static Vector2 project_3d_2d(Vector3 v3) {
    if (v3.z < 0)
        v3.z = -v3.z;
    if (v3.z < EPSILON)
        v3.z += EPSILON;
    return make_vector2(v3.x / v3.z, v3.y / v3.z);
}

static Vector2 project_2d_scr(Vector2 v2, i32 width, i32 height) {
    return make_vector2((v2.x + 1) / 2 * width, (1 - (v2.y + 1) / 2) * height);
}

static Vector3 rotate_y(Vector3 p, float delta_angle) {
    float angle = atan2f(p.z, p.x) + delta_angle;
    float mag = sqrtf(p.x * p.x + p.z * p.z);
    return make_vector3(cosf(angle) * mag, p.y, sinf(angle) * mag);
}

typedef enum {
    FACE_V1,
    FACE_V2,
    FACE_V3,
    FACE_VT1,
    FACE_VT2,
    FACE_VT3,
    FACE_VN1,
    FACE_VN2,
    FACE_VN3,
} Face_Index;

float vector3_dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

int main(void) {
    RGFW_window *win =
        RGFW_createWindow("Spinning Triangle", 100, 100, 800, 600,
                          RGFW_windowCenter | RGFW_windowNoResize);

    u8 *pixels = (u8 *)RGFW_alloc(win->w * win->h * 4);
    float *zbuffer = (float *)malloc(win->w * win->h * sizeof(float));

    RGFW_surface *surface = RGFW_window_createSurface(win, pixels, win->w,
                                                      win->h, RGFW_formatRGBA8);
    RGFW_window_setExitKey(win, RGFW_escape);

    Olivec_Canvas canvas =
        olivec_canvas((uint32_t *)pixels, win->w, win->h, win->w);

    float angle = 0.0f;
    float rotation_speed = DEG2RAD(90.0f);
    uint64_t last_frame = nanos_since_unspecified_epoch();

    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;
        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit)
                break;
        }

        uint64_t current_time = nanos_since_unspecified_epoch();

        float dt = (float)(current_time - last_frame) / NANOS_PER_SEC;
        last_frame = current_time;

        olivec_fill(canvas, 0xFF181818);

        for (size_t i = 0; i < win->w * win->h; ++i)
            zbuffer[i] = 0;

        Vector3 camera = {0, 0, 1};
        for (size_t i = 0; i < faces_count; ++i) {
            int a, b, c;

            a = faces[i][FACE_V1];
            b = faces[i][FACE_V2];
            c = faces[i][FACE_V3];
            Vector3 v1 = rotate_y(
                make_vector3(vertices[a][0], vertices[a][1], vertices[a][2]),
                angle);
            Vector3 v2 = rotate_y(
                make_vector3(vertices[b][0], vertices[b][1], vertices[b][2]),
                angle);
            Vector3 v3 = rotate_y(
                make_vector3(vertices[c][0], vertices[c][1], vertices[c][2]),
                angle);
            v1.z += 1.5;
            v2.z += 1.5;
            v3.z += 1.5;

            a = faces[i][FACE_VN1];
            b = faces[i][FACE_VN2];
            c = faces[i][FACE_VN3];
            Vector3 vn1 = rotate_y(
                make_vector3(normals[a][0], normals[a][1], normals[a][2]),
                angle);
            Vector3 vn2 = rotate_y(
                make_vector3(normals[b][0], normals[b][1], normals[b][2]),
                angle);
            Vector3 vn3 = rotate_y(
                make_vector3(normals[c][0], normals[c][1], normals[c][2]),
                angle);
            if (vector3_dot(camera, vn1) > 0.0 &&
                vector3_dot(camera, vn2) > 0.0 &&
                vector3_dot(camera, vn3) > 0.0)
                continue;

            Vector2 p1 = project_2d_scr(project_3d_2d(v1), win->w, win->h);
            Vector2 p2 = project_2d_scr(project_3d_2d(v2), win->w, win->h);
            Vector2 p3 = project_2d_scr(project_3d_2d(v3), win->w, win->h);

            int x1 = p1.x;
            int x2 = p2.x;
            int x3 = p3.x;
            int y1 = p1.y;
            int y2 = p2.y;
            int y3 = p3.y;
            int lx, hx, ly, hy;
            if (olivec_normalize_triangle(canvas.width, canvas.height, x1, y1,
                                          x2, y2, x3, y3, &lx, &hx, &ly, &hy)) {
                for (int y = ly; y <= hy; ++y) {
                    for (int x = lx; x <= hx; ++x) {
                        int u1, u2, det;
                        if (olivec_barycentric(x1, y1, x2, y2, x3, y3, x, y,
                                               &u1, &u2, &det)) {
                            int u3 = det - u1 - u2;
                            float z = 1 / v1.z * u1 / det +
                                      1 / v2.z * u2 / det + 1 / v3.z * u3 / det;
                            float near_plane = 0.1f;
                            float far_plane = 5.0f;
                            if (1.0f / far_plane < z && z < 1.0f / near_plane &&
                                z > zbuffer[y * win->w + x]) {
                                zbuffer[y * win->w + x] = z;
                                OLIVEC_PIXEL(canvas, x, y) =
                                    mix_colors3(0xFF1818FF, 0xFF18FF18,
                                                0xFFFF1818, u1, u2, det);

                                z = 1.0f / z;
                                if (z >= 1.0) {
                                    z -= 1.0;
                                    uint32_t v = z * 255;
                                    if (v > 255)
                                        v = 255;
                                    olivec_blend_color(
                                        &OLIVEC_PIXEL(canvas, x, y),
                                        (v << (3 * 8)));
                                }
                            }
                        }
                    }
                }
            }
        }
        angle += rotation_speed * dt;

        if (angle >= 360.0f)
            angle -= 360.0f;

        RGFW_window_blitSurface(win, surface);
    }

    RGFW_surface_free(surface);
    RGFW_free(pixels);
    RGFW_window_close(win);

    return 0;
}
