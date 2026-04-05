#define ASSETS_IMPLEMENTATION
#include "assets_runtime.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#define ASSETS_PACK_MAGIC_SIZE 8
#define ASSETS_PACK_VERSION 1

typedef struct {
    char magic[ASSETS_PACK_MAGIC_SIZE];
    uint32_t version;
    uint32_t asset_count;
    uint64_t entry_table_offset;
    uint64_t string_table_offset;
    uint64_t string_table_size;
    uint64_t data_offset;
} Assets_Pack_Header;

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint32_t name_offset;
    uint32_t name_length;
    uint64_t payload_offset;
    uint64_t payload_size;
} Assets_Pack_Entry;

typedef struct {
    uint64_t vertex_count;
    uint64_t texcoord_count;
    uint64_t normal_count;
    uint64_t face_count;
    uint64_t vertices_offset;
    uint64_t texcoords_offset;
    uint64_t normals_offset;
    uint64_t faces_offset;
} Assets_Pack_Model_Header;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t stride;
    uint64_t pixels_size;
    uint64_t pixels_offset;
} Assets_Pack_Image_Header;

typedef struct {
    float pixel_height;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t atlas_stride;
    uint32_t first_codepoint;
    uint32_t glyph_count;
    float ascent;
    float descent;
    float line_gap;
    uint64_t atlas_size;
    uint64_t atlas_offset;
    uint64_t glyphs_offset;
} Assets_Pack_Font_Header;

typedef struct {
    const char *assets_root;
    Nob_String_Builder string_table;
    Nob_String_Builder payloads;
    Assets_Pack_Entry *entries;
    size_t entry_count;
    size_t entry_capacity;
} Assets_Pack_Build_Context;

typedef struct {
    const char *assets_root;
    Assets_Pack_Build_Context *pack;
    Assets_Error *error;
} Assets_Pack_Discovery_Context;

typedef struct {
    Assets_Runtime_Model_Entry *items;
    size_t count;
    size_t capacity;
} Assets_Runtime_Model_Entries;

typedef struct {
    Assets_Runtime_Image_Entry *items;
    size_t count;
    size_t capacity;
} Assets_Runtime_Image_Entries;

typedef struct {
    Assets_Runtime_Font_Entry *items;
    size_t count;
    size_t capacity;
} Assets_Runtime_Font_Entries;

typedef struct {
    Assets_Runtime_Audio_Entry *items;
    size_t count;
    size_t capacity;
} Assets_Runtime_Audio_Entries;

static const char assets_pack_magic[ASSETS_PACK_MAGIC_SIZE] = {'L', '3', 'D', 'P', 'A', 'C', 'K', '\0'};

static void assets_runtime_clear_error(Assets_Error *error)
{
    if (error == NULL) return;
    error->message[0] = '\0';
    error->source_path[0] = '\0';
    error->line = 0;
}

static void assets_runtime_copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t count;

    if (dst_size == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    count = strlen(src);
    if (count >= dst_size) count = dst_size - 1;
    memcpy(dst, src, count);
    dst[count] = '\0';
}

static void assets_runtime_errorf(Assets_Error *error, const char *source_path, size_t line, const char *fmt, ...)
{
    va_list args;

    if (error == NULL) return;

    assets_runtime_clear_error(error);
    assets_runtime_copy_string(error->source_path, sizeof(error->source_path), source_path);
    error->line = line;

    va_start(args, fmt);
    vsnprintf(error->message, sizeof(error->message), fmt, args);
    va_end(args);
}

static bool assets_runtime_is_little_endian(void)
{
    const uint32_t value = 1;
    return *((const unsigned char *)&value) == 1;
}

static bool assets_runtime_is_path_sep(char ch)
{
    return ch == '/' || ch == '\\';
}

static bool assets_runtime_mkdirs(const char *path, Assets_Error *error)
{
    char *buffer;
    size_t length;
    size_t start = 0;

    if (path == NULL || path[0] == '\0') {
        return true;
    }

    length = strlen(path);
    buffer = (char *)malloc(length + 1);
    if (buffer == NULL) {
        assets_runtime_errorf(error, path, 0, "Out of memory");
        return false;
    }
    memcpy(buffer, path, length + 1);

    while (length > 0 && assets_runtime_is_path_sep(buffer[length - 1])) {
        buffer[length - 1] = '\0';
        length -= 1;
    }

    for (size_t i = 0; i < length; ++i) {
        if (buffer[i] == '\\') buffer[i] = '/';
    }

    if (length >= 2 && buffer[1] == ':') {
        start = 2;
    }
    if (assets_runtime_is_path_sep(buffer[start])) {
        start += 1;
    }

    for (size_t i = start; i < length; ++i) {
        Nob_File_Type type;

        if (!assets_runtime_is_path_sep(buffer[i])) continue;

        buffer[i] = '\0';
        if (buffer[0] != '\0') {
            type = get_file_type(buffer);
            if (type < 0) {
                if (!mkdir_if_not_exists(buffer)) {
                    assets_runtime_errorf(error, buffer, 0, "Could not create output directory");
                    free(buffer);
                    return false;
                }
            } else if (type != NOB_FILE_DIRECTORY) {
                assets_runtime_errorf(error, buffer, 0, "Output parent path is not a directory");
                free(buffer);
                return false;
            }
        }
        buffer[i] = '/';
    }

    {
        Nob_File_Type type = get_file_type(buffer);
        if (type < 0) {
            if (!mkdir_if_not_exists(buffer)) {
                assets_runtime_errorf(error, buffer, 0, "Could not create output directory");
                free(buffer);
                return false;
            }
        } else if (type != NOB_FILE_DIRECTORY) {
            assets_runtime_errorf(error, buffer, 0, "Output parent path is not a directory");
            free(buffer);
            return false;
        }
    }

    free(buffer);
    return true;
}

