#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGFW_IMPLEMENTATION
#define RGFW_DEBUG
#include "third_party/RGFW.h"

#ifdef ERROR
#undef ERROR
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#include "assets_runtime.h"
#include "core.h"
#include "lua_api.h"

#define DEFAULT_WINDOW_WIDTH 980
#define DEFAULT_WINDOW_HEIGHT 540
#define DEFAULT_BACKGROUND_COLOR 0xFF181818

typedef struct {
    const char *assets_root;
    Nob_File_Paths *inputs;
} Pack_Input_Context;

typedef struct {
    const char *name;
    const char *stub;
} Lua_Stub_Function_Spec;

typedef struct {
    const char *module_name;
    const char *class_name;
    const Lua_Stub_Function_Spec *functions;
    size_t function_count;
} Lua_Stub_Module_Spec;

typedef struct {
    const char *name;
    const char *stub;
} Lua_Stub_Method_Spec;

static const char *starter_main_lua =
    "local bg = graphics.rgba(24, 24, 24, 255)\n"
    "\n"
    "function update(dt)\n"
    "    if rgfw.is_key_pressed(rgfw.key_escape) then\n"
    "        rgfw.close()\n"
    "    end\n"
    "end\n"
    "\n"
    "function draw()\n"
    "    core.begin_frame(bg, 1.0)\n"
    "end\n";

static const char *project_luarc_json =
    "{\n"
    "  \"workspace\": {\n"
    "    \"library\": [\n"
    "      \"./.limerence/meta\"\n"
    "    ],\n"
    "    \"checkThirdParty\": false\n"
    "  },\n"
    "  \"completion\": {\n"
    "    \"callSnippet\": \"Both\"\n"
    "  },\n"
    "  \"hint\": {\n"
    "    \"enable\": true\n"
    "  }\n"
    "}\n";

static const char *lua_stub_prelude =
    "---@meta\n"
    "\n"
    "---@class LimerenceVec2\n"
    "---@field x number\n"
    "---@field y number\n"
    "\n"
    "---@class LimerenceVec3\n"
    "---@field x number\n"
    "---@field y number\n"
    "---@field z number\n"
    "\n"
    "---@class LimerenceVec4\n"
    "---@field x number\n"
    "---@field y number\n"
    "---@field z number\n"
    "---@field w number\n"
    "\n"
    "---@class LimerenceMat4\n"
    "---@field type string\n"
    "---@field [integer] number\n"
    "\n"
    "---@class LimerenceDrawOptions\n"
    "---@field near_plane? number\n"
    "---@field lighting_enabled? boolean\n"
    "---@field lighting_mode? 'none'|'flat'|'gouraud'\n"
    "---@field light_direction_world? LimerenceVec3\n"
    "---@field ambient_strength? number\n"
    "---@field diffuse_strength? number\n"
    "---@field specular_strength? number\n"
    "---@field shininess? number\n"
    "---@field occlusion_culling? boolean\n"
    "---@field occlusion_test_step? integer\n"
    "---@field fog_start? number\n"
    "---@field fog_end? number\n"
    "---@field fog_power? number\n"
    "---@field fog_color? integer\n"
    "---@field backface_culling? boolean\n"
    "\n"
    "---@class LimerenceCamera\n"
    "local LimerenceCamera = {}\n"
    "\n"
    "function hello_world() end\n"
    "\n";

static const char *lua_stub_rgfw_constants[] = {
    "key_escape",
    "key_up",
    "key_down",
    "key_left",
    "key_right",
    "key_w",
    "key_a",
    "key_s",
    "key_d",
    "key_q",
    "key_e",
    "key_space",
    "key_shift_left",
    "key_shift_right",
    "mouse_left",
    "mouse_right",
    "mouse_middle",
};

