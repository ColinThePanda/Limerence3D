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
#include "third_party/lua-5.5.0/src/lauxlib.h"
#include "third_party/lua-5.5.0/src/lualib.h"

#define DEFAULT_WINDOW_X 100
#define DEFAULT_WINDOW_Y 100
#define DEFAULT_WINDOW_WIDTH 980
#define DEFAULT_WINDOW_HEIGHT 540
#define DEFAULT_BACKGROUND_COLOR 0xFF181818

typedef struct {
    const char *assets_root;
    Nob_File_Paths *inputs;
} Pack_Input_Context;

typedef struct {
    char *title;
    int x;
    int y;
    int width;
    int height;
    bool centered;
    bool resizable;
    bool borderless;
    bool fullscreen;
    bool hidden;
    bool focus_on_show;
} Project_Window_Config;

static const char *starter_main_lua =
    "local bg = graphics.rgba(24, 24, 24, 255)\n"
    "\n"
    "function update(dt)\n"
    "    if window.is_key_pressed(window.key_escape) then\n"
    "        window.close()\n"
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

static void append_lua_string_literal(Nob_String_Builder *sb, const char *value)
{
    nob_sb_append_cstr(sb, "\"");
    for (size_t i = 0; value != NULL && value[i] != '\0'; ++i) {
        char ch = value[i];

        switch (ch) {
        case '\\':
            nob_sb_append_cstr(sb, "\\\\");
            break;
        case '"':
            nob_sb_append_cstr(sb, "\\\"");
            break;
        case '\n':
            nob_sb_append_cstr(sb, "\\n");
            break;
        case '\r':
            nob_sb_append_cstr(sb, "\\r");
            break;
        case '\t':
            nob_sb_append_cstr(sb, "\\t");
            break;
        default:
            nob_sb_append_buf(sb, &ch, 1);
            break;
        }
    }
    nob_sb_append_cstr(sb, "\"");
}

static bool build_default_project_conf(Nob_String_Builder *sb, const char *title)
{
    nob_sb_append_cstr(sb, "return {\n");
    nob_sb_append_cstr(sb, "  window = {\n");
    nob_sb_append_cstr(sb, "    title = ");
    append_lua_string_literal(sb, title);
    nob_sb_append_cstr(sb, ",\n");
    nob_sb_appendf(sb, "    width = %d,\n", DEFAULT_WINDOW_WIDTH);
    nob_sb_appendf(sb, "    height = %d,\n", DEFAULT_WINDOW_HEIGHT);
    nob_sb_append_cstr(sb, "    resizable = false,\n");
    nob_sb_append_cstr(sb, "    centered = true,\n");
    nob_sb_append_cstr(sb, "  },\n");
    nob_sb_append_cstr(sb, "}\n");
    nob_sb_append_null(sb);
    return true;
}

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

static const char *path_join(const char *lhs, const char *rhs);

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

static void project_window_config_init(Project_Window_Config *config, const char *default_title)
{
    memset(config, 0, sizeof(*config));
    config->title = copy_string(default_title != NULL ? default_title : "Limerence3D");
    config->x = DEFAULT_WINDOW_X;
    config->y = DEFAULT_WINDOW_Y;
    config->width = DEFAULT_WINDOW_WIDTH;
    config->height = DEFAULT_WINDOW_HEIGHT;
    config->centered = true;
}

static void project_window_config_free(Project_Window_Config *config)
{
    free(config->title);
    memset(config, 0, sizeof(*config));
}

static void lua_read_optional_string_field(lua_State *L, int index, const char *field_name, char **dst)
{
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (lua_isstring(L, -1)) {
        char *value = copy_string(lua_tostring(L, -1));

        if (value != NULL) {
            free(*dst);
            *dst = value;
        }
    }
    lua_pop(L, 1);
}