static size_t assets_runtime_align_up(size_t value, size_t alignment)
{
    size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

static void assets_runtime_sb_align(Nob_String_Builder *sb, size_t alignment)
{
    unsigned char zero = 0;
    size_t aligned_count = assets_runtime_align_up(sb->count, alignment);
    while (sb->count < aligned_count) {
        nob_da_append(sb, zero);
    }
}

static void assets_runtime_sb_append_bytes(Nob_String_Builder *sb, const void *data, size_t size)
{
    if (size == 0) return;
    nob_da_append_many(sb, data, size);
}

static bool assets_runtime_validate_range(size_t offset, size_t size, size_t total)
{
    return offset <= total && size <= total - offset;
}

static char *assets_runtime_strdup_range(const char *data, size_t count)
{
    char *result = (char *)malloc(count + 1);
    if (result == NULL) return NULL;
    memcpy(result, data, count);
    result[count] = '\0';
    return result;
}

static char *assets_runtime_make_relative_path(const char *assets_root, const char *path)
{
    size_t prefix_len = strlen(assets_root);
    const char *relative = path;

    if (strncmp(path, assets_root, prefix_len) == 0) {
        relative = path + prefix_len;
        while (assets_runtime_is_path_sep(*relative)) {
            relative += 1;
        }
    }

    return assets_runtime_strdup_range(relative, strlen(relative));
}

static char *assets_runtime_make_asset_name(const char *relative_path)
{
    size_t count = strlen(relative_path);
    size_t dot_index = count;
    char *name = NULL;

    for (size_t i = count; i > 0; --i) {
        char ch = relative_path[i - 1];
        if (ch == '.') {
            dot_index = i - 1;
            break;
        }
        if (assets_runtime_is_path_sep(ch)) {
            break;
        }
    }

    name = assets_runtime_strdup_range(relative_path, dot_index);
    if (name == NULL) return NULL;

    for (size_t i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '\\') name[i] = '/';
    }

    return name;
}

static const char *assets_runtime_file_ext(const char *path)
{
    const char *result = NULL;

    for (const char *it = path; *it != '\0'; ++it) {
        if (*it == '.') {
            result = it;
        } else if (assets_runtime_is_path_sep(*it)) {
            result = NULL;
        }
    }

    return result;
}

static Assets_Runtime_Type assets_runtime_type_from_path(const char *path)
{
    const char *ext = assets_runtime_file_ext(path);

    if (ext == NULL) return 0;
    if (strcmp(ext, ".obj") == 0) return ASSETS_RUNTIME_TYPE_MODEL;
    if (strcmp(ext, ".png") == 0) return ASSETS_RUNTIME_TYPE_IMAGE;
    if (strcmp(ext, ".jpg") == 0) return ASSETS_RUNTIME_TYPE_IMAGE;
    if (strcmp(ext, ".jpeg") == 0) return ASSETS_RUNTIME_TYPE_IMAGE;
    if (strcmp(ext, ".ttf") == 0) return ASSETS_RUNTIME_TYPE_FONT;
    if (strcmp(ext, ".wav") == 0) return ASSETS_RUNTIME_TYPE_AUDIO;
    if (strcmp(ext, ".mp3") == 0) return ASSETS_RUNTIME_TYPE_AUDIO;
    if (strcmp(ext, ".flac") == 0) return ASSETS_RUNTIME_TYPE_AUDIO;
    if (strcmp(ext, ".ogg") == 0) return ASSETS_RUNTIME_TYPE_AUDIO;
    return 0;
}

static Assets_Model_Options assets_runtime_default_model_options(const char *relative_path)
{
    Assets_Model_Options options = {0};
    (void)relative_path;
    options.scale = 1.0f;
    return options;
}