static const Lua_Stub_Function_Spec lua_stub_rgfw_functions[] = {
    {"get_width", "---@return integer\nfunction rgfw.get_width() end\n"},
    {"get_height", "---@return integer\nfunction rgfw.get_height() end\n"},
    {"should_close", "---@return boolean\nfunction rgfw.should_close() end\n"},
    {"close", "function rgfw.close() end\n"},
    {"set_exit_key", "---@param key integer\nfunction rgfw.set_exit_key(key) end\n"},
    {"show_mouse", "---@param visible boolean\nfunction rgfw.show_mouse(visible) end\n"},
    {"get_mouse_vector", "---@return LimerenceVec2\nfunction rgfw.get_mouse_vector() end\n"},
    {"hold_mouse", "function rgfw.hold_mouse() end\n"},
    {"unhold_mouse", "function rgfw.unhold_mouse() end\n"},
    {"is_key_pressed", "---@param key integer\n---@return boolean\nfunction rgfw.is_key_pressed(key) end\n"},
    {"is_key_released", "---@param key integer\n---@return boolean\nfunction rgfw.is_key_released(key) end\n"},
    {"is_key_down", "---@param key integer\n---@return boolean\nfunction rgfw.is_key_down(key) end\n"},
    {"is_mouse_pressed", "---@param button integer\n---@return boolean\nfunction rgfw.is_mouse_pressed(button) end\n"},
    {"is_mouse_released", "---@param button integer\n---@return boolean\nfunction rgfw.is_mouse_released(button) end\n"},
    {"is_mouse_down", "---@param button integer\n---@return boolean\nfunction rgfw.is_mouse_down(button) end\n"},
};

static const Lua_Stub_Function_Spec lua_stub_graphics_functions[] = {
    {"rgba", "---@param r integer\n---@param g integer\n---@param b integer\n---@param a? integer\n---@return integer\nfunction graphics.rgba(r, g, b, a) end\n"},
    {"get_width", "---@return integer\nfunction graphics.get_width() end\n"},
    {"get_height", "---@return integer\nfunction graphics.get_height() end\n"},
    {"clear", "---@param color integer\nfunction graphics.clear(color) end\n"},
    {"rect", "---@param x integer\n---@param y integer\n---@param w integer\n---@param h integer\n---@param color integer\nfunction graphics.rect(x, y, w, h, color) end\n"},
    {"frame", "---@param x integer\n---@param y integer\n---@param w integer\n---@param h integer\n---@param color integer\nfunction graphics.frame(x, y, w, h, color) end\n"},
    {"circle", "---@param x integer\n---@param y integer\n---@param r integer\n---@param color integer\nfunction graphics.circle(x, y, r, color) end\n"},
    {"line", "---@param x1 integer\n---@param y1 integer\n---@param x2 integer\n---@param y2 integer\n---@param color integer\nfunction graphics.line(x1, y1, x2, y2, color) end\n"},
    {"triangle", "---@param x1 integer\n---@param y1 integer\n---@param x2 integer\n---@param y2 integer\n---@param x3 integer\n---@param y3 integer\n---@param color integer\nfunction graphics.triangle(x1, y1, x2, y2, x3, y3, color) end\n"},
    {"set_pixel", "---@param x integer\n---@param y integer\n---@param color integer\nfunction graphics.set_pixel(x, y, color) end\n"},
};

static const Lua_Stub_Function_Spec lua_stub_core_functions[] = {
    {"begin_frame", "---@param clear_color integer\n---@param clear_depth? number\nfunction core.begin_frame(clear_color, clear_depth) end\n"},
    {"draw_text", "---@param font string\n---@param text string\n---@param x integer\n---@param y integer\n---@param scale integer\n---@param color integer\nfunction core.draw_text(font, text, x, y, scale, color) end\n"},
    {"draw_model", "---@param model string\n---@param model_matrix LimerenceMat4\n---@param view_matrix LimerenceMat4\n---@param projection_matrix LimerenceMat4\n---@param color integer\n---@param options? LimerenceDrawOptions\nfunction core.draw_model(model, model_matrix, view_matrix, projection_matrix, color, options) end\n"},
    {"camera", "---@param position_or_x LimerenceVec3|table|number\n---@param y_or_pitch? number\n---@param z_or_yaw? number\n---@param pitch? number\n---@param yaw? number\n---@return LimerenceCamera\nfunction core.camera(position_or_x, y_or_pitch, z_or_yaw, pitch, yaw) end\n"},
};

