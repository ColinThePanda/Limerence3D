#ifndef ASSETS_H_
#define ASSETS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASSETS_STATUS_OK = 0,
    ASSETS_STATUS_ERROR = 1,
} Assets_Status;

typedef struct {
    char message[512];
    char source_path[260];
    size_t line;
} Assets_Error;

typedef Assets_Status (*Assets_Write_Fn)(void *user_data, const void *data, size_t size, Assets_Error *error);

typedef struct {
    Assets_Write_Fn write;
    void *user_data;
} Assets_Writer;

typedef enum {
    ASSETS_FACE_V1,
    ASSETS_FACE_V2,
    ASSETS_FACE_V3,
    ASSETS_FACE_VT1,
    ASSETS_FACE_VT2,
    ASSETS_FACE_VT3,
    ASSETS_FACE_VN1,
    ASSETS_FACE_VN2,
    ASSETS_FACE_VN3,
} Assets_Model_Face_Index;

typedef struct {
    const char *name;
    size_t vertex_count;
    const float (*vertices)[3];
    size_t texcoord_count;
    const float (*texcoords)[2];
    size_t normal_count;
    const float (*normals)[3];
    size_t face_count;
    const int (*faces)[9];
} Assets_Model;

typedef struct {
    float scale;
    const int *deleted_components;
    size_t deleted_component_count;
} Assets_Model_Options;

typedef struct {
    const char *name;
    int width;
    int height;
    int channels;
    int stride;
    size_t pixels_size;
    const unsigned char *pixels;
} Assets_Image;

typedef struct {
    int force_rgba;
} Assets_Image_Options;

typedef struct {
    int codepoint;
    int x0;
    int y0;
    int x1;
    int y1;
    float xoff;
    float yoff;
    float xadvance;
} Assets_Glyph;

typedef struct {
    const char *name;
    float pixel_height;
    int atlas_width;
    int atlas_height;
    int atlas_stride;
    int first_codepoint;
    int glyph_count;
    float ascent;
    float descent;
    float line_gap;
    size_t atlas_size;
    const unsigned char *atlas_alpha;
    const Assets_Glyph *glyphs;
} Assets_Font;

typedef struct {
    float pixel_height;
    int first_codepoint;
    int codepoint_count;
    int atlas_width;
    int atlas_height;
} Assets_Font_Options;

Assets_Writer assets_writer_from_file(FILE *file);
Assets_Status assets_writer_write(Assets_Writer writer, const void *data, size_t size, Assets_Error *error);
Assets_Status assets_writer_printf(Assets_Writer writer, Assets_Error *error, const char *fmt, ...);

void assets_make_symbol_name(char *buffer, size_t buffer_size, const char *prefix, const char *input_path);

Assets_Status assets_load_model_obj(const char *input_path, const Assets_Model_Options *options, Assets_Model *model, Assets_Error *error);
void assets_free_model(Assets_Model *model);
Assets_Status assets_emit_model_c(Assets_Writer writer, const char *symbol_name, const Assets_Model *model, Assets_Error *error);
Assets_Status assets_compile_model_to_c(const char *input_path, const char *output_path, const char *symbol_name, const Assets_Model_Options *options, Assets_Error *error);

Assets_Status assets_load_image(const char *input_path, const Assets_Image_Options *options, Assets_Image *image, Assets_Error *error);
void assets_free_image(Assets_Image *image);
Assets_Status assets_emit_image_c(Assets_Writer writer, const char *symbol_name, const Assets_Image *image, Assets_Error *error);
Assets_Status assets_compile_image_to_c(const char *input_path, const char *output_path, const char *symbol_name, const Assets_Image_Options *options, Assets_Error *error);

Assets_Status assets_load_font(const char *input_path, const Assets_Font_Options *options, Assets_Font *font, Assets_Error *error);
void assets_free_font(Assets_Font *font);
Assets_Status assets_emit_font_c(Assets_Writer writer, const char *symbol_name, const Assets_Font *font, Assets_Error *error);
Assets_Status assets_compile_font_to_c(const char *input_path, const char *output_path, const char *symbol_name, const Assets_Font_Options *options, Assets_Error *error);

#ifdef __cplusplus
}
#endif

#endif // ASSETS_H_

#ifdef ASSETS_IMPLEMENTATION

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef NOB_H_
#include "third_party/nob.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

#define ASSETS_ARRAY_APPEND(array, array_count, array_capacity, value, error_ptr) \
    do {                                                                           \
        (void)(error_ptr);                                                         \
        struct {                                                                   \
            __typeof__(array) items;                                               \
            size_t count;                                                          \
            size_t capacity;                                                       \
        } assets_da_ = {(array), (array_count), (array_capacity)};                 \
        nob_da_append(&assets_da_, (value));                                       \
        (array) = assets_da_.items;                                                \
        (array_count) = assets_da_.count;                                          \
        (array_capacity) = assets_da_.capacity;                                    \
    } while (0)

typedef struct {
    const char *data;
    size_t count;
} Assets_String_View;

typedef struct {
    float x;
    float y;
} Assets_Vec2;

typedef struct {
    float x;
    float y;
    float z;
} Assets_Vec3;

typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} Assets_Index_List;

typedef struct {
    Assets_Vec3 position;
    Assets_Index_List faces;
    size_t component;
} Assets_Obj_Vertex;

typedef struct {
    int v[3];
    int vt[3];
    int vn[3];
} Assets_Obj_Face;

typedef struct {
    FILE *file;
} Assets_File_Writer_Context;

typedef struct {
    Nob_String_Builder *sb;
} Assets_SB_Writer_Context;

static void assets_clear_error(Assets_Error *error)
{
    if (error == NULL) return;
    error->message[0] = '\0';
    error->source_path[0] = '\0';
    error->line = 0;
}

static void assets_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t count = strlen(src);
    if (count >= dst_size) count = dst_size - 1;
    memcpy(dst, src, count);
    dst[count] = '\0';
}