static Assets_Image_Options assets_runtime_default_image_options(void)
{
    Assets_Image_Options options = {0};
    options.force_rgba = 1;
    return options;
}

static Assets_Font_Options assets_runtime_default_font_options(void)
{
    Assets_Font_Options options = {0};
    options.pixel_height = 32.0f;
    options.first_codepoint = 32;
    options.codepoint_count = 95;
    options.atlas_width = 512;
    options.atlas_height = 512;
    return options;
}

static bool assets_runtime_append_name(
    Nob_String_Builder *string_table,
    const char *name,
    uint32_t *name_offset,
    uint32_t *name_length,
    Assets_Error *error
)
{
    size_t count = strlen(name);

    if (string_table->count > UINT32_MAX) {
        assets_runtime_errorf(error, NULL, 0, "String table is too large");
        return false;
    }
    if (count > UINT32_MAX) {
        assets_runtime_errorf(error, NULL, 0, "Asset name is too long");
        return false;
    }

    *name_offset = (uint32_t)string_table->count;
    *name_length = (uint32_t)count;
    assets_runtime_sb_append_bytes(string_table, name, count + 1);
    return true;
}

static bool assets_runtime_has_duplicate_name(
    const Assets_Pack_Build_Context *pack,
    Assets_Runtime_Type type,
    const char *name
)
{
    for (size_t i = 0; i < pack->entry_count; ++i) {
        const char *existing_name;

        if (pack->entries[i].type != (uint32_t)type) continue;
        if (!assets_runtime_validate_range(pack->entries[i].name_offset, pack->entries[i].name_length + 1, pack->string_table.count)) continue;

        existing_name = pack->string_table.items + pack->entries[i].name_offset;
        if (strcmp(existing_name, name) == 0) {
            return true;
        }
    }

    return false;
}

static bool assets_runtime_serialize_model(const Assets_Model *model, Nob_String_Builder *payload, Assets_Error *error)
{
    Assets_Pack_Model_Header header = {0};

    header.vertex_count = model->vertex_count;
    header.texcoord_count = model->texcoord_count;
    header.normal_count = model->normal_count;
    header.face_count = model->face_count;

    assets_runtime_sb_append_bytes(payload, &header, sizeof(header));
    assets_runtime_sb_align(payload, 8);

    if (model->vertex_count > 0) {
        header.vertices_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, model->vertices, model->vertex_count * sizeof(float[3]));
        assets_runtime_sb_align(payload, 8);
    }
    if (model->texcoord_count > 0) {
        header.texcoords_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, model->texcoords, model->texcoord_count * sizeof(float[2]));
        assets_runtime_sb_align(payload, 8);
    }
    if (model->normal_count > 0) {
        header.normals_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, model->normals, model->normal_count * sizeof(float[3]));
        assets_runtime_sb_align(payload, 8);
    }
    if (model->face_count > 0) {
        header.faces_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, model->faces, model->face_count * sizeof(int[9]));
        assets_runtime_sb_align(payload, 8);
    }

    memcpy(payload->items, &header, sizeof(header));
    (void)error;
    return true;
}

static bool assets_runtime_serialize_image(const Assets_Image *image, Nob_String_Builder *payload, Assets_Error *error)
{
    Assets_Pack_Image_Header header = {0};

    if (image->width < 0 || image->height < 0 || image->channels < 0 || image->stride < 0) {
        assets_runtime_errorf(error, NULL, 0, "Image fields are out of range");
        return false;
    }

    header.width = (uint32_t)image->width;
    header.height = (uint32_t)image->height;
    header.channels = (uint32_t)image->channels;
    header.stride = (uint32_t)image->stride;
    header.pixels_size = image->pixels_size;

    assets_runtime_sb_append_bytes(payload, &header, sizeof(header));
    assets_runtime_sb_align(payload, 8);

    if (image->pixels_size > 0) {
        header.pixels_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, image->pixels, image->pixels_size);
        assets_runtime_sb_align(payload, 8);
    }

    memcpy(payload->items, &header, sizeof(header));
    return true;
}

static bool assets_runtime_serialize_font(const Assets_Font *font, Nob_String_Builder *payload, Assets_Error *error)
{
    Assets_Pack_Font_Header header = {0};

    if (font->atlas_width < 0 || font->atlas_height < 0 || font->atlas_stride < 0 || font->first_codepoint < 0 || font->glyph_count < 0) {
        assets_runtime_errorf(error, NULL, 0, "Font fields are out of range");
        return false;
    }

    header.pixel_height = font->pixel_height;
    header.atlas_width = (uint32_t)font->atlas_width;
    header.atlas_height = (uint32_t)font->atlas_height;
    header.atlas_stride = (uint32_t)font->atlas_stride;
    header.first_codepoint = (uint32_t)font->first_codepoint;
    header.glyph_count = (uint32_t)font->glyph_count;
    header.ascent = font->ascent;
    header.descent = font->descent;
    header.line_gap = font->line_gap;
    header.atlas_size = font->atlas_size;

    assets_runtime_sb_append_bytes(payload, &header, sizeof(header));
    assets_runtime_sb_align(payload, 8);

    if (font->atlas_size > 0) {
        header.atlas_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, font->atlas_alpha, font->atlas_size);
        assets_runtime_sb_align(payload, 8);
    }
    if (font->glyph_count > 0) {
        header.glyphs_offset = payload->count;
        assets_runtime_sb_append_bytes(payload, font->glyphs, (size_t)font->glyph_count * sizeof(Assets_Glyph));
        assets_runtime_sb_align(payload, 8);
    }

    memcpy(payload->items, &header, sizeof(header));
    return true;
}