static void lua_read_optional_int_field(lua_State *L, int index, const char *field_name, int *dst)
{
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (lua_isinteger(L, -1)) {
        *dst = (int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
}

static void lua_read_optional_bool_field(lua_State *L, int index, const char *field_name, bool *dst)
{
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (!lua_isnil(L, -1)) {
        *dst = lua_toboolean(L, -1) ? true : false;
    }
    lua_pop(L, 1);
}

static void project_window_config_apply_table(lua_State *L, int index, Project_Window_Config *config)
{
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, "window");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_read_optional_string_field(L, -1, "title", &config->title);
    lua_read_optional_int_field(L, -1, "x", &config->x);
    lua_read_optional_int_field(L, -1, "y", &config->y);
    lua_read_optional_int_field(L, -1, "width", &config->width);
    lua_read_optional_int_field(L, -1, "height", &config->height);
    lua_read_optional_bool_field(L, -1, "centered", &config->centered);
    lua_read_optional_bool_field(L, -1, "resizable", &config->resizable);
    lua_read_optional_bool_field(L, -1, "borderless", &config->borderless);
    lua_read_optional_bool_field(L, -1, "fullscreen", &config->fullscreen);
    lua_read_optional_bool_field(L, -1, "hidden", &config->hidden);
    lua_read_optional_bool_field(L, -1, "focus_on_show", &config->focus_on_show);
    lua_pop(L, 1);
}

static bool load_project_window_config(const char *project_root, Project_Window_Config *config)
{
    bool result = true;
    lua_State *L = NULL;
    char *config_path = copy_string(path_join(project_root, "conf.lua"));

    if (config_path == NULL) {
        return false;
    }
    if (!file_exists(config_path)) {
        goto defer;
    }

    L = luaL_newstate();
    if (L == NULL) {
        fputs("failed to create Lua state for conf.lua\n", stderr);
        nob_return_defer(false);
    }

    luaL_openlibs(L);
    if (luaL_loadfilex(L, config_path, NULL) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", config_path, lua_tostring(L, -1));
        nob_return_defer(false);
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", config_path, lua_tostring(L, -1));
        nob_return_defer(false);
    }
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "%s: conf.lua must return a table\n", config_path);
        nob_return_defer(false);
    }

    project_window_config_apply_table(L, -1, config);

defer:
    if (L != NULL) lua_close(L);
    free(config_path);
    return result;
}

