#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RGFW_IMPLEMENTATION
#define RGFW_DEBUG
#include "third_party/RGFW.h"

#ifdef ERROR
#undef ERROR
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#include "core.h"

#include "assets/generated/assets_model_utah_teapot.asset.c"
#include "assets/generated/assets_model_cube.asset.c"
#include "assets/generated/assets_font_iosevka_regular.asset.c"

#define SENSITIVITY 0.1f
#define MOVEMENT_SPEED 3.0f
#define FOREGROUND_COLOR 0xFFED9564
#define BACKGROUND_COLOR 0xFF181818
#define NEAR_PLANE 0.1f
#define FAR_PLANE 30.0f

typedef enum {
    MOUSE_NORMAL,
    MOUSE_DISABLED,
} MouseMode;

static MouseMode mouse_mode = MOUSE_NORMAL;

static void set_mouse_mode(RGFW_window *win, MouseMode mode)
{
    mouse_mode = mode;
    if (mode == MOUSE_DISABLED) {
        RGFW_window_showMouse(win, 0);
        RGFW_window_holdMouse(win);
    } else {
        RGFW_window_showMouse(win, 1);
        RGFW_window_unholdMouse(win);
    }
}

static HMM_Mat4 make_model_matrix(HMM_Vec3 position, HMM_Vec3 scale, float rotation_degrees, HMM_Vec3 axis)
{
    HMM_Mat4 translation = HMM_Translate(position);
    HMM_Mat4 rotation = HMM_Rotate_RH(rotation_degrees, axis);
    HMM_Mat4 scaling = HMM_Scale(scale);
    return HMM_MulM4(translation, HMM_MulM4(rotation, scaling));
}

static void draw_test_scene(
    Olivec_Canvas canvas,
    float *zbuffer,
    HMM_Mat4 view,
    HMM_Mat4 projection,
    const Core_Mesh_Draw_Options *draw_options,
    float teapot_rotation
)
{
    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(0.0f, -1.55f, 0.0f), HMM_V3(8.0f, 0.30f, 8.0f), 0.0f, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        0xFF3C4650,
        draw_options
    );

    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(0.0f, 0.1f, -4.6f), HMM_V3(7.5f, 3.0f, 0.35f), 0.0f, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        0xFF25313A,
        draw_options
    );

    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(-3.1f, -0.15f, -1.6f), HMM_V3(0.8f, 2.8f, 0.8f), 0.0f, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        0xFFC85A44,
        draw_options
    );

    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(3.0f, -0.35f, -1.4f), HMM_V3(1.3f, 1.9f, 0.9f), 22.0f, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        0xFF5E8E3E,
        draw_options
    );

    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(-1.75f, -0.95f, 2.1f), HMM_V3(1.6f, 0.55f, 2.3f), 18.0f, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        0xFF4979A8,
        draw_options
    );

    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(2.1f, -0.55f, 1.8f), HMM_V3(2.7f, 0.35f, 0.8f), -28.0f, HMM_V3(1.0f, 0.0f, 0.0f)),
        view,
        projection,
        0xFF8A6BCE,
        draw_options
    );

    core_draw_model(
        &assets_model_cube,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(0.0f, -0.95f, 0.0f), HMM_V3(1.8f, 0.6f, 1.8f), 0.0f, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        0xFFD2B870,
        draw_options
    );

    core_draw_model(
        &assets_model_utah_teapot,
        canvas,
        zbuffer,
        make_model_matrix(HMM_V3(0.0f, 0.1f, 0.0f), HMM_V3(1.0f, 1.0f, 1.0f), teapot_rotation, HMM_V3(0.0f, 1.0f, 0.0f)),
        view,
        projection,
        FOREGROUND_COLOR,
        draw_options
    );
}

int main(void)
{
    RGFW_window *win = RGFW_createWindow("Test Scene", 100, 100, 800, 600, RGFW_windowCenter | RGFW_windowNoResize);
    u8 *pixels = (u8 *)RGFW_alloc(win->w * win->h * 4);
    float *zbuffer = (float *)malloc(win->w * win->h * sizeof(float));
    RGFW_surface *surface = RGFW_window_createSurface(win, pixels, win->w, win->h, RGFW_formatRGBA8);
    Olivec_Canvas canvas = core_make_canvas((uint32_t *)pixels, win->w, win->h, win->w);
    Core_Fly_Camera camera = {
        .position = HMM_V3(0.0f, 1.35f, 8.0f),
        .pitch = 8.0f,
        .yaw = 180.0f,
    };
    Core_Mesh_Draw_Options draw_options = CORE_MESH_DRAW_OPTIONS_DEFAULT;
    float angle = 0.0f;
    uint64_t last_frame = nanos_since_unspecified_epoch();
    HMM_Mat4 projection = HMM_Perspective_RH_NO(70.0f, (float)win->w / (float)win->h, NEAR_PLANE, FAR_PLANE);

    draw_options.near_plane = NEAR_PLANE;
    draw_options.fog_color = BACKGROUND_COLOR;
    draw_options.fog_power = 1.8f;

    set_mouse_mode(win, MOUSE_DISABLED);
    RGFW_window_setExitKey(win, RGFW_escape);

    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;

        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit) break;
            if (event.type == RGFW_focusIn && mouse_mode == MOUSE_DISABLED) {
                RGFW_window_holdMouse(win);
            }
            if (event.type == RGFW_mousePosChanged && mouse_mode == MOUSE_DISABLED) {
                core_fly_camera_look(&camera,
                                     -event.mouse.vecX * SENSITIVITY,
                                     event.mouse.vecY * SENSITIVITY,
                                     -89.9f,
                                     89.9f);
            }
        }

        if (RGFW_isKeyPressed(RGFW_escape)) {
            set_mouse_mode(win, MOUSE_NORMAL);
        }
        if (RGFW_isMousePressed(RGFW_mouseLeft) && mouse_mode == MOUSE_NORMAL) {
            set_mouse_mode(win, MOUSE_DISABLED);
        }

        uint64_t current_time = nanos_since_unspecified_epoch();
        float dt = (float)(current_time - last_frame) / NANOS_PER_SEC;
        float forward_distance = 0.0f;
        float strafe_distance = 0.0f;
        float vertical_distance = 0.0f;
        HMM_Mat4 view;
        char buffer[256];

        last_frame = current_time;

        core_begin_frame(canvas, zbuffer, BACKGROUND_COLOR, 1.0f);

        if (RGFW_isKeyDown(RGFW_w)) forward_distance += MOVEMENT_SPEED * dt;
        if (RGFW_isKeyDown(RGFW_s)) forward_distance -= MOVEMENT_SPEED * dt;
        if (RGFW_isKeyDown(RGFW_a)) strafe_distance -= MOVEMENT_SPEED * dt;
        if (RGFW_isKeyDown(RGFW_d)) strafe_distance += MOVEMENT_SPEED * dt;
        if (RGFW_isKeyDown(RGFW_space)) vertical_distance += MOVEMENT_SPEED * dt;
        if (RGFW_isKeyDown(RGFW_shiftL) || RGFW_isKeyDown(RGFW_shiftR)) vertical_distance -= MOVEMENT_SPEED * dt;

        core_fly_camera_move(&camera, forward_distance, strafe_distance, vertical_distance);

        view = core_fly_camera_view(camera);
        draw_test_scene(canvas, zbuffer, view, projection, &draw_options, angle);

        snprintf(buffer, sizeof(buffer), "fps: %d", (int)(1.0f / dt));
        core_draw_text(canvas, &assets_font_iosevka_regular, buffer, 8, 8, 1, 0xFFFFFFFF);

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