static bool assets_runtime_append_payload(
    Assets_Pack_Build_Context *pack,
    Assets_Runtime_Type type,
    const char *name,
    const void *payload_data,
    size_t payload_size,
    Assets_Error *error
)
{
    Assets_Pack_Entry entry = {0};

    if (assets_runtime_has_duplicate_name(pack, type, name)) {
        assets_runtime_errorf(error, NULL, 0, "Duplicate asset name '%s' for type %u", name, (unsigned)type);
        return false;
    }
    if (!assets_runtime_append_name(&pack->string_table, name, &entry.name_offset, &entry.name_length, error)) {
        return false;
    }

    assets_runtime_sb_align(&pack->payloads, 8);
    entry.type = (uint32_t)type;
    entry.payload_offset = pack->payloads.count;
    entry.payload_size = payload_size;
    assets_runtime_sb_append_bytes(&pack->payloads, payload_data, payload_size);
    {
        struct {
            Assets_Pack_Entry *items;
            size_t count;
            size_t capacity;
        } entries_da = {pack->entries, pack->entry_count, pack->entry_capacity};

        nob_da_append(&entries_da, entry);
        pack->entries = entries_da.items;
        pack->entry_count = entries_da.count;
        pack->entry_capacity = entries_da.capacity;
    }
    return true;
}

static bool assets_runtime_build_asset(const char *source_path, const char *relative_path, Assets_Pack_Build_Context *pack, Assets_Error *error)
{
    Assets_Runtime_Type type = assets_runtime_type_from_path(source_path);
    char *asset_name = NULL;
    Nob_String_Builder payload = {0};
    bool ok = false;

    if (type == 0) {
        return true;
    }

    asset_name = assets_runtime_make_asset_name(relative_path);
    if (asset_name == NULL) {
        assets_runtime_errorf(error, source_path, 0, "Out of memory");
        goto defer;
    }

    if (type == ASSETS_RUNTIME_TYPE_MODEL) {
        Assets_Model model = {0};
        Assets_Model_Options options = assets_runtime_default_model_options(relative_path);

        if (assets_load_model_obj(source_path, &options, &model, error) != ASSETS_STATUS_OK) goto defer;
        if (!assets_runtime_serialize_model(&model, &payload, error)) {
            assets_free_model(&model);
            goto defer;
        }
        ok = assets_runtime_append_payload(pack, type, asset_name, payload.items, payload.count, error);
        assets_free_model(&model);
        goto defer;
    }

    if (type == ASSETS_RUNTIME_TYPE_IMAGE) {
        Assets_Image image = {0};
        Assets_Image_Options options = assets_runtime_default_image_options();

        if (assets_load_image(source_path, &options, &image, error) != ASSETS_STATUS_OK) goto defer;
        if (!assets_runtime_serialize_image(&image, &payload, error)) {
            assets_free_image(&image);
            goto defer;
        }
        ok = assets_runtime_append_payload(pack, type, asset_name, payload.items, payload.count, error);
        assets_free_image(&image);
        goto defer;
    }

    if (type == ASSETS_RUNTIME_TYPE_FONT) {
        Assets_Font font = {0};
        Assets_Font_Options options = assets_runtime_default_font_options();

        if (assets_load_font(source_path, &options, &font, error) != ASSETS_STATUS_OK) goto defer;
        if (!assets_runtime_serialize_font(&font, &payload, error)) {
            assets_free_font(&font);
            goto defer;
        }
        ok = assets_runtime_append_payload(pack, type, asset_name, payload.items, payload.count, error);
        assets_free_font(&font);
        goto defer;
    }

    if (type == ASSETS_RUNTIME_TYPE_AUDIO) {
        if (!nob_read_entire_file(source_path, &payload)) {
            assets_runtime_errorf(error, source_path, 0, "Could not read audio file");
            goto defer;
        }
        ok = assets_runtime_append_payload(pack, type, asset_name, payload.items, payload.count, error);
    }

defer:
    free(asset_name);
    free(payload.items);
    return ok;
}