static void assets_errorf(Assets_Error *error, const char *source_path, size_t line, const char *fmt, ...)
{
    if (error == NULL) return;

    assets_clear_error(error);
    assets_copy_string(error->source_path, sizeof(error->source_path), source_path);
    error->line = line;

    va_list args;
    va_start(args, fmt);
    vsnprintf(error->message, sizeof(error->message), fmt, args);
    va_end(args);
}

static Assets_Status assets_file_write_callback(void *user_data, const void *data, size_t size, Assets_Error *error)
{
    Assets_File_Writer_Context *context = (Assets_File_Writer_Context *)user_data;
    if (size == 0) return ASSETS_STATUS_OK;
    if (fwrite(data, 1, size, context->file) != size) {
        assets_errorf(error, NULL, 0, "Failed to write output");
        return ASSETS_STATUS_ERROR;
    }
    return ASSETS_STATUS_OK;
}

static Assets_Status assets_sb_write_callback(void *user_data, const void *data, size_t size, Assets_Error *error)
{
    Assets_SB_Writer_Context *context = (Assets_SB_Writer_Context *)user_data;
    (void)error;
    nob_da_append_many(context->sb, data, size);
    return ASSETS_STATUS_OK;
}

Assets_Writer assets_writer_from_file(FILE *file)
{
    Assets_File_Writer_Context *context = (Assets_File_Writer_Context *)malloc(sizeof(*context));
    Assets_Writer writer = {0};

    if (context == NULL) return writer;

    context->file = file;
    writer.write = assets_file_write_callback;
    writer.user_data = context;
    return writer;
}

static void assets_writer_release(Assets_Writer writer)
{
    free(writer.user_data);
}

static Assets_Writer assets_writer_from_sb(Nob_String_Builder *sb)
{
    Assets_SB_Writer_Context *context = (Assets_SB_Writer_Context *)malloc(sizeof(*context));
    Assets_Writer writer = {0};

    if (context == NULL) return writer;

    context->sb = sb;
    writer.write = assets_sb_write_callback;
    writer.user_data = context;
    return writer;
}

Assets_Status assets_writer_write(Assets_Writer writer, const void *data, size_t size, Assets_Error *error)
{
    if (writer.write == NULL) {
        assets_errorf(error, NULL, 0, "Writer callback is NULL");
        return ASSETS_STATUS_ERROR;
    }
    return writer.write(writer.user_data, data, size, error);
}

Assets_Status assets_writer_printf(Assets_Writer writer, Assets_Error *error, const char *fmt, ...)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    va_list args;
    va_list copy;
    int count;

    va_start(args, fmt);
    va_copy(copy, args);
    count = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (count < 0) {
        assets_errorf(error, NULL, 0, "Formatting failed");
        goto defer;
    }

    char stack_buffer[1024];
    char *buffer = stack_buffer;
    size_t buffer_size = (size_t)count + 1;
    if (buffer_size > sizeof(stack_buffer)) {
        buffer = (char *)malloc(buffer_size);
        if (buffer == NULL) {
            assets_errorf(error, NULL, 0, "Out of memory");
            goto defer;
        }
    }

    if (vsnprintf(buffer, buffer_size, fmt, args) < 0) {
        assets_errorf(error, NULL, 0, "Formatting failed");
        if (buffer != stack_buffer) free(buffer);
        goto defer;
    }

    status = assets_writer_write(writer, buffer, buffer_size - 1, error);
    if (buffer != stack_buffer) free(buffer);

defer:
    va_end(args);
    return status;
}

static Assets_Status assets_read_entire_file(const char *path, unsigned char **data, size_t *size, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    Nob_String_Builder sb = {0};

    *data = NULL;
    *size = 0;

    if (!nob_read_entire_file(path, &sb)) {
        assets_errorf(error, path, 0, "Could not read file");
        goto defer;
    }

    nob_da_append(&sb, 0);
    *data = (unsigned char *)sb.items;
    *size = sb.count - 1;
    status = ASSETS_STATUS_OK;

defer:
    if (status != ASSETS_STATUS_OK) {
        free(sb.items);
    }
    return status;
}

static Assets_String_View assets_sv_from_parts(const char *data, size_t count)
{
    Assets_String_View view;
    view.data = data;
    view.count = count;
    return view;
}

static Assets_String_View assets_sv_trim_left(Assets_String_View view)
{
    while (view.count > 0 && isspace((unsigned char)view.data[0])) {
        view.data += 1;
        view.count -= 1;
    }
    return view;
}

static void assets_sv_chop_left(Assets_String_View *view, size_t count)
{
    if (count > view->count) count = view->count;
    view->data += count;
    view->count -= count;
}

static Assets_String_View assets_sv_chop_by_delim(Assets_String_View *view, char delim)
{
    size_t index = 0;
    while (index < view->count && view->data[index] != delim) index += 1;
    Assets_String_View head = assets_sv_from_parts(view->data, index);
    if (index < view->count) index += 1;
    assets_sv_chop_left(view, index);
    return head;
}

static bool assets_sv_eq(Assets_String_View view, const char *text)
{
    size_t text_count = strlen(text);
    return view.count == text_count && memcmp(view.data, text, text_count) == 0;
}

static int assets_parse_non_negative_index(const char *token, char **endptr)
{
    long value = strtol(token, endptr, 10);
    if (value <= 0) return -1;
    return (int)value - 1;
}

