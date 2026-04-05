#include "project_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGFW_IMPLEMENTATION
#include "third_party/RGFW.h"

#ifdef ERROR
#undef ERROR
#endif

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
#define EXPORT_PACKAGE_PATH "./?.luac;./?/init.luac"

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
} Project_Window_Config;

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

static Assets_Runtime_Type asset_type_from_path(const char *path)
{
    const char *ext = temp_file_ext(path);

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
    lua_pop(L, 1);
}

static bool lua_set_package_path(lua_State *L, const char *package_path)
{
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        fputs("lua package table is unavailable\n", stderr);
        return false;
    }

    lua_pushstring(L, package_path);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
    return true;
}

static bool load_project_window_config_from_script(const char *script_path, bool export_mode, Project_Window_Config *config)
{
    bool result = true;
    lua_State *L = NULL;

    if (script_path == NULL || !file_exists(script_path)) {
        return true;
    }

    L = luaL_newstate();
    if (L == NULL) {
        fprintf(stderr, "failed to create Lua state for %s\n", script_path);
        nob_return_defer(false);
    }

    luaL_openlibs(L);
    if (export_mode && !lua_set_package_path(L, EXPORT_PACKAGE_PATH)) {
        nob_return_defer(false);
    }
    if (luaL_loadfilex(L, script_path, NULL) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", script_path, lua_tostring(L, -1));
        nob_return_defer(false);
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", script_path, lua_tostring(L, -1));
        nob_return_defer(false);
    }
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "%s: config script must return a table\n", script_path);
        nob_return_defer(false);
    }

    project_window_config_apply_table(L, -1, config);

defer:
    if (L != NULL) lua_close(L);
    return result;
}

static bool project_window_config_is_borderless_fullscreen(const Project_Window_Config *config)
{
    return config->borderless && config->fullscreen;
}

static void project_window_apply_borderless_fullscreen(RGFW_window *win)
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

#ifdef _WIN32
    x = 0;
    y = 0;
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
#else
    RGFW_monitor monitor = RGFW_getPrimaryMonitor();

    x = monitor.x;
    y = monitor.y;
    width = monitor.mode.w;
    height = monitor.mode.h;
#endif

    RGFW_window_move(win, x, y);
    RGFW_window_resize(win, width, height);
}

static RGFW_windowFlags project_window_config_flags(const Project_Window_Config *config)
{
    RGFW_windowFlags flags = 0;

    if (!config->resizable) flags |= RGFW_windowNoResize;
    if (project_window_config_is_borderless_fullscreen(config)) {
        flags |= RGFW_windowNoBorder;
        return flags;
    }

    if (config->borderless) flags |= RGFW_windowNoBorder;
    if (config->fullscreen) flags |= RGFW_windowFullscreen;
    if (config->centered) flags |= RGFW_windowCenter;

    return flags;
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

bool project_runtime_build_pack_if_needed(const char *project_root, const char **pack_path_out)
{
    bool result = true;
    Nob_File_Paths inputs = {0};
    char *assets_root = copy_string(path_join(project_root, "assets"));
    char *generated_root = copy_string(path_join(project_root, "generated"));
    char *pack_path = copy_string(path_join(project_root, "generated/assets.pack"));
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

static bool run_project_internal(
    const char *cwd_root,
    const char *bootstrap_path,
    const char *conf_script_path,
    const char *main_script_path,
    const char *pack_path,
    const char *default_title,
    bool export_mode
)
{
    bool result = false;
    RGFW_window *win = NULL;
    u8 *pixels = NULL;
    float *zbuffer = NULL;
    RGFW_surface *surface = NULL;
    Olivec_Canvas canvas = {0};
    Assets_Runtime_Registry assets = {0};
    Lua_API_Context lua_context = {0};
    uint64_t last_frame = 0;
    Project_Window_Config window_config = {0};

    project_window_config_init(&window_config, default_title);
    if (!set_current_dir(cwd_root)) {
        goto defer;
    }
    if (!load_project_window_config_from_script(conf_script_path, export_mode, &window_config)) {
        goto defer;
    }

    if (pack_path != NULL) {
        Assets_Error error = {0};

        if (assets_runtime_load_pack(pack_path, &assets, &error) != ASSETS_STATUS_OK) {
            log_assets_error(&error);
            goto defer;
        }
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
    if (project_window_config_is_borderless_fullscreen(&window_config)) {
        project_window_apply_borderless_fullscreen(win);
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
    if (export_mode && !lua_api_set_package_path(EXPORT_PACKAGE_PATH)) {
        goto defer;
    }
    if (bootstrap_path != NULL && file_exists(bootstrap_path) && !lua_api_run_file(bootstrap_path)) {
        goto defer;
    }
    if (!lua_api_run_file(main_script_path)) {
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
    project_window_config_free(&window_config);
    if (surface != NULL) RGFW_surface_free(surface);
    if (pixels != NULL) RGFW_free(pixels);
    free(zbuffer);
    if (win != NULL) RGFW_window_close(win);
    return result;
}

bool project_runtime_run_dev(const char *repo_root, const char *project_root)
{
    bool result = false;
    const char *pack_path = NULL;
    char *bootstrap_path = NULL;
    char *conf_script_path = NULL;
    char *main_script_path = NULL;

    if (!project_runtime_build_pack_if_needed(project_root, &pack_path)) {
        goto defer;
    }

    if (repo_root != NULL) {
        bootstrap_path = copy_string(path_join(repo_root, "lua_api.lua"));
    }
    conf_script_path = copy_string(path_join(project_root, "conf.lua"));
    main_script_path = copy_string(path_join(project_root, "main.lua"));
    if (conf_script_path == NULL || main_script_path == NULL) {
        goto defer;
    }

    result = run_project_internal(
        project_root,
        bootstrap_path,
        conf_script_path,
        main_script_path,
        pack_path,
        path_name(project_root),
        false
    );

defer:
    free((void *)pack_path);
    free(bootstrap_path);
    free(conf_script_path);
    free(main_script_path);
    return result;
}

bool project_runtime_run_export(const char *bundle_root)
{
    char *pack_path = copy_string(path_join(bundle_root, "assets.pack"));
    char *conf_script_path = copy_string(path_join(bundle_root, "conf.luac"));
    char *main_script_path = copy_string(path_join(bundle_root, "main.luac"));
    bool result = false;

    if (pack_path == NULL || conf_script_path == NULL || main_script_path == NULL) {
        goto defer;
    }

    if (!file_exists(pack_path)) {
        fprintf(stderr, "missing exported asset pack: %s\n", pack_path);
        goto defer;
    }
    if (!file_exists(main_script_path)) {
        fprintf(stderr, "missing exported Lua entry point: %s\n", main_script_path);
        goto defer;
    }

    result = run_project_internal(
        bundle_root,
        NULL,
        conf_script_path,
        main_script_path,
        pack_path,
        path_name(bundle_root),
        true
    );

defer:
    free(pack_path);
    free(conf_script_path);
    free(main_script_path);
    return result;
}