static bool assets_runtime_discover_asset(Walk_Entry entry)
{
    Assets_Pack_Discovery_Context *context = entry.data;
    char *relative_path = NULL;
    bool ok;

    if (entry.type == NOB_FILE_DIRECTORY) {
        if (strcmp(path_name(entry.path), "generated") == 0) {
            *entry.action = NOB_WALK_SKIP;
        }
        return true;
    }
    if (entry.type != NOB_FILE_REGULAR) {
        return true;
    }

    relative_path = assets_runtime_make_relative_path(context->assets_root, entry.path);
    if (relative_path == NULL) {
        assets_runtime_errorf(context->error, entry.path, 0, "Out of memory");
        return false;
    }

    ok = assets_runtime_build_asset(entry.path, relative_path, context->pack, context->error);
    free(relative_path);
    return ok;
}

static bool assets_runtime_collect_assets(const char *assets_dir, Assets_Pack_Build_Context *pack, Assets_Error *error)
{
    Assets_Pack_Discovery_Context context = {
        .assets_root = assets_dir,
        .pack = pack,
        .error = error,
    };

    return walk_dir(assets_dir, assets_runtime_discover_asset, .data = &context);
}

static bool assets_runtime_finalize_pack(
    const Assets_Pack_Entry *relative_entries,
    size_t entry_count,
    const Nob_String_Builder *string_table,
    const Nob_String_Builder *payloads,
    const char *output_path,
    Assets_Error *error
)
{
    Assets_Pack_Header header = {0};
    Nob_String_Builder file = {0};
    size_t entry_table_size = entry_count * sizeof(Assets_Pack_Entry);
    size_t string_table_offset = sizeof(header) + entry_table_size;
    size_t data_offset = assets_runtime_align_up(string_table_offset + string_table->count, 8);
    size_t data_padding = data_offset - (string_table_offset + string_table->count);
    Assets_Pack_Entry *entries = NULL;
    unsigned char zero = 0;
    bool ok = false;

    memcpy(header.magic, assets_pack_magic, sizeof(header.magic));
    header.version = ASSETS_PACK_VERSION;
    header.asset_count = (uint32_t)entry_count;
    header.entry_table_offset = sizeof(header);
    header.string_table_offset = string_table_offset;
    header.string_table_size = string_table->count;
    header.data_offset = data_offset;

    if (entry_table_size > 0) {
        entries = (Assets_Pack_Entry *)malloc(entry_table_size);
        if (entries == NULL) {
            assets_runtime_errorf(error, output_path, 0, "Out of memory");
            goto defer;
        }
    }

    for (size_t i = 0; i < entry_count; ++i) {
        entries[i] = relative_entries[i];
        entries[i].payload_offset += data_offset;
    }

    assets_runtime_sb_append_bytes(&file, &header, sizeof(header));
    assets_runtime_sb_append_bytes(&file, entries, entry_table_size);
    assets_runtime_sb_append_bytes(&file, string_table->items, string_table->count);
    while (data_padding-- > 0) {
        nob_da_append(&file, zero);
    }
    assets_runtime_sb_append_bytes(&file, payloads->items, payloads->count);

    if (!nob_write_entire_file(output_path, file.items, file.count)) {
        assets_runtime_errorf(error, output_path, 0, "Could not write asset pack");
        goto defer;
    }

    ok = true;

defer:
    free(entries);
    free(file.items);
    return ok;
}

Assets_Status assets_build_pack_from_dir(const char *assets_dir, const char *output_path, Assets_Error *error)
{
    Assets_Pack_Build_Context pack = {0};
    Assets_Status status = ASSETS_STATUS_ERROR;

    assets_runtime_clear_error(error);

    if (!assets_runtime_is_little_endian()) {
        assets_runtime_errorf(error, assets_dir, 0, "Asset pack building currently requires a little-endian machine");
        goto defer;
    }
    if (!file_exists(assets_dir)) {
        assets_runtime_errorf(error, assets_dir, 0, "Assets directory does not exist");
        goto defer;
    }
    if (!assets_runtime_mkdirs(temp_dir_name(output_path), error)) goto defer;
    if (!assets_runtime_collect_assets(assets_dir, &pack, error)) {
        goto defer;
    }
    if (pack.entry_count > UINT32_MAX) {
        assets_runtime_errorf(error, output_path, 0, "Too many packed assets");
        goto defer;
    }
    if (!assets_runtime_finalize_pack(pack.entries, pack.entry_count, &pack.string_table, &pack.payloads, output_path, error)) {
        goto defer;
    }

    status = ASSETS_STATUS_OK;

defer:
    free(pack.entries);
    free(pack.string_table.items);
    free(pack.payloads.items);
    return status;
}