static Assets_Status assets_parse_face_vertex(const char *input_path, size_t line_number, Assets_String_View *line, int *v, int *vt, int *vn, Assets_Error *error)
{
    char *endptr;

    *line = assets_sv_trim_left(*line);
    if (line->count == 0) {
        assets_errorf(error, input_path, line_number, "Malformed face entry");
        return ASSETS_STATUS_ERROR;
    }

    *v = assets_parse_non_negative_index(line->data, &endptr);
    if (endptr == line->data || *v < 0) {
        assets_errorf(error, input_path, line_number, "Face vertex index must be positive");
        return ASSETS_STATUS_ERROR;
    }
    assets_sv_chop_left(line, (size_t)(endptr - line->data));

    *vt = -1;
    if (line->count > 0 && line->data[0] == '/') {
        assets_sv_chop_left(line, 1);
        if (line->count > 0 && line->data[0] != '/' && !isspace((unsigned char)line->data[0])) {
            *vt = assets_parse_non_negative_index(line->data, &endptr);
            if (endptr == line->data || *vt < 0) {
                assets_errorf(error, input_path, line_number, "Texture coordinate index must be positive");
                return ASSETS_STATUS_ERROR;
            }
            assets_sv_chop_left(line, (size_t)(endptr - line->data));
        }
    }

    *vn = -1;
    if (line->count > 0 && line->data[0] == '/') {
        assets_sv_chop_left(line, 1);
        if (line->count > 0 && !isspace((unsigned char)line->data[0])) {
            *vn = assets_parse_non_negative_index(line->data, &endptr);
            if (endptr == line->data || *vn < 0) {
                assets_errorf(error, input_path, line_number, "Normal index must be positive");
                return ASSETS_STATUS_ERROR;
            }
            assets_sv_chop_left(line, (size_t)(endptr - line->data));
        }
    }

    while (line->count > 0 && !isspace((unsigned char)line->data[0])) {
        assets_sv_chop_left(line, 1);
    }

    return ASSETS_STATUS_OK;
}

static bool assets_face_is_deleted(const Assets_Obj_Vertex *vertices, const Assets_Obj_Face *face, const int *deleted_components, size_t deleted_component_count)
{
    for (int i = 0; i < 3; ++i) {
        size_t component = vertices[face->v[i]].component;
        for (size_t j = 0; j < deleted_component_count; ++j) {
            if ((int)component == deleted_components[j]) {
                return true;
            }
        }
    }
    return false;
}

static Assets_Status assets_validate_model_faces(const char *input_path, const Assets_Obj_Face *faces, size_t face_count, size_t texcoord_count, size_t normal_count, Assets_Error *error)
{
    for (size_t i = 0; i < face_count; ++i) {
        for (int lane = 0; lane < 3; ++lane) {
            if (faces[i].vt[lane] >= 0 && (size_t)faces[i].vt[lane] >= texcoord_count) {
                assets_errorf(error, input_path, 0, "Face references missing texture coordinate");
                return ASSETS_STATUS_ERROR;
            }
            if (faces[i].vn[lane] >= 0 && (size_t)faces[i].vn[lane] >= normal_count) {
                assets_errorf(error, input_path, 0, "Face references missing normal");
                return ASSETS_STATUS_ERROR;
            }
        }
    }

    return ASSETS_STATUS_OK;
}

static void assets_free_obj_vertices(Assets_Obj_Vertex *vertices, size_t vertex_count)
{
    if (vertices == NULL) return;
    for (size_t i = 0; i < vertex_count; ++i) {
        free(vertices[i].faces.items);
    }
    free(vertices);
}

