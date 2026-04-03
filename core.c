#define OLIVEC_IMPLEMENTATION
#include "core.h"

#include <math.h>

static uint32_t core_lerp_color(uint32_t a, uint32_t b, float t)
{
    uint8_t a_a = (uint8_t)((a >> 24) & 0xFF);
    uint8_t a_r = (uint8_t)((a >> 16) & 0xFF);
    uint8_t a_g = (uint8_t)((a >> 8) & 0xFF);
    uint8_t a_b = (uint8_t)(a & 0xFF);
    uint8_t b_a = (uint8_t)((b >> 24) & 0xFF);
    uint8_t b_r = (uint8_t)((b >> 16) & 0xFF);
    uint8_t b_g = (uint8_t)((b >> 8) & 0xFF);
    uint8_t b_b = (uint8_t)(b & 0xFF);
    uint8_t out_a;
    uint8_t out_r;
    uint8_t out_g;
    uint8_t out_b;

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    out_a = (uint8_t)(a_a + (b_a - a_a) * t);
    out_r = (uint8_t)(a_r + (b_r - a_r) * t);
    out_g = (uint8_t)(a_g + (b_g - a_g) * t);
    out_b = (uint8_t)(a_b + (b_b - a_b) * t);

    return ((uint32_t)out_a << 24) | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | (uint32_t)out_b;
}

static inline HMM_Vec3 core_vec3_from_array(const float arr[3])
{
    return HMM_V3(arr[0], arr[1], arr[2]);
}

static bool core_point_within_near_plane(HMM_Vec3 point, float near_plane)
{
    return point.Z <= -near_plane;
}

static HMM_Vec3 core_intersect_near_plane(HMM_Vec3 a, HMM_Vec3 b, float near_plane)
{
    float t = (-near_plane - a.Z) / (b.Z - a.Z);
    return HMM_LerpV3(a, t, b);
}

static bool core_same_point(HMM_Vec3 a, HMM_Vec3 b)
{
    const float epsilon = 0.00001f;
    return fabsf(a.X - b.X) < epsilon &&
           fabsf(a.Y - b.Y) < epsilon &&
           fabsf(a.Z - b.Z) < epsilon;
}

static void core_append_unique_vertex(HMM_Vec3 *vertices, size_t *count, HMM_Vec3 vertex)
{
    if (*count > 0 && core_same_point(vertices[*count - 1], vertex)) {
        return;
    }

    vertices[(*count)++] = vertex;
}

static size_t core_clip_triangle_against_near_plane(const HMM_Vec3 input[3], float near_plane, HMM_Vec3 output[4])
{
    size_t output_count = 0;
    HMM_Vec3 previous = input[2];
    bool previous_inside = core_point_within_near_plane(previous, near_plane);

    for (size_t i = 0; i < 3; ++i) {
        HMM_Vec3 current = input[i];
        bool current_inside = core_point_within_near_plane(current, near_plane);

        if (previous_inside && current_inside) {
            core_append_unique_vertex(output, &output_count, current);
        } else if (previous_inside && !current_inside) {
            core_append_unique_vertex(output, &output_count, core_intersect_near_plane(previous, current, near_plane));
        } else if (!previous_inside && current_inside) {
            core_append_unique_vertex(output, &output_count, core_intersect_near_plane(previous, current, near_plane));
            core_append_unique_vertex(output, &output_count, current);
        }

        previous = current;
        previous_inside = current_inside;
    }

    if (output_count > 1 && core_same_point(output[0], output[output_count - 1])) {
        output_count -= 1;
    }

    return output_count;
}

static uint32_t core_apply_fog(uint32_t color, float view_depth, const Core_Mesh_Draw_Options *options)
{
    float fog;

    if (view_depth <= options->fog_start) {
        return color;
    }

    if (options->fog_end <= options->fog_start) {
        return options->fog_color;
    }

    fog = (view_depth - options->fog_start) / (options->fog_end - options->fog_start);
    if (fog < 0.0f) fog = 0.0f;
    if (fog > 1.0f) fog = 1.0f;

    if (options->fog_power != 1.0f) {
        fog = powf(fog, options->fog_power);
    }

    return core_lerp_color(color, options->fog_color, fog);
}