static const Lua_Stub_Function_Spec lua_stub_hmm_functions[] = {
    {"vec2", "---@param x number\n---@param y number\n---@return LimerenceVec2\nfunction hmm.vec2(x, y) end\n"},
    {"vec3", "---@param x number\n---@param y number\n---@param z number\n---@return LimerenceVec3\nfunction hmm.vec3(x, y, z) end\n"},
    {"vec4", "---@param x number\n---@param y number\n---@param z number\n---@param w number\n---@return LimerenceVec4\nfunction hmm.vec4(x, y, z, w) end\n"},
    {"add3", "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.add3(a, b) end\n"},
    {"sub3", "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.sub3(a, b) end\n"},
    {"mul3f", "---@param value LimerenceVec3\n---@param scalar number\n---@return LimerenceVec3\nfunction hmm.mul3f(value, scalar) end\n"},
    {"dot3", "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return number\nfunction hmm.dot3(a, b) end\n"},
    {"cross", "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.cross(a, b) end\n"},
    {"len3", "---@param value LimerenceVec3\n---@return number\nfunction hmm.len3(value) end\n"},
    {"norm3", "---@param value LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.norm3(value) end\n"},
    {"lerp3", "---@param a LimerenceVec3\n---@param t number\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.lerp3(a, t, b) end\n"},
    {"sin", "---@param value number\n---@return number\nfunction hmm.sin(value) end\n"},
    {"cos", "---@param value number\n---@return number\nfunction hmm.cos(value) end\n"},
    {"clamp", "---@param min number\n---@param value number\n---@param max number\n---@return number\nfunction hmm.clamp(min, value, max) end\n"},
    {"identity4", "---@return LimerenceMat4\nfunction hmm.identity4() end\n"},
    {"translate", "---@param value LimerenceVec3\n---@return LimerenceMat4\nfunction hmm.translate(value) end\n"},
    {"scale", "---@param value LimerenceVec3\n---@return LimerenceMat4\nfunction hmm.scale(value) end\n"},
    {"rotate_rh", "---@param degrees number\n---@param axis LimerenceVec3\n---@return LimerenceMat4\nfunction hmm.rotate_rh(degrees, axis) end\n"},
    {"look_at_rh", "---@param eye LimerenceVec3\n---@param center LimerenceVec3\n---@param up LimerenceVec3\n---@return LimerenceMat4\nfunction hmm.look_at_rh(eye, center, up) end\n"},
    {"perspective_rh_no", "---@param fov_degrees number\n---@param aspect number\n---@param near_plane number\n---@param far_plane number\n---@return LimerenceMat4\nfunction hmm.perspective_rh_no(fov_degrees, aspect, near_plane, far_plane) end\n"},
    {"mul_m4", "---@param a LimerenceMat4\n---@param b LimerenceMat4\n---@return LimerenceMat4\nfunction hmm.mul_m4(a, b) end\n"},
    {"transform_point", "---@param matrix LimerenceMat4\n---@param point LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.transform_point(matrix, point) end\n"},
    {"transform_vector", "---@param matrix LimerenceMat4\n---@param vector LimerenceVec3\n---@return LimerenceVec3\nfunction hmm.transform_vector(matrix, vector) end\n"},
};