static Assets_Status assets_emit_bytes_array(Assets_Writer writer, Assets_Error *error, const char *type_name, const char *array_name, const unsigned char *data, size_t count)
{
    if (count == 0 || data == NULL) {
        return assets_writer_printf(writer, error, "static const %s %s[1] = {0};\n", type_name, array_name);
    }

    if (assets_writer_printf(writer, error, "static const %s %s[%zu] = {\n", type_name, array_name, count) != ASSETS_STATUS_OK) {
        return ASSETS_STATUS_ERROR;
    }

    for (size_t i = 0; i < count; ++i) {
        if (i % 12 == 0 && assets_writer_printf(writer, error, "    ") != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
        if (assets_writer_printf(writer, error, "0x%02X", data[i]) != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
        if (i + 1 < count && assets_writer_printf(writer, error, ", ") != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
        if ((i % 12 == 11 || i + 1 == count) && assets_writer_printf(writer, error, "\n") != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
    }

    return assets_writer_printf(writer, error, "};\n");
}

static Assets_Status assets_emit_float3_array(Assets_Writer writer, Assets_Error *error, const char *array_name, const float (*data)[3], size_t count)
{
    if (count == 0 || data == NULL) {
        return assets_writer_printf(writer, error, "static const float %s[1][3] = {{0}};\n", array_name);
    }

    if (assets_writer_printf(writer, error, "static const float %s[%zu][3] = {\n", array_name, count) != ASSETS_STATUS_OK) {
        return ASSETS_STATUS_ERROR;
    }
    for (size_t i = 0; i < count; ++i) {
        if (assets_writer_printf(writer, error, "    {%a, %a, %a},\n", data[i][0], data[i][1], data[i][2]) != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
    }
    return assets_writer_printf(writer, error, "};\n");
}

static Assets_Status assets_emit_float2_array(Assets_Writer writer, Assets_Error *error, const char *array_name, const float (*data)[2], size_t count)
{
    if (count == 0 || data == NULL) {
        return assets_writer_printf(writer, error, "static const float %s[1][2] = {{0}};\n", array_name);
    }

    if (assets_writer_printf(writer, error, "static const float %s[%zu][2] = {\n", array_name, count) != ASSETS_STATUS_OK) {
        return ASSETS_STATUS_ERROR;
    }
    for (size_t i = 0; i < count; ++i) {
        if (assets_writer_printf(writer, error, "    {%a, %a},\n", data[i][0], data[i][1]) != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
    }
    return assets_writer_printf(writer, error, "};\n");
}

static Assets_Status assets_emit_faces_array(Assets_Writer writer, Assets_Error *error, const char *array_name, const int (*data)[9], size_t count)
{
    if (count == 0 || data == NULL) {
        return assets_writer_printf(writer, error, "static const int %s[1][9] = {{0}};\n", array_name);
    }

    if (assets_writer_printf(writer, error, "static const int %s[%zu][9] = {\n", array_name, count) != ASSETS_STATUS_OK) {
        return ASSETS_STATUS_ERROR;
    }
    for (size_t i = 0; i < count; ++i) {
        if (assets_writer_printf(writer, error, "    {%d, %d, %d, %d, %d, %d, %d, %d, %d},\n",
                                 data[i][0], data[i][1], data[i][2],
                                 data[i][3], data[i][4], data[i][5],
                                 data[i][6], data[i][7], data[i][8]) != ASSETS_STATUS_OK) {
            return ASSETS_STATUS_ERROR;
        }
    }
    return assets_writer_printf(writer, error, "};\n");
}

static Assets_Status assets_emit_prelude(Assets_Writer writer, Assets_Error *error)
{
    return assets_writer_printf(writer, error, "#include \"assets.h\"\n\n");
}

static void assets_remap_model_vertex(float vertex[3], float scale, float min_x, float max_x, float min_y, float max_y, float min_z, float max_z)
{
    float center_x = min_x + (max_x - min_x) * 0.5f;
    float center_y = min_y + (max_y - min_y) * 0.5f;
    float center_z = min_z + (max_z - min_z) * 0.5f;

    vertex[2] = (vertex[2] - center_z) * scale;
    vertex[0] = (vertex[0] - center_x) * scale;
    vertex[1] = (vertex[1] - center_y) * scale;
}

void assets_make_symbol_name(char *buffer, size_t buffer_size, const char *prefix, const char *input_path)
{
    size_t out = 0;
    char previous = '\0';
    const char *path = input_path == NULL ? "" : input_path;
    const char *basename = path;
    const char *dot = NULL;

    if (buffer_size == 0) return;
    buffer[0] = '\0';

    for (const char *it = path; *it != '\0'; ++it) {
        if (*it == '/' || *it == '\\') basename = it + 1;
        if (*it == '.') dot = it;
    }

    const char *end = dot != NULL && dot > basename ? dot : basename + strlen(basename);

    if (prefix != NULL && prefix[0] != '\0') {
        for (const char *it = prefix; *it != '\0' && out + 1 < buffer_size; ++it) {
            char c = *it;
            if (!isalnum((unsigned char)c)) c = '_';
            c = (char)tolower((unsigned char)c);
            if (c == '_' && (out == 0 || previous == '_')) continue;
            buffer[out++] = c;
            previous = c;
        }
    }

    if (out > 0 && previous != '_' && out + 1 < buffer_size) {
        buffer[out++] = '_';
        previous = '_';
    }

    for (const char *it = basename; it < end && out + 1 < buffer_size; ++it) {
        char c = *it;
        bool is_upper = isupper((unsigned char)c) != 0;
        bool is_alpha_num = isalnum((unsigned char)c) != 0;

        if (!is_alpha_num) {
            if (out > 0 && previous != '_') {
                buffer[out++] = '_';
                previous = '_';
            }
            continue;
        }

        if (is_upper && out > 0 && previous != '_' && !isupper((unsigned char)previous)) {
            if (out + 1 >= buffer_size) break;
            buffer[out++] = '_';
            previous = '_';
        }

        c = (char)tolower((unsigned char)c);
        if (c == '_' && previous == '_') continue;
        buffer[out++] = c;
        previous = c;
    }

    if (out == 0) {
        assets_copy_string(buffer, buffer_size, "asset");
        return;
    }

    while (out > 0 && buffer[out - 1] == '_') out -= 1;
    if (out == 0) {
        assets_copy_string(buffer, buffer_size, "asset");
        return;
    }

    buffer[out] = '\0';
}

Assets_Status assets_load_model_obj(const char *input_path, const Assets_Model_Options *options, Assets_Model *model, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    unsigned char *file_data = NULL;
    size_t file_size = 0;
    Assets_String_View content = {0};
    Assets_Obj_Vertex *vertices = NULL;
    Assets_Vec2 *texcoords = NULL;
    Assets_Vec3 *normals = NULL;
    Assets_Obj_Face *faces = NULL;
    size_t vertex_count = 0, vertex_capacity = 0;
    size_t texcoord_count = 0, texcoord_capacity = 0;
    size_t normal_count = 0, normal_capacity = 0;
    size_t face_count = 0, face_capacity = 0;
    float min_x = 0.0f, max_x = 0.0f;
    float min_y = 0.0f, max_y = 0.0f;
    float min_z = 0.0f, max_z = 0.0f;
    bool have_bounds = false;
    bool saw_object = false;
    size_t object_line = 0;
    size_t component_count = 0;
    int *wave = NULL;
    size_t wave_count = 0, wave_capacity = 0;
    int *next_wave = NULL;
    size_t next_wave_count = 0, next_wave_capacity = 0;
    Assets_Model result = {0};
    float scale = options != NULL && options->scale != 0.0f ? options->scale : 1.0f;

    assets_clear_error(error);
    memset(model, 0, sizeof(*model));

    if (assets_read_entire_file(input_path, &file_data, &file_size, error) != ASSETS_STATUS_OK) {
        goto defer;
    }

    content = assets_sv_from_parts((const char *)file_data, file_size);
    for (size_t line_number = 1; content.count > 0; ++line_number) {
        Assets_String_View line = assets_sv_trim_left(assets_sv_chop_by_delim(&content, '\n'));
        if (line.count == 0 || line.data[0] == '#') continue;

        Assets_String_View kind = assets_sv_chop_by_delim(&line, ' ');
        line = assets_sv_trim_left(line);

        if (assets_sv_eq(kind, "v")) {
            Assets_Obj_Vertex vertex = {0};
            char *endptr;

            vertex.position.x = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed vertex");
                goto defer;
            }
            assets_sv_chop_left(&line, (size_t)(endptr - line.data));
            line = assets_sv_trim_left(line);

            vertex.position.y = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed vertex");
                goto defer;
            }
            assets_sv_chop_left(&line, (size_t)(endptr - line.data));
            line = assets_sv_trim_left(line);

            vertex.position.z = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed vertex");
                goto defer;
            }

            if (!have_bounds) {
                min_x = max_x = vertex.position.x;
                min_y = max_y = vertex.position.y;
                min_z = max_z = vertex.position.z;
                have_bounds = true;
            } else {
                if (vertex.position.x < min_x) min_x = vertex.position.x;
                if (vertex.position.x > max_x) max_x = vertex.position.x;
                if (vertex.position.y < min_y) min_y = vertex.position.y;
                if (vertex.position.y > max_y) max_y = vertex.position.y;
                if (vertex.position.z < min_z) min_z = vertex.position.z;
                if (vertex.position.z > max_z) max_z = vertex.position.z;
            }

            ASSETS_ARRAY_APPEND(vertices, vertex_count, vertex_capacity, vertex, error);
        } else if (assets_sv_eq(kind, "vt")) {
            Assets_Vec2 texcoord = {0};
            char *endptr;

            texcoord.x = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed texture coordinate");
                goto defer;
            }
            assets_sv_chop_left(&line, (size_t)(endptr - line.data));
            line = assets_sv_trim_left(line);

            texcoord.y = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed texture coordinate");
                goto defer;
            }

            ASSETS_ARRAY_APPEND(texcoords, texcoord_count, texcoord_capacity, texcoord, error);
        } else if (assets_sv_eq(kind, "vn")) {
            Assets_Vec3 normal = {0};
            char *endptr;

            normal.x = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed normal");
                goto defer;
            }
            assets_sv_chop_left(&line, (size_t)(endptr - line.data));
            line = assets_sv_trim_left(line);

            normal.y = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed normal");
                goto defer;
            }
            assets_sv_chop_left(&line, (size_t)(endptr - line.data));
            line = assets_sv_trim_left(line);

            normal.z = strtof(line.data, &endptr);
            if (endptr == line.data) {
                assets_errorf(error, input_path, line_number, "Malformed normal");
                goto defer;
            }

            ASSETS_ARRAY_APPEND(normals, normal_count, normal_capacity, normal, error);
        } else if (assets_sv_eq(kind, "f")) {
            Assets_Obj_Face face = {0};
            int parsed = 0;
            int face_index = (int)face_count;

            while (line.count > 0) {
                line = assets_sv_trim_left(line);
                if (line.count == 0) break;
                if (parsed >= 3) {
                    assets_errorf(error, input_path, line_number, "Only triangle faces are supported");
                    goto defer;
                }

                if (assets_parse_face_vertex(input_path, line_number, &line,
                                             &face.v[parsed], &face.vt[parsed], &face.vn[parsed],
                                             error) != ASSETS_STATUS_OK) {
                    goto defer;
                }

                if (face.v[parsed] < 0 || (size_t)face.v[parsed] >= vertex_count) {
                    assets_errorf(error, input_path, line_number, "Face references missing vertex");
                    goto defer;
                }

                ASSETS_ARRAY_APPEND(vertices[face.v[parsed]].faces.items,
                                    vertices[face.v[parsed]].faces.count,
                                    vertices[face.v[parsed]].faces.capacity,
                                    face_index,
                                    error);
                parsed += 1;
            }

            if (parsed != 3) {
                assets_errorf(error, input_path, line_number, "Face must have exactly 3 vertices");
                goto defer;
            }

            ASSETS_ARRAY_APPEND(faces, face_count, face_capacity, face, error);
        } else if (assets_sv_eq(kind, "o")) {
            if (saw_object) {
                assets_errorf(error, input_path, line_number, "Only one object per OBJ is supported (previous object at line %zu)", object_line);
                goto defer;
            }
            saw_object = true;
            object_line = line_number;
        } else if (assets_sv_eq(kind, "mtllib") || assets_sv_eq(kind, "usemtl") || assets_sv_eq(kind, "s")) {
            continue;
        } else {
            assets_errorf(error, input_path, line_number, "Unknown OBJ directive");
            goto defer;
        }
    }

    if (!have_bounds) {
        assets_errorf(error, input_path, 0, "OBJ has no vertices");
        goto defer;
    }

    if (assets_validate_model_faces(input_path, faces, face_count, texcoord_count, normal_count, error) != ASSETS_STATUS_OK) {
        goto defer;
    }

    for (size_t start = 0; start < vertex_count; ++start) {
        if (vertices[start].component != 0) continue;
        component_count += 1;
        wave_count = 0;
        next_wave_count = 0;
        ASSETS_ARRAY_APPEND(wave, wave_count, wave_capacity, (int)start, error);
        vertices[start].component = component_count;

        while (wave_count > 0) {
            for (size_t i = 0; i < wave_count; ++i) {
                Assets_Obj_Vertex *vertex = &vertices[wave[i]];
                for (size_t j = 0; j < vertex->faces.count; ++j) {
                    Assets_Obj_Face face = faces[vertex->faces.items[j]];
                    for (int lane = 0; lane < 3; ++lane) {
                        int neighbor = face.v[lane];
                        if (vertices[neighbor].component == 0) {
                            vertices[neighbor].component = component_count;
                            ASSETS_ARRAY_APPEND(next_wave, next_wave_count, next_wave_capacity, neighbor, error);
                        }
                    }
                }
            }

            wave_count = 0;
            int *temp_items = wave;
            size_t temp_capacity = wave_capacity;
            wave = next_wave;
            wave_capacity = next_wave_capacity;
            next_wave = temp_items;
            next_wave_capacity = temp_capacity;
            wave_count = next_wave_count;
            next_wave_count = 0;
        }
    }

    result.vertex_count = vertex_count;
    result.texcoord_count = texcoord_count;
    result.normal_count = normal_count;

    if (vertex_count > 0) {
        result.vertices = (const float (*)[3])malloc(vertex_count * sizeof(float[3]));
        if (result.vertices == NULL) {
            assets_errorf(error, input_path, 0, "Out of memory");
            goto defer;
        }
        for (size_t i = 0; i < vertex_count; ++i) {
            ((float (*)[3])result.vertices)[i][0] = vertices[i].position.x;
            ((float (*)[3])result.vertices)[i][1] = vertices[i].position.y;
            ((float (*)[3])result.vertices)[i][2] = vertices[i].position.z;
            assets_remap_model_vertex(((float (*)[3])result.vertices)[i], scale, min_x, max_x, min_y, max_y, min_z, max_z);
        }
    }

    if (texcoord_count > 0) {
        result.texcoords = (const float (*)[2])malloc(texcoord_count * sizeof(float[2]));
        if (result.texcoords == NULL) {
            assets_errorf(error, input_path, 0, "Out of memory");
            goto defer;
        }
        for (size_t i = 0; i < texcoord_count; ++i) {
            ((float (*)[2])result.texcoords)[i][0] = texcoords[i].x;
            ((float (*)[2])result.texcoords)[i][1] = texcoords[i].y;
        }
    }

    if (normal_count > 0) {
        result.normals = (const float (*)[3])malloc(normal_count * sizeof(float[3]));
        if (result.normals == NULL) {
            assets_errorf(error, input_path, 0, "Out of memory");
            goto defer;
        }
        for (size_t i = 0; i < normal_count; ++i) {
            ((float (*)[3])result.normals)[i][0] = normals[i].x;
            ((float (*)[3])result.normals)[i][1] = normals[i].y;
            ((float (*)[3])result.normals)[i][2] = normals[i].z;
        }
    }

    for (size_t i = 0; i < face_count; ++i) {
        const int *deleted = options != NULL ? options->deleted_components : NULL;
        size_t deleted_count = options != NULL ? options->deleted_component_count : 0;
        if (!assets_face_is_deleted(vertices, &faces[i], deleted, deleted_count)) {
            result.face_count += 1;
        }
    }

    if (result.face_count > 0) {
        size_t out_index = 0;
        result.faces = (const int (*)[9])malloc(result.face_count * sizeof(int[9]));
        if (result.faces == NULL) {
            assets_errorf(error, input_path, 0, "Out of memory");
            goto defer;
        }
        for (size_t i = 0; i < face_count; ++i) {
            const int *deleted = options != NULL ? options->deleted_components : NULL;
            size_t deleted_count = options != NULL ? options->deleted_component_count : 0;
            if (assets_face_is_deleted(vertices, &faces[i], deleted, deleted_count)) continue;
            for (int lane = 0; lane < 3; ++lane) {
                ((int (*)[9])result.faces)[out_index][lane] = faces[i].v[lane];
                ((int (*)[9])result.faces)[out_index][lane + 3] = faces[i].vt[lane];
                ((int (*)[9])result.faces)[out_index][lane + 6] = faces[i].vn[lane];
            }
            out_index += 1;
        }
    }

    *model = result;
    memset(&result, 0, sizeof(result));
    status = ASSETS_STATUS_OK;