Assets_Status assets_build_empty_pack(const char *output_path, Assets_Error *error)
{
    Assets_Status status = ASSETS_STATUS_ERROR;
    Nob_String_Builder string_table = {0};
    Nob_String_Builder payloads = {0};

    assets_runtime_clear_error(error);

    if (!assets_runtime_is_little_endian()) {
        assets_runtime_errorf(error, output_path, 0, "Asset pack building currently requires a little-endian machine");
        goto defer;
    }
    if (!assets_runtime_mkdirs(temp_dir_name(output_path), error)) goto defer;
    if (!assets_runtime_finalize_pack(NULL, 0, &string_table, &payloads, output_path, error)) {
        goto defer;
    }

    status = ASSETS_STATUS_OK;

defer:
    free(string_table.items);
    free(payloads.items);
    return status;
}

static const Assets_Pack_Entry *assets_runtime_get_entry(
    const unsigned char *pack_data,
    size_t pack_size,
    const Assets_Pack_Header *header,
    size_t index,
    Assets_Error *error,
    const char *path
)
{
    size_t table_size = (size_t)header->asset_count * sizeof(Assets_Pack_Entry);
    size_t entry_offset;

    if (!assets_runtime_validate_range((size_t)header->entry_table_offset, table_size, pack_size)) {
        assets_runtime_errorf(error, path, 0, "Asset entry table is out of bounds");
        return NULL;
    }

    entry_offset = (size_t)header->entry_table_offset + index * sizeof(Assets_Pack_Entry);
    if (!assets_runtime_validate_range(entry_offset, sizeof(Assets_Pack_Entry), pack_size)) {
        assets_runtime_errorf(error, path, 0, "Asset entry is out of bounds");
        return NULL;
    }

    return (const Assets_Pack_Entry *)(pack_data + entry_offset);
}

static const char *assets_runtime_entry_name(
    const unsigned char *pack_data,
    size_t pack_size,
    const Assets_Pack_Header *header,
    const Assets_Pack_Entry *entry,
    Assets_Error *error,
    const char *path
)
{
    size_t absolute_offset = (size_t)header->string_table_offset + entry->name_offset;

    if (!assets_runtime_validate_range((size_t)header->string_table_offset, (size_t)header->string_table_size, pack_size)) {
        assets_runtime_errorf(error, path, 0, "String table is out of bounds");
        return NULL;
    }
    if (!assets_runtime_validate_range(entry->name_offset, entry->name_length + 1, (size_t)header->string_table_size)) {
        assets_runtime_errorf(error, path, 0, "Asset name is out of bounds");
        return NULL;
    }
    if (!assets_runtime_validate_range(absolute_offset, entry->name_length + 1, pack_size)) {
        assets_runtime_errorf(error, path, 0, "Asset name points outside the pack");
        return NULL;
    }

    return (const char *)(pack_data + absolute_offset);
}