static const Lua_Stub_Function_Spec lua_stub_audio_functions[] = {
    {"init", "---@return boolean ok, string? err\nfunction audio.init() end\n"},
    {"shutdown", "function audio.shutdown() end\n"},
    {"is_ready", "---@return boolean\nfunction audio.is_ready() end\n"},
    {"set_master_volume", "---@param volume number\nfunction audio.set_master_volume(volume) end\n"},
    {"play", "---@param path string\n---@return boolean ok, string? err\nfunction audio.play(path) end\n"},
    {"load_sound", "---@param path string\n---@return integer? handle, string? err\nfunction audio.load_sound(path) end\n"},
    {"unload_sound", "---@param handle integer\nfunction audio.unload_sound(handle) end\n"},
    {"start", "---@param handle integer\n---@return boolean ok, string? err\nfunction audio.start(handle) end\n"},
    {"stop", "---@param handle integer\n---@return boolean ok, string? err\nfunction audio.stop(handle) end\n"},
    {"set_volume", "---@param handle integer\n---@param volume number\nfunction audio.set_volume(handle, volume) end\n"},
    {"set_pitch", "---@param handle integer\n---@param pitch number\nfunction audio.set_pitch(handle, pitch) end\n"},
    {"set_pan", "---@param handle integer\n---@param pan number\nfunction audio.set_pan(handle, pan) end\n"},
    {"set_looping", "---@param handle integer\n---@param looping boolean\nfunction audio.set_looping(handle, looping) end\n"},
    {"is_playing", "---@param handle integer\n---@return boolean\nfunction audio.is_playing(handle) end\n"},
    {"at_end", "---@param handle integer\n---@return boolean\nfunction audio.at_end(handle) end\n"},
    {"seek", "---@param handle integer\n---@param seconds number\n---@return boolean ok, string? err\nfunction audio.seek(handle, seconds) end\n"},
};

static const Lua_Stub_Method_Spec lua_stub_camera_methods[] = {
    {"get_position", "---@return LimerenceVec3\nfunction LimerenceCamera:get_position() end\n"},
    {"set_position", "---@param position LimerenceVec3\nfunction LimerenceCamera:set_position(position) end\n"},
    {"get_pitch", "---@return number\nfunction LimerenceCamera:get_pitch() end\n"},
    {"set_pitch", "---@param pitch number\nfunction LimerenceCamera:set_pitch(pitch) end\n"},
    {"get_yaw", "---@return number\nfunction LimerenceCamera:get_yaw() end\n"},
    {"set_yaw", "---@param yaw number\nfunction LimerenceCamera:set_yaw(yaw) end\n"},
    {"look", "---@param yaw_delta number\n---@param pitch_delta number\n---@param pitch_min? number\n---@param pitch_max? number\nfunction LimerenceCamera:look(yaw_delta, pitch_delta, pitch_min, pitch_max) end\n"},
    {"move", "---@param forward_distance number\n---@param strafe_distance number\n---@param vertical_distance number\nfunction LimerenceCamera:move(forward_distance, strafe_distance, vertical_distance) end\n"},
    {"forward", "---@return LimerenceVec3\nfunction LimerenceCamera:forward() end\n"},
    {"flat_forward", "---@return LimerenceVec3\nfunction LimerenceCamera:flat_forward() end\n"},
    {"right", "---@return LimerenceVec3\nfunction LimerenceCamera:right() end\n"},
    {"view", "---@return LimerenceMat4\nfunction LimerenceCamera:view() end\n"},
};

static const Lua_Stub_Module_Spec lua_stub_modules[] = {
    {"rgfw", "rgfw", lua_stub_rgfw_functions, NOB_ARRAY_LEN(lua_stub_rgfw_functions)},
    {"graphics", "graphics", lua_stub_graphics_functions, NOB_ARRAY_LEN(lua_stub_graphics_functions)},
    {"core", "core", lua_stub_core_functions, NOB_ARRAY_LEN(lua_stub_core_functions)},
    {"hmm", "hmm", lua_stub_hmm_functions, NOB_ARRAY_LEN(lua_stub_hmm_functions)},
    {"audio", "audio", lua_stub_audio_functions, NOB_ARRAY_LEN(lua_stub_audio_functions)},
};

static void log_assets_error(const Assets_Error *error)
{
    if (error->source_path[0] != '\0' && error->line > 0) {
        nob_log(ERROR, "%s:%zu: %s", error->source_path, error->line, error->message);
    } else if (error->source_path[0] != '\0') {
        nob_log(ERROR, "%s: %s", error->source_path, error->message);
    } else {
        nob_log(ERROR, "%s", error->message);
    }
}

static char *copy_string(const char *src)
{
    size_t count;
    char *result;

    if (src == NULL) return NULL;

    count = strlen(src);
    result = (char *)malloc(count + 1);
    if (result == NULL) return NULL;

    memcpy(result, src, count + 1);
    return result;
}