defer:
    assets_free_obj_vertices(vertices, vertex_count);
    free(texcoords);
    free(normals);
    free(faces);
    free(file_data);
    free(wave);
    free(next_wave);
    assets_free_model(&result);
    return status;
}

void assets_free_model(Assets_Model *model)
{
    if (model == NULL) return;
    free((void *)model->vertices);
    free((void *)model->texcoords);
    free((void *)model->normals);
    free((void *)model->faces);
    memset(model, 0, sizeof(*model));
}

Assets_Status assets_emit_model_c(Assets_Writer writer, const char *symbol_name, const Assets_Model *model, Assets_Error *error)
{
    char vertices_name[256];
    char texcoords_name[256];
    char normals_name[256];
    char faces_name[256];
    Assets_Status status = ASSETS_STATUS_ERROR;

    snprintf(vertices_name, sizeof(vertices_name), "%s_vertices", symbol_name);
    snprintf(texcoords_name, sizeof(texcoords_name), "%s_texcoords", symbol_name);
    snprintf(normals_name, sizeof(normals_name), "%s_normals", symbol_name);
    snprintf(faces_name, sizeof(faces_name), "%s_faces", symbol_name);

    if (assets_emit_prelude(writer, error) != ASSETS_STATUS_OK) goto defer;
    if (assets_emit_float3_array(writer, error, vertices_name, model->vertices, model->vertex_count) != ASSETS_STATUS_OK) goto defer;
    if (assets_writer_printf(writer, error, "\n") != ASSETS_STATUS_OK) goto defer;
    if (assets_emit_float2_array(writer, error, texcoords_name, model->texcoords, model->texcoord_count) != ASSETS_STATUS_OK) goto defer;
    if (assets_writer_printf(writer, error, "\n") != ASSETS_STATUS_OK) goto defer;
    if (assets_emit_float3_array(writer, error, normals_name, model->normals, model->normal_count) != ASSETS_STATUS_OK) goto defer;
    if (assets_writer_printf(writer, error, "\n") != ASSETS_STATUS_OK) goto defer;
    if (assets_emit_faces_array(writer, error, faces_name, model->faces, model->face_count) != ASSETS_STATUS_OK) goto defer;
    if (assets_writer_printf(writer, error,
                             "\nconst Assets_Model %s = {\n"
                             "    .name = \"%s\",\n"
                             "    .vertex_count = %zu,\n"
                             "    .vertices = %s,\n"
                             "    .texcoord_count = %zu,\n"
                             "    .texcoords = %s,\n"
                             "    .normal_count = %zu,\n"
                             "    .normals = %s,\n"
                             "    .face_count = %zu,\n"
                             "    .faces = %s,\n"
                             "};\n",
                             symbol_name, symbol_name,
                             model->vertex_count, vertices_name,
                             model->texcoord_count, texcoords_name,
                             model->normal_count, normals_name,
                             model->face_count, faces_name) != ASSETS_STATUS_OK) {
        goto defer;
    }

    status = ASSETS_STATUS_OK;

defer:
    return status;
}