static void core_draw_triangle(
    Olivec_Canvas canvas,
    float *zbuffer,
    HMM_Vec3 v1_view,
    HMM_Vec3 v2_view,
    HMM_Vec3 v3_view,
    HMM_Mat4 projection,
    uint32_t color,
    const Core_Mesh_Draw_Options *options
)
{
    HMM_Vec3 edge1 = HMM_SubV3(v2_view, v1_view);
    HMM_Vec3 edge2 = HMM_SubV3(v3_view, v1_view);
    HMM_Vec3 normal = HMM_Cross(edge1, edge2);
    HMM_Vec3 view_dir = HMM_V3(-v1_view.X, -v1_view.Y, -v1_view.Z);

    if (options->backface_culling && HMM_DotV3(normal, view_dir) <= 0.0f) {
        return;
    }

    HMM_Vec4 v1_clip = HMM_MulM4V4(projection, HMM_V4V(v1_view, 1.0f));
    HMM_Vec4 v2_clip = HMM_MulM4V4(projection, HMM_V4V(v2_view, 1.0f));
    HMM_Vec4 v3_clip = HMM_MulM4V4(projection, HMM_V4V(v3_view, 1.0f));
    HMM_Vec3 v1_ndc = HMM_V3(v1_clip.X / v1_clip.W, v1_clip.Y / v1_clip.W, v1_clip.Z / v1_clip.W);
    HMM_Vec3 v2_ndc = HMM_V3(v2_clip.X / v2_clip.W, v2_clip.Y / v2_clip.W, v2_clip.Z / v2_clip.W);
    HMM_Vec3 v3_ndc = HMM_V3(v3_clip.X / v3_clip.W, v3_clip.Y / v3_clip.W, v3_clip.Z / v3_clip.W);

    HMM_Vec2 p1 = HMM_V2((v1_ndc.X + 1.0f) * canvas.width * 0.5f, (1.0f - v1_ndc.Y) * canvas.height * 0.5f);
    HMM_Vec2 p2 = HMM_V2((v2_ndc.X + 1.0f) * canvas.width * 0.5f, (1.0f - v2_ndc.Y) * canvas.height * 0.5f);
    HMM_Vec2 p3 = HMM_V2((v3_ndc.X + 1.0f) * canvas.width * 0.5f, (1.0f - v3_ndc.Y) * canvas.height * 0.5f);

    int lx;
    int hx;
    int ly;
    int hy;

    if (!olivec_normalize_triangle((int)canvas.width, (int)canvas.height,
                                   (int)p1.X, (int)p1.Y,
                                   (int)p2.X, (int)p2.Y,
                                   (int)p3.X, (int)p3.Y,
                                   &lx, &hx, &ly, &hy)) {
        return;
    }

    for (int y = ly; y <= hy; ++y) {
        for (int x = lx; x <= hx; ++x) {
            int u1;
            int u2;
            int det;

            if (!olivec_barycentric((int)p1.X, (int)p1.Y,
                                    (int)p2.X, (int)p2.Y,
                                    (int)p3.X, (int)p3.Y,
                                    x, y, &u1, &u2, &det)) {
                continue;
            }

            int u3 = det - u1 - u2;
            float z = v1_ndc.Z * u1 / det + v2_ndc.Z * u2 / det + v3_ndc.Z * u3 / det;
            float view_depth = -(v1_view.Z * u1 / det + v2_view.Z * u2 / det + v3_view.Z * u3 / det);

            if (z >= -1.0f && z <= 1.0f && z < zbuffer[y * canvas.width + x]) {
                zbuffer[y * canvas.width + x] = z;
                OLIVEC_PIXEL(canvas, x, y) = core_apply_fog(color, view_depth, options);
            }
        }
    }
}

Olivec_Canvas core_make_canvas(uint32_t *pixels, size_t width, size_t height, size_t stride)
{
    return olivec_canvas(pixels, width, height, stride);
}