static bool is_path_sep(char ch)
{
    return ch == '/' || ch == '\\';
}

static void normalize_slashes(char *path)
{
    if (path == NULL) return;
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (path[i] == '\\') path[i] = '/';
    }
}

static bool ensure_directory_tree(const char *path)
{
    char *buffer;
    size_t length;
    size_t start = 0;

    if (path == NULL || path[0] == '\0') return true;

    buffer = copy_string(path);
    if (buffer == NULL) return false;

    normalize_slashes(buffer);
    length = strlen(buffer);
    while (length > 0 && is_path_sep(buffer[length - 1])) {
        buffer[length - 1] = '\0';
        length -= 1;
    }

    if (length >= 2 && buffer[1] == ':') {
        start = 2;
    }
    if (is_path_sep(buffer[start])) {
        start += 1;
    }

    for (size_t i = start; i < length; ++i) {
        if (!is_path_sep(buffer[i])) continue;
        buffer[i] = '\0';
        if (buffer[0] != '\0') {
            if (!mkdir_if_not_exists(buffer)) {
                free(buffer);
                return false;
            }
        }
        buffer[i] = '/';
    }

    if (buffer[0] != '\0') {
        if (!mkdir_if_not_exists(buffer)) {
            free(buffer);
            return false;
        }
    }

    free(buffer);
    return true;
}

static bool write_text_file(const char *path, const char *contents)
{
    return nob_write_entire_file(path, contents, strlen(contents));
}

static bool write_new_file(const char *path, const char *contents)
{
    if (file_exists(path)) {
        fprintf(stderr, "refusing to overwrite existing file: %s\n", path);
        return false;
    }

    if (!ensure_directory_tree(temp_dir_name(path))) {
        return false;
    }

    if (!write_text_file(path, contents)) {
        fprintf(stderr, "failed to write file: %s\n", path);
        return false;
    }

    return true;
}

static bool path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    if (is_path_sep(path[0])) return true;
    if (path[0] != '\0' && path[1] == ':') return true;
    return false;
}

static const char *path_join(const char *lhs, const char *rhs)
{
    size_t lhs_len;

    if (lhs == NULL || lhs[0] == '\0') return temp_strdup(rhs);
    if (rhs == NULL || rhs[0] == '\0') return temp_strdup(lhs);

    lhs_len = strlen(lhs);
    if (lhs_len > 0 && is_path_sep(lhs[lhs_len - 1])) {
        return temp_sprintf("%s%s", lhs, rhs);
    }

    return temp_sprintf("%s/%s", lhs, rhs);
}

static const char *resolve_path(const char *cwd, const char *path)
{
    if (path_is_absolute(path)) return temp_strdup(path);
    return path_join(cwd, path);
}

static Assets_Runtime_Type asset_type_from_path(const char *path)
{
    const char *ext = temp_file_ext(path);

    if (ext == NULL) return 0;
    if (strcmp(ext, ".obj") == 0) return ASSETS_RUNTIME_TYPE_MODEL;
    if (strcmp(ext, ".png") == 0) return ASSETS_RUNTIME_TYPE_IMAGE;
    if (strcmp(ext, ".jpg") == 0) return ASSETS_RUNTIME_TYPE_IMAGE;
    if (strcmp(ext, ".jpeg") == 0) return ASSETS_RUNTIME_TYPE_IMAGE;
    if (strcmp(ext, ".ttf") == 0) return ASSETS_RUNTIME_TYPE_FONT;
    return 0;
}

static void append_lua_stub_generic_function(Nob_String_Builder *sb, const char *module_name, const char *name)
{
    nob_sb_appendf(sb, "function %s.%s(...) end\n", module_name, name);
}

static void append_lua_stub_generic_method(Nob_String_Builder *sb, const char *class_name, const char *name)
{
    nob_sb_appendf(sb, "function %s:%s(...) end\n", class_name, name);
}

