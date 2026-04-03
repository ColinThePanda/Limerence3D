#define OLIVEC_IMPLEMENTATION
#include "core.h"

#include <math.h>

typedef struct {
    HMM_Vec3 view_pos;
    HMM_Vec3 rgb;
} Core_Shaded_Vertex;

typedef struct {
    HMM_Vec3 min;
    HMM_Vec3 max;
} Core_Aabb;

static float core_clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static HMM_Vec3 core_clamp_rgb(HMM_Vec3 rgb)
{
    return HMM_V3(core_clamp01(rgb.X), core_clamp01(rgb.Y), core_clamp01(rgb.Z));
}

static HMM_Vec3 core_color_to_rgb(uint32_t color)
{
    return HMM_V3(
        (float)((color >> 16) & 0xFF) / 255.0f,
        (float)((color >> 8) & 0xFF) / 255.0f,
        (float)(color & 0xFF) / 255.0f
    );
}

static uint32_t core_rgb_to_color(HMM_Vec3 rgb)
{
    HMM_Vec3 clamped = core_clamp_rgb(rgb);
    uint32_t r = (uint32_t)(clamped.X * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(clamped.Y * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(clamped.Z * 255.0f + 0.5f);

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static HMM_Vec3 core_lerp_rgb(HMM_Vec3 a, HMM_Vec3 b, float t)
{
    return HMM_LerpV3(a, t, b);
}

static inline HMM_Vec3 core_vec3_from_array(const float arr[3])
{
    return HMM_V3(arr[0], arr[1], arr[2]);
}

static Core_Aabb core_asset_bounds(const Assets_Model *asset)
{
    Core_Aabb bounds = {0};

    if (asset == NULL || asset->vertex_count == 0) {
        return bounds;
    }

    bounds.min = core_vec3_from_array(asset->vertices[0]);
    bounds.max = bounds.min;

    for (size_t i = 1; i < asset->vertex_count; ++i) {
        HMM_Vec3 vertex = core_vec3_from_array(asset->vertices[i]);

        if (vertex.X < bounds.min.X) bounds.min.X = vertex.X;
        if (vertex.Y < bounds.min.Y) bounds.min.Y = vertex.Y;
        if (vertex.Z < bounds.min.Z) bounds.min.Z = vertex.Z;
        if (vertex.X > bounds.max.X) bounds.max.X = vertex.X;
        if (vertex.Y > bounds.max.Y) bounds.max.Y = vertex.Y;
        if (vertex.Z > bounds.max.Z) bounds.max.Z = vertex.Z;
    }

    return bounds;
}

static HMM_Vec3 core_aabb_corner(Core_Aabb bounds, int index)
{
    return HMM_V3(
        (index & 1) ? bounds.max.X : bounds.min.X,
        (index & 2) ? bounds.max.Y : bounds.min.Y,
        (index & 4) ? bounds.max.Z : bounds.min.Z
    );
}

static HMM_Mat3 core_mat3_from_mat4(HMM_Mat4 matrix)
{
    HMM_Mat3 result = {0};

    result.Columns[0] = matrix.Columns[0].XYZ;
    result.Columns[1] = matrix.Columns[1].XYZ;
    result.Columns[2] = matrix.Columns[2].XYZ;

    return result;
}

static HMM_Mat3 core_normal_matrix_from_model_view(HMM_Mat4 model_view)
{
    HMM_Mat3 linear = core_mat3_from_mat4(model_view);
    float determinant = HMM_DeterminantM3(linear);

    if (fabsf(determinant) < 0.000001f) {
        return HMM_M3D(1.0f);
    }

    return HMM_TransposeM3(HMM_InvGeneralM3(linear));
}

static HMM_Vec3 core_safe_norm_v3(HMM_Vec3 vector)
{
    float length = HMM_LenV3(vector);

    if (length <= 0.000001f) {
        return HMM_V3(0.0f, 0.0f, 0.0f);
    }

    return HMM_MulV3F(vector, 1.0f / length);
}

static HMM_Vec3 core_face_normal(HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c)
{
    HMM_Vec3 edge1 = HMM_SubV3(b, a);
    HMM_Vec3 edge2 = HMM_SubV3(c, a);
    return core_safe_norm_v3(HMM_Cross(edge1, edge2));
}

static HMM_Vec3 core_triangle_centroid(HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c)
{
    return HMM_MulV3F(HMM_AddV3(HMM_AddV3(a, b), c), 1.0f / 3.0f);
}

static HMM_Vec3 core_eval_lighting(
    HMM_Vec3 base_rgb,
    HMM_Vec3 normal_view,
    HMM_Vec3 view_pos,
    HMM_Vec3 light_dir_view,
    const Core_Mesh_Draw_Options *options
)
{
    HMM_Vec3 normal = core_safe_norm_v3(normal_view);
    HMM_Vec3 point_to_light = core_safe_norm_v3(HMM_MulV3F(light_dir_view, -1.0f));
    float diffuse = 0.0f;
    float specular = 0.0f;

    if (HMM_LenV3(normal) > 0.0f && HMM_LenV3(point_to_light) > 0.0f) {
        diffuse = HMM_DotV3(normal, point_to_light);
        if (diffuse < 0.0f) diffuse = 0.0f;
        diffuse *= options->diffuse_strength;

        if (diffuse > 0.0f && options->specular_strength > 0.0f) {
            HMM_Vec3 view_dir = core_safe_norm_v3(HMM_MulV3F(view_pos, -1.0f));
            HMM_Vec3 half_vector = core_safe_norm_v3(HMM_AddV3(view_dir, point_to_light));
            float highlight = HMM_DotV3(normal, half_vector);

            if (highlight > 0.0f) {
                specular = powf(highlight, options->shininess) * options->specular_strength;
            }
        }
    }

    return core_clamp_rgb(HMM_AddV3(
        HMM_MulV3F(base_rgb, options->ambient_strength + diffuse),
        HMM_V3(specular, specular, specular)
    ));
}

static bool core_point_within_near_plane(Core_Shaded_Vertex vertex, float near_plane)
{
    return vertex.view_pos.Z <= -near_plane;
}

static Core_Shaded_Vertex core_intersect_near_plane(Core_Shaded_Vertex a, Core_Shaded_Vertex b, float near_plane)
{
    float t = (-near_plane - a.view_pos.Z) / (b.view_pos.Z - a.view_pos.Z);
    Core_Shaded_Vertex result = {0};

    result.view_pos = HMM_LerpV3(a.view_pos, t, b.view_pos);
    result.rgb = core_lerp_rgb(a.rgb, b.rgb, t);

    return result;
}

static bool core_same_point(HMM_Vec3 a, HMM_Vec3 b)
{
    const float epsilon = 0.00001f;
    return fabsf(a.X - b.X) < epsilon &&
           fabsf(a.Y - b.Y) < epsilon &&
           fabsf(a.Z - b.Z) < epsilon;
}

static void core_append_unique_vertex(Core_Shaded_Vertex *vertices, size_t *count, Core_Shaded_Vertex vertex)
{
    if (*count > 0 && core_same_point(vertices[*count - 1].view_pos, vertex.view_pos)) {
        return;
    }

    vertices[(*count)++] = vertex;
}

static size_t core_clip_triangle_against_near_plane(const Core_Shaded_Vertex input[3], float near_plane, Core_Shaded_Vertex output[4])
{
    size_t output_count = 0;
    Core_Shaded_Vertex previous = input[2];
    bool previous_inside = core_point_within_near_plane(previous, near_plane);

    for (size_t i = 0; i < 3; ++i) {
        Core_Shaded_Vertex current = input[i];
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

    if (output_count > 1 && core_same_point(output[0].view_pos, output[output_count - 1].view_pos)) {
        output_count -= 1;
    }

    return output_count;
}

static HMM_Vec3 core_apply_fog(HMM_Vec3 color, float view_depth, const Core_Mesh_Draw_Options *options)
{
    float fog;
    HMM_Vec3 fog_color = core_color_to_rgb(options->fog_color);

    if (view_depth <= options->fog_start) {
        return color;
    }

    if (options->fog_end <= options->fog_start) {
        return fog_color;
    }

    fog = (view_depth - options->fog_start) / (options->fog_end - options->fog_start);
    if (fog < 0.0f) fog = 0.0f;
    if (fog > 1.0f) fog = 1.0f;

    if (options->fog_power != 1.0f) {
        fog = powf(fog, options->fog_power);
    }

    return core_lerp_rgb(color, fog_color, fog);
}

static bool core_model_is_occluded(
    const Assets_Model *asset,
    Olivec_Canvas canvas,
    const float *zbuffer,
    HMM_Mat4 model_view,
    HMM_Mat4 projection,
    const Core_Mesh_Draw_Options *options
)
{
    Core_Aabb bounds;
    float min_ndc_x = 1.0f;
    float min_ndc_y = 1.0f;
    float max_ndc_x = -1.0f;
    float max_ndc_y = -1.0f;
    float nearest_ndc_z = 1.0f;
    bool any_inside_near = false;
    bool any_outside_near = false;
    bool all_left = true;
    bool all_right = true;
    bool all_above = true;
    bool all_below = true;
    bool all_far = true;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int step;

    if (!options->occlusion_culling || asset == NULL || zbuffer == NULL || asset->vertex_count == 0) {
        return false;
    }

    bounds = core_asset_bounds(asset);

    for (int i = 0; i < 8; ++i) {
        HMM_Vec3 local_corner = core_aabb_corner(bounds, i);
        HMM_Vec3 view_corner = HMM_MulM4V4(model_view, HMM_V4V(local_corner, 1.0f)).XYZ;
        HMM_Vec4 clip_corner;
        HMM_Vec3 ndc_corner;

        if (view_corner.Z <= -options->near_plane) {
            any_inside_near = true;
        } else {
            any_outside_near = true;
        }

        clip_corner = HMM_MulM4V4(projection, HMM_V4V(view_corner, 1.0f));
        if (fabsf(clip_corner.W) < 0.000001f) {
            return false;
        }

        ndc_corner = HMM_V3(
            clip_corner.X / clip_corner.W,
            clip_corner.Y / clip_corner.W,
            clip_corner.Z / clip_corner.W
        );

        if (ndc_corner.X < min_ndc_x) min_ndc_x = ndc_corner.X;
        if (ndc_corner.Y < min_ndc_y) min_ndc_y = ndc_corner.Y;
        if (ndc_corner.X > max_ndc_x) max_ndc_x = ndc_corner.X;
        if (ndc_corner.Y > max_ndc_y) max_ndc_y = ndc_corner.Y;
        if (ndc_corner.Z < nearest_ndc_z) nearest_ndc_z = ndc_corner.Z;

        all_left = all_left && ndc_corner.X < -1.0f;
        all_right = all_right && ndc_corner.X > 1.0f;
        all_above = all_above && ndc_corner.Y > 1.0f;
        all_below = all_below && ndc_corner.Y < -1.0f;
        all_far = all_far && ndc_corner.Z > 1.0f;
    }

    if (!any_inside_near) {
        return true;
    }

    if (all_left || all_right || all_above || all_below || all_far) {
        return true;
    }

    if (any_outside_near) {
        return false;
    }

    min_x = (int)(((min_ndc_x + 1.0f) * 0.5f) * canvas.width);
    max_x = (int)(((max_ndc_x + 1.0f) * 0.5f) * canvas.width);
    min_y = (int)(((1.0f - max_ndc_y) * 0.5f) * canvas.height);
    max_y = (int)(((1.0f - min_ndc_y) * 0.5f) * canvas.height);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)canvas.width) max_x = (int)canvas.width - 1;
    if (max_y >= (int)canvas.height) max_y = (int)canvas.height - 1;

    if (min_x > max_x || min_y > max_y) {
        return true;
    }

    step = options->occlusion_test_step;
    if (step <= 0) step = 1;

    for (int y = min_y; y <= max_y; y += step) {
        for (int x = min_x; x <= max_x; x += step) {
            if (zbuffer[y * canvas.width + x] >= nearest_ndc_z - 0.0005f) {
                return false;
            }
        }

        if (zbuffer[y * canvas.width + max_x] >= nearest_ndc_z - 0.0005f) {
            return false;
        }
    }

    for (int x = min_x; x <= max_x; x += step) {
        if (zbuffer[max_y * canvas.width + x] >= nearest_ndc_z - 0.0005f) {
            return false;
        }
    }

    if (zbuffer[max_y * canvas.width + max_x] >= nearest_ndc_z - 0.0005f) {
        return false;
    }

    return true;
}

static void core_draw_triangle(
    Olivec_Canvas canvas,
    float *zbuffer,
    Core_Shaded_Vertex v1,
    Core_Shaded_Vertex v2,
    Core_Shaded_Vertex v3,
    HMM_Mat4 projection,
    const Core_Mesh_Draw_Options *options
)
{
    HMM_Vec3 edge1 = HMM_SubV3(v2.view_pos, v1.view_pos);
    HMM_Vec3 edge2 = HMM_SubV3(v3.view_pos, v1.view_pos);
    HMM_Vec3 normal = HMM_Cross(edge1, edge2);
    HMM_Vec3 view_dir = HMM_V3(-v1.view_pos.X, -v1.view_pos.Y, -v1.view_pos.Z);

    if (options->backface_culling && HMM_DotV3(normal, view_dir) <= 0.0f) {
        return;
    }

    HMM_Vec4 v1_clip = HMM_MulM4V4(projection, HMM_V4V(v1.view_pos, 1.0f));
    HMM_Vec4 v2_clip = HMM_MulM4V4(projection, HMM_V4V(v2.view_pos, 1.0f));
    HMM_Vec4 v3_clip = HMM_MulM4V4(projection, HMM_V4V(v3.view_pos, 1.0f));
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
            float w1 = (float)u1 / (float)det;
            float w2 = (float)u2 / (float)det;
            float w3 = (float)u3 / (float)det;
            float z = v1_ndc.Z * w1 + v2_ndc.Z * w2 + v3_ndc.Z * w3;
            float view_depth = -(v1.view_pos.Z * w1 + v2.view_pos.Z * w2 + v3.view_pos.Z * w3);
            HMM_Vec3 rgb = HMM_AddV3(
                HMM_AddV3(HMM_MulV3F(v1.rgb, w1), HMM_MulV3F(v2.rgb, w2)),
                HMM_MulV3F(v3.rgb, w3)
            );

            if (z >= -1.0f && z <= 1.0f && z < zbuffer[y * canvas.width + x]) {
                zbuffer[y * canvas.width + x] = z;
                OLIVEC_PIXEL(canvas, x, y) = core_rgb_to_color(core_apply_fog(rgb, view_depth, options));
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
    HMM_Mat3 normal_matrix = core_normal_matrix_from_model_view(model_view);
    HMM_Vec3 base_rgb = core_color_to_rgb(color);
    HMM_Mat3 view_rotation;
    HMM_Vec3 light_dir_view;

    if (asset == NULL || zbuffer == NULL) return;
    if (options != NULL) resolved_options = *options;
    view_rotation = core_mat3_from_mat4(view);
    light_dir_view = core_safe_norm_v3(HMM_MulM3V3(view_rotation, resolved_options.light_direction_world));

    if (core_model_is_occluded(asset, canvas, zbuffer, model_view, projection, &resolved_options)) {
        return;
    }

    for (size_t i = 0; i < asset->face_count; ++i) {
        const int *face = asset->faces[i];
        Core_Shaded_Vertex triangle[3] = {
            {.view_pos = HMM_MulM4V4(model_view, HMM_V4V(core_vec3_from_array(asset->vertices[face[ASSETS_FACE_V1]]), 1.0f)).XYZ},
            {.view_pos = HMM_MulM4V4(model_view, HMM_V4V(core_vec3_from_array(asset->vertices[face[ASSETS_FACE_V2]]), 1.0f)).XYZ},
            {.view_pos = HMM_MulM4V4(model_view, HMM_V4V(core_vec3_from_array(asset->vertices[face[ASSETS_FACE_V3]]), 1.0f)).XYZ},
        };
        Core_Shaded_Vertex clipped[4];
        HMM_Vec3 face_normal = core_face_normal(triangle[0].view_pos, triangle[1].view_pos, triangle[2].view_pos);
        bool has_vertex_normals =
            asset->normal_count > 0 &&
            face[ASSETS_FACE_VN1] >= 0 &&
            face[ASSETS_FACE_VN2] >= 0 &&
            face[ASSETS_FACE_VN3] >= 0;

        if (!resolved_options.lighting_enabled || resolved_options.lighting_mode == CORE_LIGHTING_NONE) {
            triangle[0].rgb = base_rgb;
            triangle[1].rgb = base_rgb;
            triangle[2].rgb = base_rgb;
        } else if (resolved_options.lighting_mode == CORE_LIGHTING_FLAT) {
            HMM_Vec3 centroid = core_triangle_centroid(triangle[0].view_pos, triangle[1].view_pos, triangle[2].view_pos);
            HMM_Vec3 lit_rgb = core_eval_lighting(base_rgb, face_normal, centroid, light_dir_view, &resolved_options);

            triangle[0].rgb = lit_rgb;
            triangle[1].rgb = lit_rgb;
            triangle[2].rgb = lit_rgb;
        } else {
            HMM_Vec3 normals[3] = {face_normal, face_normal, face_normal};

            if (has_vertex_normals) {
                normals[0] = HMM_MulM3V3(normal_matrix, core_vec3_from_array(asset->normals[face[ASSETS_FACE_VN1]]));
                normals[1] = HMM_MulM3V3(normal_matrix, core_vec3_from_array(asset->normals[face[ASSETS_FACE_VN2]]));
                normals[2] = HMM_MulM3V3(normal_matrix, core_vec3_from_array(asset->normals[face[ASSETS_FACE_VN3]]));
            }

            triangle[0].rgb = core_eval_lighting(base_rgb, normals[0], triangle[0].view_pos, light_dir_view, &resolved_options);
            triangle[1].rgb = core_eval_lighting(base_rgb, normals[1], triangle[1].view_pos, light_dir_view, &resolved_options);
            triangle[2].rgb = core_eval_lighting(base_rgb, normals[2], triangle[2].view_pos, light_dir_view, &resolved_options);
        }

        size_t clipped_count = core_clip_triangle_against_near_plane(triangle, resolved_options.near_plane, clipped);

        for (size_t j = 1; j + 1 < clipped_count; ++j) {
            core_draw_triangle(canvas, zbuffer, clipped[0], clipped[j], clipped[j + 1], projection, &resolved_options);
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