void core_begin_frame(Olivec_Canvas canvas, float *zbuffer, uint32_t clear_color, float clear_depth)
{
    size_t pixel_count = canvas.width * canvas.height;

    olivec_fill(canvas, clear_color);
    for (size_t i = 0; i < pixel_count; ++i) {
        zbuffer[i] = clear_depth;
    }
}

void core_draw_text(Olivec_Canvas canvas, const Assets_Font *font, const char *text, int x, int y, int scale, uint32_t color)
{
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

void core_draw_model(
    const Assets_Model *asset,
    Olivec_Canvas canvas,
    float *zbuffer,
    HMM_Mat4 model,
    HMM_Mat4 view,
    HMM_Mat4 projection,
    uint32_t color,
    const Core_Mesh_Draw_Options *options
)
{
    Core_Mesh_Draw_Options resolved_options = CORE_MESH_DRAW_OPTIONS_DEFAULT;
    HMM_Mat4 model_view = HMM_MulM4(view, model);

    if (asset == NULL || zbuffer == NULL) return;
    if (options != NULL) resolved_options = *options;

    for (size_t i = 0; i < asset->face_count; ++i) {
        const int *face = asset->faces[i];
        HMM_Vec3 triangle[3] = {
            HMM_MulM4V4(model_view, HMM_V4V(core_vec3_from_array(asset->vertices[face[ASSETS_FACE_V1]]), 1.0f)).XYZ,
            HMM_MulM4V4(model_view, HMM_V4V(core_vec3_from_array(asset->vertices[face[ASSETS_FACE_V2]]), 1.0f)).XYZ,
            HMM_MulM4V4(model_view, HMM_V4V(core_vec3_from_array(asset->vertices[face[ASSETS_FACE_V3]]), 1.0f)).XYZ,
        };
        HMM_Vec3 clipped[4];
        size_t clipped_count = core_clip_triangle_against_near_plane(triangle, resolved_options.near_plane, clipped);

        for (size_t j = 1; j + 1 < clipped_count; ++j) {
            core_draw_triangle(canvas, zbuffer, clipped[0], clipped[j], clipped[j + 1], projection, color, &resolved_options);
        }
    }
}

void core_fly_camera_look(Core_Fly_Camera *camera, float yaw_delta, float pitch_delta, float pitch_min, float pitch_max)
{
    if (camera == NULL) return;

    camera->yaw += yaw_delta;
    camera->pitch += pitch_delta;

    if (camera->pitch > pitch_max) camera->pitch = pitch_max;
    if (camera->pitch < pitch_min) camera->pitch = pitch_min;
}

HMM_Vec3 core_fly_camera_forward(Core_Fly_Camera camera)
{
    return HMM_V3(
        HMM_CosF(camera.pitch) * HMM_SinF(camera.yaw),
        -HMM_SinF(camera.pitch),
        HMM_CosF(camera.pitch) * HMM_CosF(camera.yaw)
    );
}

HMM_Vec3 core_fly_camera_flat_forward(Core_Fly_Camera camera)
{
    HMM_Vec3 forward = core_fly_camera_forward(camera);
    return HMM_NormV3(HMM_V3(forward.X, 0.0f, forward.Z));
}

HMM_Vec3 core_fly_camera_right(Core_Fly_Camera camera)
{
    return HMM_Cross(core_fly_camera_flat_forward(camera), HMM_V3(0.0f, 1.0f, 0.0f));
}

void core_fly_camera_move(Core_Fly_Camera *camera, float forward_distance, float strafe_distance, float vertical_distance)
{
    if (camera == NULL) return;

    camera->position = HMM_AddV3(camera->position, HMM_MulV3F(core_fly_camera_flat_forward(*camera), forward_distance));
    camera->position = HMM_AddV3(camera->position, HMM_MulV3F(core_fly_camera_right(*camera), strafe_distance));
    camera->position = HMM_AddV3(camera->position, HMM_V3(0.0f, vertical_distance, 0.0f));
}

HMM_Mat4 core_fly_camera_view(Core_Fly_Camera camera)
{
    HMM_Vec3 forward = core_fly_camera_forward(camera);
    return HMM_LookAt_RH(camera.position, HMM_AddV3(camera.position, forward), HMM_V3(0.0f, 1.0f, 0.0f));
}