Assets_Status assets_compile_model_to_c(const char *input_path, const char *output_path, const char *symbol_name, const Assets_Model_Options *options, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    Assets_Model model = {0};
    Nob_String_Builder rendered = {0};
    Assets_Writer writer = {0};

    if (assets_load_model_obj(input_path, options, &model, error) != ASSETS_STATUS_OK) {
        goto defer;
    }

    writer = assets_writer_from_sb(&rendered);
    if (writer.write == NULL) {
        assets_errorf(error, output_path, 0, "Could not create output builder");
        goto defer;
    }

    status = assets_emit_model_c(writer, symbol_name, &model, error);
    if (status != ASSETS_STATUS_OK) goto defer;

    nob_sb_append_null(&rendered);
    if (!nob_write_entire_file(output_path, rendered.items, rendered.count - 1)) {
        assets_errorf(error, output_path, 0, "Could not write generated asset");
        status = ASSETS_STATUS_ERROR;
        goto defer;
    }

defer:
    assets_free_model(&model);
    if (writer.write != NULL) assets_writer_release(writer);
    free(rendered.items);
    return status;
}

Assets_Status assets_load_image(const char *input_path, const Assets_Image_Options *options, Assets_Image *image, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    int width = 0;
    int height = 0;
    int source_channels = 0;
    int output_channels = 4;
    unsigned char *pixels = NULL;
    unsigned char *copy = NULL;

    (void)options;
    assets_clear_error(error);
    memset(image, 0, sizeof(*image));

    pixels = stbi_load(input_path, &width, &height, &source_channels, output_channels);
    if (pixels == NULL) {
        assets_errorf(error, input_path, 0, "Could not decode image: %s", stbi_failure_reason());
        goto defer;
    }

    size_t pixel_count = (size_t)width * (size_t)height * (size_t)output_channels;
    copy = (unsigned char *)malloc(pixel_count);
    if (copy == NULL) {
        assets_errorf(error, input_path, 0, "Out of memory");
        goto defer;
    }
    memcpy(copy, pixels, pixel_count);

    image->width = width;
    image->height = height;
    image->channels = output_channels;
    image->stride = width * output_channels;
    image->pixels_size = pixel_count;
    image->pixels = copy;
    copy = NULL;

    status = ASSETS_STATUS_OK;

defer:
    if (pixels != NULL) stbi_image_free(pixels);
    free(copy);
    return status;
}