static RGFW_windowFlags project_window_config_flags(const Project_Window_Config *config)
{
    RGFW_windowFlags flags = 0;

    if (!config->resizable) flags |= RGFW_windowNoResize;
    if (config->borderless) flags |= RGFW_windowNoBorder;
    if (config->fullscreen) flags |= RGFW_windowFullscreen;
    if (config->centered) flags |= RGFW_windowCenter;
    if (config->hidden) flags |= RGFW_windowHide;
    if (config->focus_on_show) flags |= RGFW_windowFocusOnShow;

    return flags;
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

static void append_lua_stub_global_function(Nob_String_Builder *sb, const char *name)
{
    nob_sb_appendf(sb, "function %s(...) end\n", name);
}

static void append_lua_stub_generic_method(Nob_String_Builder *sb, const char *class_name, const char *name)
{
    nob_sb_appendf(sb, "function %s:%s(...) end\n", class_name, name);
}

static void append_lua_stub_module(Nob_String_Builder *sb, const Lua_API_Module_Def *module)
{
    nob_sb_appendf(sb, "---@class %s\n", module->class_name);
    for (size_t i = 0; i < module->constant_count; ++i) {
        nob_sb_appendf(sb, "---@field %s integer\n", module->constants[i]);
    }
    nob_sb_appendf(sb, "%s = {}\n", module->module_name);
    nob_sb_append_cstr(sb, "\n");

    for (size_t i = 0; i < module->function_count; ++i) {
        if (module->functions[i].stub != NULL) {
            nob_sb_append_cstr(sb, module->functions[i].stub);
        } else {
            append_lua_stub_generic_function(sb, module->module_name, module->functions[i].name);
        }
    }

    nob_sb_append_cstr(sb, "\n");
}

static void append_lua_stub_class_methods(Nob_String_Builder *sb, const Lua_API_Class_Def *class_def)
{
    nob_sb_appendf(sb, "---@class %s\n", class_def->class_name);
    nob_sb_appendf(sb, "local %s = {}\n\n", class_def->class_name);

    for (size_t i = 0; i < class_def->method_count; ++i) {
        if (class_def->methods[i].stub != NULL) {
            nob_sb_append_cstr(sb, class_def->methods[i].stub);
        } else {
            append_lua_stub_generic_method(sb, class_def->class_name, class_def->methods[i].name);
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

    nob_sb_append_cstr(&stub, lua_api_stub_prelude());
    for (size_t i = 0; i < lua_api_global_count(); ++i) {
        const Lua_API_Global_Def *global = lua_api_global_def(i);

        if (global == NULL) continue;
        if (global->stub != NULL) {
            nob_sb_append_cstr(&stub, global->stub);
        } else {
            append_lua_stub_global_function(&stub, global->name);
        }
    }
    nob_sb_append_cstr(&stub, "\n");
    for (size_t i = 0; i < lua_api_class_count(); ++i) {
        const Lua_API_Class_Def *class_def = lua_api_class_def(i);

        if (class_def == NULL) continue;
        append_lua_stub_class_methods(&stub, class_def);
    }
    for (size_t i = 0; i < lua_api_module_count(); ++i) {
        const Lua_API_Module_Def *module = lua_api_module_def(i);

        if (module == NULL) continue;
        append_lua_stub_module(&stub, module);
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
    char *conf_lua_path = copy_string(path_join(project_root, "conf.lua"));
    char *main_lua_path = copy_string(path_join(project_root, "main.lua"));
    Nob_String_Builder conf = {0};
    bool result = true;

    if (assets_root == NULL || conf_lua_path == NULL || main_lua_path == NULL) {
        nob_return_defer(false);
    }
    if (!ensure_directory_tree(project_root)) {
        nob_return_defer(false);
    }
    if (!mkdir_if_not_exists(assets_root)) {
        nob_return_defer(false);
    }
    if (!build_default_project_conf(&conf, path_name(project_root))) {
        nob_return_defer(false);
    }
    if (!write_new_file(conf_lua_path, conf.items)) {
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
    free(conf_lua_path);
    free(main_lua_path);
    free(conf.items);
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
    da_append(inputs, path_join(repo_root, "src/assets.h"));
    da_append(inputs, path_join(repo_root, "src/assets_runtime.h"));
    da_append(inputs, path_join(repo_root, "src/assets_runtime.c"));
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
    const char *pack_path = NULL;
    RGFW_window *win = NULL;
    u8 *pixels = NULL;
    float *zbuffer = NULL;
    RGFW_surface *surface = NULL;
    Olivec_Canvas canvas = {0};
    Assets_Runtime_Registry assets = {0};
    Lua_API_Context lua_context = {0};
    uint64_t last_frame = 0;
    Project_Window_Config window_config = {0};

    project_window_config_init(&window_config, path_name(project_root));
    if (!load_project_window_config(project_root, &window_config)) {
        goto defer;
    }
    if (!build_project_pack_if_needed(project_root, repo_root, &pack_path)) {
        goto defer;
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
        window_config.title != NULL && window_config.title[0] != '\0' ? window_config.title : "Limerence3D",
        window_config.x,
        window_config.y,
        window_config.width,
        window_config.height,
        project_window_config_flags(&window_config)
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
        float mouse_delta_x = 0.0f;
        float mouse_delta_y = 0.0f;

        while (RGFW_window_checkEvent(win, &event)) {
            if (event.type == RGFW_quit) {
                break;
            }

            if (event.type == RGFW_mousePosChanged) {
                mouse_delta_x += event.mouse.vecX;
                mouse_delta_y += event.mouse.vecY;
            }
        }

        now = nanos_since_unspecified_epoch();
        dt = (float)(now - last_frame) / (float)NANOS_PER_SEC;
        last_frame = now;
        lua_api_set_mouse_vector(mouse_delta_x, mouse_delta_y);

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
    project_window_config_free(&window_config);
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