static void append_lua_stub_module(Nob_String_Builder *sb, const Lua_Stub_Module_Spec *module)
{
    nob_sb_appendf(sb, "---@class %s\n", module->class_name);
    if (strcmp(module->module_name, "rgfw") == 0) {
        for (size_t i = 0; i < NOB_ARRAY_LEN(lua_stub_rgfw_constants); ++i) {
            nob_sb_appendf(sb, "---@field %s integer\n", lua_stub_rgfw_constants[i]);
        }
    }
    nob_sb_appendf(sb, "%s = {}\n", module->module_name);
    nob_sb_append_cstr(sb, "\n");

    for (size_t i = 0; i < module->function_count; ++i) {
        const Lua_Stub_Function_Spec *function = &module->functions[i];

        if (function->stub != NULL) {
            nob_sb_append_cstr(sb, function->stub);
        } else {
            append_lua_stub_generic_function(sb, module->module_name, function->name);
        }
    }

    nob_sb_append_cstr(sb, "\n");
}

static void append_lua_stub_camera_methods(Nob_String_Builder *sb)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(lua_stub_camera_methods); ++i) {
        const Lua_Stub_Method_Spec *method = &lua_stub_camera_methods[i];

        if (method->stub != NULL) {
            nob_sb_append_cstr(sb, method->stub);
        } else {
            append_lua_stub_generic_method(sb, "LimerenceCamera", method->name);
        }
    }

    nob_sb_append_cstr(sb, "\n");
}

static bool generate_lua_lsp_files(const char *project_root)
{
    bool result = true;
    Nob_String_Builder stub = {0};
    char *meta_root = copy_string(path_join(project_root, ".limerence/meta"));
    char *stub_path = copy_string(path_join(project_root, ".limerence/meta/limerence3d.lua"));
    char *luarc_path = copy_string(path_join(project_root, ".luarc.json"));

    if (meta_root == NULL || stub_path == NULL || luarc_path == NULL) {
        nob_return_defer(false);
    }
    if (!ensure_directory_tree(project_root)) {
        nob_return_defer(false);
    }
    if (!ensure_directory_tree(meta_root)) {
        nob_return_defer(false);
    }

    nob_sb_append_cstr(&stub, lua_stub_prelude);
    append_lua_stub_camera_methods(&stub);
    for (size_t i = 0; i < NOB_ARRAY_LEN(lua_stub_modules); ++i) {
        append_lua_stub_module(&stub, &lua_stub_modules[i]);
    }
    nob_sb_append_null(&stub);

    if (!write_text_file(stub_path, stub.items)) {
        nob_return_defer(false);
    }
    if (!file_exists(luarc_path) && !write_text_file(luarc_path, project_luarc_json)) {
        nob_return_defer(false);
    }
    nob_log(INFO, "Generated Lua stub %s", stub_path);

defer:
    free(stub.items);
    free(meta_root);
    free(stub_path);
    free(luarc_path);
    return result;
}

static bool create_new_project(const char *repo_root, const char *project_root)
{
    char *assets_root = copy_string(path_join(project_root, "assets"));
    char *main_lua_path = copy_string(path_join(project_root, "main.lua"));
    bool result = true;

    if (assets_root == NULL || main_lua_path == NULL) {
        nob_return_defer(false);
    }
    if (!ensure_directory_tree(project_root)) {
        nob_return_defer(false);
    }
    if (!mkdir_if_not_exists(assets_root)) {
        nob_return_defer(false);
    }
    if (!write_new_file(main_lua_path, starter_main_lua)) {
        nob_return_defer(false);
    }
    if (!generate_lua_lsp_files(project_root)) {
        nob_return_defer(false);
    }

    nob_log(INFO, "Created project %s", project_root);

defer:
    (void)repo_root;
    free(assets_root);
    free(main_lua_path);
    return result;
}

static void print_usage(FILE *stream, const char *program_name)
{
    fprintf(stream, "usage:\n");
    fprintf(stream, "  %s <project-dir>\n", program_name);
    fprintf(stream, "  %s new <project-dir>\n", program_name);
    fprintf(stream, "  %s lsp gen <project-dir>\n", program_name);
}