void assets_free_image(Assets_Image *image)
{
    if (image == NULL) return;
    free((void *)image->pixels);
    memset(image, 0, sizeof(*image));
}

Assets_Status assets_emit_image_c(Assets_Writer writer, const char *symbol_name, const Assets_Image *image, Assets_Error *error)
{
    char pixels_name[256];
    Assets_Status status = ASSETS_STATUS_ERROR;

    snprintf(pixels_name, sizeof(pixels_name), "%s_pixels", symbol_name);

    if (assets_emit_prelude(writer, error) != ASSETS_STATUS_OK) goto defer;
    if (assets_emit_bytes_array(writer, error, "unsigned char", pixels_name, image->pixels, image->pixels_size) != ASSETS_STATUS_OK) goto defer;
    if (assets_writer_printf(writer, error,
                             "\nconst Assets_Image %s = {\n"
                             "    .name = \"%s\",\n"
                             "    .width = %d,\n"
                             "    .height = %d,\n"
                             "    .channels = %d,\n"
                             "    .stride = %d,\n"
                             "    .pixels_size = %zu,\n"
                             "    .pixels = %s,\n"
                             "};\n",
                             symbol_name, symbol_name,
                             image->width,
                             image->height,
                             image->channels,
                             image->stride,
                             image->pixels_size,
                             pixels_name) != ASSETS_STATUS_OK) {
        goto defer;
    }

    status = ASSETS_STATUS_OK;

defer:
    return status;
}

Assets_Status assets_compile_image_to_c(const char *input_path, const char *output_path, const char *symbol_name, const Assets_Image_Options *options, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    Assets_Image image = {0};
    Nob_String_Builder rendered = {0};
    Assets_Writer writer = {0};

    if (assets_load_image(input_path, options, &image, error) != ASSETS_STATUS_OK) {
        goto defer;
    }

    writer = assets_writer_from_sb(&rendered);
    if (writer.write == NULL) {
        assets_errorf(error, output_path, 0, "Could not create output builder");
        goto defer;
    }

    status = assets_emit_image_c(writer, symbol_name, &image, error);
    if (status != ASSETS_STATUS_OK) goto defer;

    nob_sb_append_null(&rendered);
    if (!nob_write_entire_file(output_path, rendered.items, rendered.count - 1)) {
        assets_errorf(error, output_path, 0, "Could not write generated asset");
        status = ASSETS_STATUS_ERROR;
        goto defer;
    }

defer:
    assets_free_image(&image);
    if (writer.write != NULL) assets_writer_release(writer);
    free(rendered.items);
    return status;
}

Assets_Status assets_load_font(const char *input_path, const Assets_Font_Options *options, Assets_Font *font, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    unsigned char *ttf_data = NULL;
    size_t ttf_size = 0;
    stbtt_fontinfo info;
    float pixel_height = options != NULL && options->pixel_height > 0.0f ? options->pixel_height : 32.0f;
    int first_codepoint = options != NULL && options->first_codepoint > 0 ? options->first_codepoint : 32;
    int codepoint_count = options != NULL && options->codepoint_count > 0 ? options->codepoint_count : 95;
    int atlas_width = options != NULL && options->atlas_width > 0 ? options->atlas_width : 512;
    int atlas_height = options != NULL && options->atlas_height > 0 ? options->atlas_height : 512;
    unsigned char *atlas = NULL;
    stbtt_bakedchar *baked = NULL;
    Assets_Glyph *glyphs = NULL;
    int ascent = 0, descent = 0, line_gap = 0;
    float scale = 0.0f;
    int bake_result;
    int offset;

    assets_clear_error(error);
    memset(font, 0, sizeof(*font));

    if (assets_read_entire_file(input_path, &ttf_data, &ttf_size, error) != ASSETS_STATUS_OK) {
        goto defer;
    }

    offset = stbtt_GetFontOffsetForIndex(ttf_data, 0);
    if (offset < 0 || !stbtt_InitFont(&info, ttf_data, offset)) {
        assets_errorf(error, input_path, 0, "Could not parse font");
        goto defer;
    }

    atlas = (unsigned char *)calloc((size_t)atlas_width * (size_t)atlas_height, 1);
    baked = (stbtt_bakedchar *)malloc((size_t)codepoint_count * sizeof(*baked));
    glyphs = (Assets_Glyph *)malloc((size_t)codepoint_count * sizeof(*glyphs));
    if (atlas == NULL || baked == NULL || glyphs == NULL) {
        assets_errorf(error, input_path, 0, "Out of memory");
        goto defer;
    }

    bake_result = stbtt_BakeFontBitmap(ttf_data, 0, pixel_height, atlas, atlas_width, atlas_height, first_codepoint, codepoint_count, baked);
    if (bake_result == 0) {
        assets_errorf(error, input_path, 0, "Font bake failed");
        goto defer;
    }
    if (bake_result < 0) {
        assets_errorf(error, input_path, 0, "Atlas is too small for requested font bake");
        goto defer;
    }

    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    scale = stbtt_ScaleForPixelHeight(&info, pixel_height);

    for (int i = 0; i < codepoint_count; ++i) {
        glyphs[i].codepoint = first_codepoint + i;
        glyphs[i].x0 = baked[i].x0;
        glyphs[i].y0 = baked[i].y0;
        glyphs[i].x1 = baked[i].x1;
        glyphs[i].y1 = baked[i].y1;
        glyphs[i].xoff = baked[i].xoff;
        glyphs[i].yoff = baked[i].yoff;
        glyphs[i].xadvance = baked[i].xadvance;
    }

    font->pixel_height = pixel_height;
    font->atlas_width = atlas_width;
    font->atlas_height = atlas_height;
    font->atlas_stride = atlas_width;
    font->first_codepoint = first_codepoint;
    font->glyph_count = codepoint_count;
    font->ascent = ascent * scale;
    font->descent = descent * scale;
    font->line_gap = line_gap * scale;
    font->atlas_size = (size_t)atlas_width * (size_t)atlas_height;
    font->atlas_alpha = atlas;
    font->glyphs = glyphs;

    atlas = NULL;
    glyphs = NULL;
    status = ASSETS_STATUS_OK;

defer:
    free(ttf_data);
    free(atlas);
    free(baked);
    free(glyphs);
    return status;
}

