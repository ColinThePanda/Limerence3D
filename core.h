#ifndef CORE_H_
#define CORE_H_

#include <stddef.h>
#include <stdint.h>

#include "assets.h"

#ifndef OLIVECDEF
#define OLIVECDEF extern
#define CORE_RESTORE_OLIVECDEF
#endif
#include "third_party/olive.c"
#ifdef CORE_RESTORE_OLIVECDEF
#undef OLIVECDEF
#undef CORE_RESTORE_OLIVECDEF
#endif

#ifndef HANDMADE_MATH_USE_DEGREES
#define HANDMADE_MATH_USE_DEGREES
#endif
#include "third_party/HandmadeMath.h"

typedef struct {
    HMM_Vec3 position;
    float pitch;
    float yaw;
} Core_Fly_Camera;

typedef struct {
    float near_plane;
    float fog_start;
    float fog_end;
    float fog_power;
    uint32_t fog_color;
    int backface_culling;
} Core_Mesh_Draw_Options;

#define CORE_MESH_DRAW_OPTIONS_DEFAULT ((Core_Mesh_Draw_Options) { \
    .near_plane = 0.1f, \
    .fog_start = 6.0f, \
    .fog_end = 16.0f, \
    .fog_power = 1.0f, \
    .fog_color = 0xFF000000, \
    .backface_culling = 1, \
})

Olivec_Canvas core_make_canvas(uint32_t *pixels, size_t width, size_t height, size_t stride);
void core_begin_frame(Olivec_Canvas canvas, float *zbuffer, uint32_t clear_color, float clear_depth);
void core_draw_text(Olivec_Canvas canvas, const Assets_Font *font, const char *text, int x, int y, int scale, uint32_t color);
void core_draw_model(
    const Assets_Model *asset,
    Olivec_Canvas canvas,
    float *zbuffer,
    HMM_Mat4 model,
    HMM_Mat4 view,
    HMM_Mat4 projection,
    uint32_t color,
    const Core_Mesh_Draw_Options *options
);

void core_fly_camera_look(Core_Fly_Camera *camera, float yaw_delta, float pitch_delta, float pitch_min, float pitch_max);
HMM_Vec3 core_fly_camera_forward(Core_Fly_Camera camera);
HMM_Vec3 core_fly_camera_flat_forward(Core_Fly_Camera camera);
HMM_Vec3 core_fly_camera_right(Core_Fly_Camera camera);
void core_fly_camera_move(Core_Fly_Camera *camera, float forward_distance, float strafe_distance, float vertical_distance);
HMM_Mat4 core_fly_camera_view(Core_Fly_Camera camera);

#endif