static void append_pack_dependencies(Nob_File_Paths *inputs, const char *repo_root)
{
    da_append(inputs, path_join(repo_root, "assets.h"));
    da_append(inputs, path_join(repo_root, "assets_runtime.h"));
    da_append(inputs, path_join(repo_root, "assets_runtime.c"));
    da_append(inputs, path_join(repo_root, "third_party/nob.h"));
    da_append(inputs, path_join(repo_root, "third_party/stb_image.h"));
    da_append(inputs, path_join(repo_root, "third_party/stb_truetype.h"));
}

static bool collect_pack_input(Walk_Entry entry)
{
    Pack_Input_Context *context = entry.data;

    if (entry.type == NOB_FILE_DIRECTORY) {
        if (strcmp(path_name(entry.path), "generated") == 0) {
            *entry.action = NOB_WALK_SKIP;
        }
        return true;
    }

    if (entry.type != NOB_FILE_REGULAR) {
        return true;
    }

    if (asset_type_from_path(entry.path) == 0) {
        return true;
    }

    da_append(context->inputs, temp_strdup(entry.path));
    (void)context->assets_root;
    return true;
}

static bool build_project_pack_if_needed(const char *project_root, const char *repo_root, const char **pack_path_out)
{
    bool result = true;
    Nob_File_Paths inputs = {0};
    char *assets_root = copy_string(path_join(project_root, "assets"));
    char *generated_root = copy_string(path_join(project_root, "generated"));
    char *pack_path = copy_string(path_join(path_join(project_root, "generated"), "assets.pack"));
    Pack_Input_Context context = {
        .assets_root = assets_root,
        .inputs = &inputs,
    };

    *pack_path_out = NULL;

    if (assets_root == NULL || generated_root == NULL || pack_path == NULL) {
        nob_return_defer(false);
    }

    if (!file_exists(assets_root)) {
        nob_return_defer(true);
    }
    if (!mkdir_if_not_exists(generated_root)) {
        nob_return_defer(false);
    }

    append_pack_dependencies(&inputs, repo_root);
    if (!walk_dir(assets_root, collect_pack_input, .data = &context)) {
        nob_return_defer(false);
    }

    {
        int rebuild = needs_rebuild(pack_path, inputs.items, inputs.count);
        if (rebuild < 0) nob_return_defer(false);
        if (rebuild > 0) {
            Assets_Error error = {0};

            nob_log(INFO, "Building asset pack %s", pack_path);
            if (assets_build_pack_from_dir(assets_root, pack_path, &error) != ASSETS_STATUS_OK) {
                log_assets_error(&error);
                nob_return_defer(false);
            }
        }
    }

    *pack_path_out = copy_string(pack_path);

defer:
    da_free(inputs);
    free(assets_root);
    free(generated_root);
    free(pack_path);
    return result;
}