static bool assets_runtime_parse_model(
    const unsigned char *payload,
    size_t payload_size,
    const char *name,
    Assets_Runtime_Model_Entries *models,
    Assets_Error *error,
    const char *path
)
{
    const Assets_Pack_Model_Header *header;
    Assets_Runtime_Model_Entry entry = {0};
    size_t vertices_size;
    size_t texcoords_size;
    size_t normals_size;
    size_t faces_size;

    if (!assets_runtime_validate_range(0, sizeof(Assets_Pack_Model_Header), payload_size)) {
        assets_runtime_errorf(error, path, 0, "Model payload is truncated");
        return false;
    }

    header = (const Assets_Pack_Model_Header *)payload;
    entry.name = name;
    entry.asset.name = name;
    entry.asset.vertex_count = (size_t)header->vertex_count;
    entry.asset.texcoord_count = (size_t)header->texcoord_count;
    entry.asset.normal_count = (size_t)header->normal_count;
    entry.asset.face_count = (size_t)header->face_count;

    vertices_size = entry.asset.vertex_count * sizeof(float[3]);
    texcoords_size = entry.asset.texcoord_count * sizeof(float[2]);
    normals_size = entry.asset.normal_count * sizeof(float[3]);
    faces_size = entry.asset.face_count * sizeof(int[9]);

    if (entry.asset.vertex_count > 0) {
        if (!assets_runtime_validate_range((size_t)header->vertices_offset, vertices_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Model vertices are out of bounds");
            return false;
        }
        entry.asset.vertices = (const float (*)[3])(payload + (size_t)header->vertices_offset);
    }
    if (entry.asset.texcoord_count > 0) {
        if (!assets_runtime_validate_range((size_t)header->texcoords_offset, texcoords_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Model texcoords are out of bounds");
            return false;
        }
        entry.asset.texcoords = (const float (*)[2])(payload + (size_t)header->texcoords_offset);
    }
    if (entry.asset.normal_count > 0) {
        if (!assets_runtime_validate_range((size_t)header->normals_offset, normals_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Model normals are out of bounds");
            return false;
        }
        entry.asset.normals = (const float (*)[3])(payload + (size_t)header->normals_offset);
    }
    if (entry.asset.face_count > 0) {
        if (!assets_runtime_validate_range((size_t)header->faces_offset, faces_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Model faces are out of bounds");
            return false;
        }
        entry.asset.faces = (const int (*)[9])(payload + (size_t)header->faces_offset);
    }

    nob_da_append(models, entry);
    return true;
}

static bool assets_runtime_parse_image(
    const unsigned char *payload,
    size_t payload_size,
    const char *name,
    Assets_Runtime_Image_Entries *images,
    Assets_Error *error,
    const char *path
)
{
    const Assets_Pack_Image_Header *header;
    Assets_Runtime_Image_Entry entry = {0};

    if (!assets_runtime_validate_range(0, sizeof(Assets_Pack_Image_Header), payload_size)) {
        assets_runtime_errorf(error, path, 0, "Image payload is truncated");
        return false;
    }

    header = (const Assets_Pack_Image_Header *)payload;
    entry.name = name;
    entry.asset.name = name;
    entry.asset.width = (int)header->width;
    entry.asset.height = (int)header->height;
    entry.asset.channels = (int)header->channels;
    entry.asset.stride = (int)header->stride;
    entry.asset.pixels_size = (size_t)header->pixels_size;

    if (entry.asset.pixels_size > 0) {
        if (!assets_runtime_validate_range((size_t)header->pixels_offset, entry.asset.pixels_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Image pixels are out of bounds");
            return false;
        }
        entry.asset.pixels = payload + (size_t)header->pixels_offset;
    }

    nob_da_append(images, entry);
    return true;
}

static bool assets_runtime_parse_font(
    const unsigned char *payload,
    size_t payload_size,
    const char *name,
    Assets_Runtime_Font_Entries *fonts,
    Assets_Error *error,
    const char *path
)
{
    const Assets_Pack_Font_Header *header;
    Assets_Runtime_Font_Entry entry = {0};
    size_t glyphs_size;

    if (!assets_runtime_validate_range(0, sizeof(Assets_Pack_Font_Header), payload_size)) {
        assets_runtime_errorf(error, path, 0, "Font payload is truncated");
        return false;
    }

    header = (const Assets_Pack_Font_Header *)payload;
    entry.name = name;
    entry.asset.name = name;
    entry.asset.pixel_height = header->pixel_height;
    entry.asset.atlas_width = (int)header->atlas_width;
    entry.asset.atlas_height = (int)header->atlas_height;
    entry.asset.atlas_stride = (int)header->atlas_stride;
    entry.asset.first_codepoint = (int)header->first_codepoint;
    entry.asset.glyph_count = (int)header->glyph_count;
    entry.asset.ascent = header->ascent;
    entry.asset.descent = header->descent;
    entry.asset.line_gap = header->line_gap;
    entry.asset.atlas_size = (size_t)header->atlas_size;

    if (entry.asset.atlas_size > 0) {
        if (!assets_runtime_validate_range((size_t)header->atlas_offset, entry.asset.atlas_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Font atlas is out of bounds");
            return false;
        }
        entry.asset.atlas_alpha = payload + (size_t)header->atlas_offset;
    }

    glyphs_size = (size_t)entry.asset.glyph_count * sizeof(Assets_Glyph);
    if (entry.asset.glyph_count > 0) {
        if (!assets_runtime_validate_range((size_t)header->glyphs_offset, glyphs_size, payload_size)) {
            assets_runtime_errorf(error, path, 0, "Font glyphs are out of bounds");
            return false;
        }
        entry.asset.glyphs = (const Assets_Glyph *)(payload + (size_t)header->glyphs_offset);
    }

    nob_da_append(fonts, entry);
    return true;
}

static bool assets_runtime_parse_audio(
    const unsigned char *payload,
    size_t payload_size,
    const char *name,
    Assets_Runtime_Audio_Entries *audios,
    Assets_Error *error,
    const char *path
)
{
    Assets_Runtime_Audio_Entry entry = {0};

    (void)error;
    (void)path;

    entry.name = name;
    entry.asset.name = name;
    entry.asset.data = payload;
    entry.asset.data_size = payload_size;

    nob_da_append(audios, entry);
    return true;
}

Assets_Status assets_runtime_load_pack(const char *path, Assets_Runtime_Registry *out, Assets_Error *error)
{
    Nob_String_Builder file = {0};
    const Assets_Pack_Header *header;
    Assets_Runtime_Model_Entries models = {0};
    Assets_Runtime_Image_Entries images = {0};
    Assets_Runtime_Font_Entries fonts = {0};
    Assets_Runtime_Audio_Entries audios = {0};
    Assets_Status status = ASSETS_STATUS_ERROR;

    assets_runtime_clear_error(error);
    memset(out, 0, sizeof(*out));

    if (!assets_runtime_is_little_endian()) {
        assets_runtime_errorf(error, path, 0, "Asset pack loading currently requires a little-endian machine");
        goto defer;
    }
    if (!nob_read_entire_file(path, &file)) {
        assets_runtime_errorf(error, path, 0, "Could not read asset pack");
        goto defer;
    }
    if (!assets_runtime_validate_range(0, sizeof(Assets_Pack_Header), file.count)) {
        assets_runtime_errorf(error, path, 0, "Asset pack header is truncated");
        goto defer;
    }

    header = (const Assets_Pack_Header *)file.items;
    if (memcmp(header->magic, assets_pack_magic, sizeof(header->magic)) != 0) {
        assets_runtime_errorf(error, path, 0, "Asset pack magic is invalid");
        goto defer;
    }
    if (header->version != ASSETS_PACK_VERSION) {
        assets_runtime_errorf(error, path, 0, "Unsupported asset pack version %u", header->version);
        goto defer;
    }

    for (size_t i = 0; i < header->asset_count; ++i) {
        const Assets_Pack_Entry *entry = assets_runtime_get_entry((const unsigned char *)file.items, file.count, header, i, error, path);
        const char *name;
        const unsigned char *payload;
        size_t payload_size;

        if (entry == NULL) goto defer;

        name = assets_runtime_entry_name((const unsigned char *)file.items, file.count, header, entry, error, path);
        if (name == NULL) goto defer;

        payload_size = (size_t)entry->payload_size;
        if (!assets_runtime_validate_range((size_t)entry->payload_offset, payload_size, file.count)) {
            assets_runtime_errorf(error, path, 0, "Payload for asset '%s' is out of bounds", name);
            goto defer;
        }
        payload = (const unsigned char *)file.items + (size_t)entry->payload_offset;

        switch ((Assets_Runtime_Type)entry->type) {
        case ASSETS_RUNTIME_TYPE_MODEL:
            if (!assets_runtime_parse_model(payload, payload_size, name, &models, error, path)) goto defer;
            break;
        case ASSETS_RUNTIME_TYPE_IMAGE:
            if (!assets_runtime_parse_image(payload, payload_size, name, &images, error, path)) goto defer;
            break;
        case ASSETS_RUNTIME_TYPE_FONT:
            if (!assets_runtime_parse_font(payload, payload_size, name, &fonts, error, path)) goto defer;
            break;
        case ASSETS_RUNTIME_TYPE_AUDIO:
            if (!assets_runtime_parse_audio(payload, payload_size, name, &audios, error, path)) goto defer;
            break;
        default:
            assets_runtime_errorf(error, path, 0, "Unknown asset type %u", entry->type);
            goto defer;
        }
    }

    out->pack_data = (unsigned char *)file.items;
    out->pack_size = file.count;
    out->models = models.items;
    out->model_count = models.count;
    out->images = images.items;
    out->image_count = images.count;
    out->fonts = fonts.items;
    out->font_count = fonts.count;
    out->audios = audios.items;
    out->audio_count = audios.count;
    status = ASSETS_STATUS_OK;

    file.items = NULL;
    models.items = NULL;
    images.items = NULL;
    fonts.items = NULL;
    audios.items = NULL;

defer:
    free(file.items);
    free(models.items);
    free(images.items);
    free(fonts.items);
    free(audios.items);
    return status;
}

void assets_runtime_unload(Assets_Runtime_Registry *registry)
{
    if (registry == NULL) return;
    free(registry->pack_data);
    free(registry->models);
    free(registry->images);
    free(registry->fonts);
    free(registry->audios);
    memset(registry, 0, sizeof(*registry));
}

const Assets_Model *assets_runtime_find_model(const Assets_Runtime_Registry *registry, const char *name)
{
    if (registry == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < registry->model_count; ++i) {
        if (strcmp(registry->models[i].name, name) == 0) return &registry->models[i].asset;
    }
    return NULL;
}

const Assets_Image *assets_runtime_find_image(const Assets_Runtime_Registry *registry, const char *name)
{
    if (registry == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < registry->image_count; ++i) {
        if (strcmp(registry->images[i].name, name) == 0) return &registry->images[i].asset;
    }
    return NULL;
}

const Assets_Font *assets_runtime_find_font(const Assets_Runtime_Registry *registry, const char *name)
{
    if (registry == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < registry->font_count; ++i) {
        if (strcmp(registry->fonts[i].name, name) == 0) return &registry->fonts[i].asset;
    }
    return NULL;
}

const Assets_Runtime_Audio *assets_runtime_find_audio(const Assets_Runtime_Registry *registry, const char *name)
{
    if (registry == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < registry->audio_count; ++i) {
        if (strcmp(registry->audios[i].name, name) == 0) return &registry->audios[i].asset;
    }
    return NULL;
}