void assets_free_font(Assets_Font *font)
{
    if (font == NULL) return;
    free((void *)font->atlas_alpha);
    free((void *)font->glyphs);
    memset(font, 0, sizeof(*font));
}

Assets_Status assets_emit_font_c(Assets_Writer writer, const char *symbol_name, const Assets_Font *font, Assets_Error *error)
{
    char atlas_name[256];
    char glyphs_name[256];
    Assets_Status status = ASSETS_STATUS_ERROR;

    snprintf(atlas_name, sizeof(atlas_name), "%s_atlas", symbol_name);
    snprintf(glyphs_name, sizeof(glyphs_name), "%s_glyphs", symbol_name);

    if (assets_emit_prelude(writer, error) != ASSETS_STATUS_OK) goto defer;
    if (assets_emit_bytes_array(writer, error, "unsigned char", atlas_name, font->atlas_alpha, font->atlas_size) != ASSETS_STATUS_OK) goto defer;

    if (font->glyph_count == 0 || font->glyphs == NULL) {
        if (assets_writer_printf(writer, error, "\nstatic const Assets_Glyph %s[1] = {{0}};\n", glyphs_name) != ASSETS_STATUS_OK) {
            goto defer;
        }
    } else {
        if (assets_writer_printf(writer, error, "\nstatic const Assets_Glyph %s[%d] = {\n", glyphs_name, font->glyph_count) != ASSETS_STATUS_OK) {
            goto defer;
        }
        for (int i = 0; i < font->glyph_count; ++i) {
            const Assets_Glyph *glyph = &font->glyphs[i];
            if (assets_writer_printf(writer, error,
                                     "    {%d, %d, %d, %d, %d, %a, %a, %a},\n",
                                     glyph->codepoint,
                                     glyph->x0, glyph->y0, glyph->x1, glyph->y1,
                                     glyph->xoff, glyph->yoff, glyph->xadvance) != ASSETS_STATUS_OK) {
                goto defer;
            }
        }
        if (assets_writer_printf(writer, error, "};\n") != ASSETS_STATUS_OK) {
            goto defer;
        }
    }

    if (assets_writer_printf(writer, error,
                             "\nconst Assets_Font %s = {\n"
                             "    .name = \"%s\",\n"
                             "    .pixel_height = %a,\n"
                             "    .atlas_width = %d,\n"
                             "    .atlas_height = %d,\n"
                             "    .atlas_stride = %d,\n"
                             "    .first_codepoint = %d,\n"
                             "    .glyph_count = %d,\n"
                             "    .ascent = %a,\n"
                             "    .descent = %a,\n"
                             "    .line_gap = %a,\n"
                             "    .atlas_size = %zu,\n"
                             "    .atlas_alpha = %s,\n"
                             "    .glyphs = %s,\n"
                             "};\n",
                             symbol_name, symbol_name,
                             font->pixel_height,
                             font->atlas_width,
                             font->atlas_height,
                             font->atlas_stride,
                             font->first_codepoint,
                             font->glyph_count,
                             font->ascent,
                             font->descent,
                             font->line_gap,
                             font->atlas_size,
                             atlas_name,
                             glyphs_name) != ASSETS_STATUS_OK) {
        goto defer;
    }

    status = ASSETS_STATUS_OK;

defer:
    return status;
}

Assets_Status assets_compile_font_to_c(const char *input_path, const char *output_path, const char *symbol_name, const Assets_Font_Options *options, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    Assets_Font font = {0};
    Nob_String_Builder rendered = {0};
    Assets_Writer writer = {0};

    if (assets_load_font(input_path, options, &font, error) != ASSETS_STATUS_OK) {
        goto defer;
    }

    writer = assets_writer_from_sb(&rendered);
    if (writer.write == NULL) {
        assets_errorf(error, output_path, 0, "Could not create output builder");
        goto defer;
    }

    status = assets_emit_font_c(writer, symbol_name, &font, error);
    if (status != ASSETS_STATUS_OK) goto defer;

    nob_sb_append_null(&rendered);
    if (!nob_write_entire_file(output_path, rendered.items, rendered.count - 1)) {
        assets_errorf(error, output_path, 0, "Could not write generated asset");
        status = ASSETS_STATUS_ERROR;
        goto defer;
    }

defer:
    assets_free_font(&font);
    if (writer.write != NULL) assets_writer_release(writer);
    free(rendered.items);
    return status;
}

#endif // ASSETS_IMPLEMENTATION