static bool run_project(const char *repo_root, const char *project_root)
{
    bool result = false;
    char *bootstrap_path = copy_string(path_join(repo_root, "lua_api.lua"));
    const char *window_title = path_name(project_root);
    const char *pack_path = NULL;
    RGFW_window *win = NULL;
    u8 *pixels = NULL;
    float *zbuffer = NULL;
    RGFW_surface *surface = NULL;
    Olivec_Canvas canvas = {0};
    Assets_Runtime_Registry assets = {0};
    Lua_API_Context lua_context = {0};
    uint64_t last_frame = 0;

    if (!build_project_pack_if_needed(project_root, repo_root, &pack_path)) {
        return false;
    }

    if (pack_path != NULL) {
        Assets_Error error = {0};

        if (assets_runtime_load_pack(pack_path, &assets, &error) != ASSETS_STATUS_OK) {
            log_assets_error(&error);
            goto defer;
        }
    }

    if (!set_current_dir(project_root)) {
        goto defer;
    }

    win = RGFW_createWindow(
        window_title[0] != '\0' ? window_title : "Limerence3D",
        100,
        100,
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        RGFW_windowCenter | RGFW_windowNoResize
    );
    if (win == NULL) {
        fputs("failed to create window\n", stderr);
        goto defer;
    }

    pixels = (u8 *)RGFW_alloc(win->w * win->h * 4);
    zbuffer = (float *)malloc(win->w * win->h * sizeof(float));
    if (pixels == NULL || zbuffer == NULL) {
        fputs("failed to allocate frame buffers\n", stderr);
        goto defer;
    }

    surface = RGFW_window_createSurface(win, pixels, win->w, win->h, RGFW_formatRGBA8);
    if (surface == NULL) {
        fputs("failed to create window surface\n", stderr);
        goto defer;
    }

    canvas = core_make_canvas((uint32_t *)pixels, win->w, win->h, win->w);
    core_begin_frame(canvas, zbuffer, DEFAULT_BACKGROUND_COLOR, 1.0f);

    lua_context.window = win;
    lua_context.canvas = canvas;
    lua_context.zbuffer = zbuffer;
    lua_context.assets = &assets;

    RGFW_window_setExitKey(win, RGFW_escape);

    if (!lua_api_init(&lua_context)) {
        goto defer;
    }
    if (file_exists(bootstrap_path) && !lua_api_run_file(bootstrap_path)) {
        goto defer;
    }
    if (!lua_api_run_file("main.lua")) {
        goto defer;
    }
    if (!lua_api_call_global0("init")) {
        goto defer;
    }

    last_frame = nanos_since_unspecified_epoch();
    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;
        uint64_t now;
        float dt;

        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit) {
                break;
            }
        }

        now = nanos_since_unspecified_epoch();
        dt = (float)(now - last_frame) / (float)NANOS_PER_SEC;
        last_frame = now;

        if (!lua_api_call_global1_number("update", dt)) {
            goto defer;
        }
        if (!lua_api_call_global0("draw")) {
            goto defer;
        }

        RGFW_window_blitSurface(win, surface);
    }

    if (!lua_api_call_global0("quit")) {
        goto defer;
    }

    result = true;

defer:
    lua_api_shutdown();
    assets_runtime_unload(&assets);
    free((void *)pack_path);
    free(bootstrap_path);
    if (surface != NULL) RGFW_surface_free(surface);
    if (pixels != NULL) RGFW_free(pixels);
    free(zbuffer);
    if (win != NULL) RGFW_window_close(win);
    return result;
}

int main(int argc, char **argv)
{
    char *repo_root = NULL;
    char *project_root = NULL;
    char *project_main_lua = NULL;
    int exit_code = 1;

    if (argc < 2) {
        print_usage(stderr, argc > 0 ? argv[0] : "main");
        return 1;
    }

    repo_root = copy_string(get_current_dir_temp());
    if (repo_root == NULL) {
        goto defer;
    }

    if (strcmp(argv[1], "lsp") == 0) {
        if (argc != 4 || strcmp(argv[2], "gen") != 0) {
            print_usage(stderr, argv[0]);
            goto defer;
        }

        project_root = copy_string(resolve_path(repo_root, argv[3]));
        if (project_root == NULL) {
            goto defer;
        }
        if (!generate_lua_lsp_files(project_root)) {
            goto defer;
        }

        exit_code = 0;
        goto defer;
    }

    if (strcmp(argv[1], "new") == 0) {
        if (argc != 3) {
            print_usage(stderr, argv[0]);
            goto defer;
        }

        project_root = copy_string(resolve_path(repo_root, argv[2]));
        if (project_root == NULL) {
            goto defer;
        }

        if (!create_new_project(repo_root, project_root)) {
            goto defer;
        }

        exit_code = 0;
        goto defer;
    }

    if (argc != 2) {
        print_usage(stderr, argv[0]);
        goto defer;
    }

    project_root = copy_string(resolve_path(repo_root, argv[1]));
    if (project_root == NULL) {
        goto defer;
    }

    project_main_lua = copy_string(path_join(project_root, "main.lua"));
    if (project_main_lua == NULL) {
        goto defer;
    }

    if (!file_exists(project_main_lua)) {
        fprintf(stderr, "missing Lua entry point: %s\n", project_main_lua);
        goto defer;
    }

    if (!run_project(repo_root, project_root)) {
        goto defer;
    }

    exit_code = 0;

defer:
    free(project_main_lua);
    free(project_root);
    free(repo_root);
    return exit_code;
}
