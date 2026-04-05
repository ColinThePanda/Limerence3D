#include "lua_api.h"

#include <stdio.h>
#include <string.h>

#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"
#include "third_party/RGFW.h"
#include "third_party/lua-5.5.0/src/lauxlib.h"
#include "third_party/lua-5.5.0/src/lua.h"
#include "third_party/lua-5.5.0/src/lualib.h"

#define LUA_API_MAX_SOUNDS 64
#define LUA_API_CAMERA_METATABLE "limerence.camera"
#define LUA_API_ARRAY_COUNT(items) (sizeof(items) / sizeof((items)[0]))

typedef struct {
    bool active;
    bool transient;
    bool has_decoder;
    ma_sound sound;
    ma_decoder decoder;
} Lua_API_Sound;

typedef struct {
    Core_Fly_Camera camera;
} Lua_API_Camera;

typedef struct {
    lua_State *state;
    Lua_API_Context context;
    float mouse_vector_x;
    float mouse_vector_y;
    bool audio_ready;
    ma_engine audio_engine;
    Lua_API_Sound sounds[LUA_API_MAX_SOUNDS];
} Lua_API_State;

static Lua_API_State g_lua_api = {0};

static const char *lua_api_stub_prelude_text =
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
    "\n";

static uint32_t lua_api_pack_rgba(int r, int g, int b, int a)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    if (a < 0) a = 0;
    if (a > 255) a = 255;

    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void lua_api_push_vec2(lua_State *L, HMM_Vec2 value)
{
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, value.X);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.Y);
    lua_setfield(L, -2, "y");
}

static void lua_api_push_vec3(lua_State *L, HMM_Vec3 value)
{
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, value.X);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.Y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, value.Z);
    lua_setfield(L, -2, "z");
}

static void lua_api_push_vec4(lua_State *L, HMM_Vec4 value)
{
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, value.X);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.Y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, value.Z);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, value.W);
    lua_setfield(L, -2, "w");
}

static void lua_api_push_mat4(lua_State *L, HMM_Mat4 value)
{
    lua_createtable(L, 16, 1);

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            lua_pushnumber(L, value.Elements[column][row]);
            lua_seti(L, -2, column * 4 + row + 1);
        }
    }

    lua_pushliteral(L, "mat4");
    lua_setfield(L, -2, "type");
}

static float lua_api_table_number_field(lua_State *L, int index, const char *field_name, int numeric_index, float default_value)
{
    float result = default_value;
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (lua_isnumber(L, -1)) {
        result = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        return result;
    }
    lua_pop(L, 1);

    lua_geti(L, absolute_index, numeric_index);
    if (lua_isnumber(L, -1)) {
        result = (float)lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    return result;
}

static HMM_Vec2 lua_api_check_vec2(lua_State *L, int index)
{
    luaL_checktype(L, index, LUA_TTABLE);
    return HMM_V2(
        lua_api_table_number_field(L, index, "x", 1, 0.0f),
        lua_api_table_number_field(L, index, "y", 2, 0.0f)
    );
}

static HMM_Vec3 lua_api_check_vec3(lua_State *L, int index)
{
    luaL_checktype(L, index, LUA_TTABLE);
    return HMM_V3(
        lua_api_table_number_field(L, index, "x", 1, 0.0f),
        lua_api_table_number_field(L, index, "y", 2, 0.0f),
        lua_api_table_number_field(L, index, "z", 3, 0.0f)
    );
}

static HMM_Vec4 lua_api_check_vec4(lua_State *L, int index)
{
    luaL_checktype(L, index, LUA_TTABLE);
    return HMM_V4(
        lua_api_table_number_field(L, index, "x", 1, 0.0f),
        lua_api_table_number_field(L, index, "y", 2, 0.0f),
        lua_api_table_number_field(L, index, "z", 3, 0.0f),
        lua_api_table_number_field(L, index, "w", 4, 0.0f)
    );
}

static HMM_Mat4 lua_api_check_mat4(lua_State *L, int index)
{
    HMM_Mat4 result = {0};
    int absolute_index = lua_absindex(L, index);

    luaL_checktype(L, index, LUA_TTABLE);

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            lua_geti(L, absolute_index, column * 4 + row + 1);
            result.Elements[column][row] = (float)luaL_checknumber(L, -1);
            lua_pop(L, 1);
        }
    }

    return result;
}

static RGFW_window *lua_api_require_window(lua_State *L)
{
    if (g_lua_api.context.window == NULL) {
        luaL_error(L, "rgfw window is not available");
    }

    return g_lua_api.context.window;
}

static Olivec_Canvas lua_api_canvas(void)
{
    return g_lua_api.context.canvas;
}

static float *lua_api_require_zbuffer(lua_State *L)
{
    if (g_lua_api.context.zbuffer == NULL) {
        luaL_error(L, "zbuffer is not available");
    }

    return g_lua_api.context.zbuffer;
}

static const Assets_Runtime_Registry *lua_api_require_assets(lua_State *L)
{
    if (g_lua_api.context.assets == NULL) {
        luaL_error(L, "asset registry is not available");
    }

    return g_lua_api.context.assets;
}

static Olivec_Canvas lua_api_image_canvas(lua_State *L, const Assets_Image *image)
{
    if (image->channels != 4) {
        luaL_error(L, "image '%s' is not RGBA", image->name != NULL ? image->name : "<unnamed>");
    }
    if (image->stride % 4 != 0) {
        luaL_error(L, "image '%s' has an invalid stride", image->name != NULL ? image->name : "<unnamed>");
    }

    return olivec_canvas((uint32_t *)image->pixels, (size_t)image->width, (size_t)image->height, (size_t)(image->stride / 4));
}

static const Assets_Model *lua_api_find_model(const char *name)
{
    return assets_runtime_find_model(g_lua_api.context.assets, name);
}

static const Assets_Image *lua_api_find_image(const char *name)
{
    return assets_runtime_find_image(g_lua_api.context.assets, name);
}

static const Assets_Font *lua_api_find_font(const char *name)
{
    return assets_runtime_find_font(g_lua_api.context.assets, name);
}

static const Assets_Runtime_Audio *lua_api_find_audio(const char *name)
{
    return assets_runtime_find_audio(g_lua_api.context.assets, name);
}

static const Assets_Model *lua_api_check_model(lua_State *L, int index)
{
    const char *name = luaL_checkstring(L, index);
    const Assets_Model *model;

    lua_api_require_assets(L);
    model = lua_api_find_model(name);

    if (model == NULL) {
        luaL_error(L, "unknown model '%s'", name);
    }

    return model;
}

static const Assets_Image *lua_api_check_image(lua_State *L, int index)
{
    const char *name = luaL_checkstring(L, index);
    const Assets_Image *image;

    lua_api_require_assets(L);
    image = lua_api_find_image(name);

    if (image == NULL) {
        luaL_error(L, "unknown image '%s'", name);
    }

    return image;
}

static const Assets_Font *lua_api_check_font(lua_State *L, int index)
{
    const char *name = luaL_checkstring(L, index);
    const Assets_Font *font;

    lua_api_require_assets(L);
    font = lua_api_find_font(name);

    if (font == NULL) {
        luaL_error(L, "unknown font '%s'", name);
    }

    return font;
}

static Lua_API_Camera *lua_api_check_camera(lua_State *L, int index)
{
    return (Lua_API_Camera *)luaL_checkudata(L, index, LUA_API_CAMERA_METATABLE);
}

static int lua_api_opt_bool_field(lua_State *L, int index, const char *field_name, int default_value)
{
    int result = default_value;
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (!lua_isnil(L, -1)) {
        result = lua_toboolean(L, -1) ? 1 : 0;
    }
    lua_pop(L, 1);

    return result;
}

static float lua_api_opt_number_field(lua_State *L, int index, const char *field_name, float default_value)
{
    float result = default_value;
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (lua_isnumber(L, -1)) {
        result = (float)lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    return result;
}

static int lua_api_opt_int_field(lua_State *L, int index, const char *field_name, int default_value)
{
    int result = default_value;
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (lua_isinteger(L, -1)) {
        result = (int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    return result;
}

static Core_Lighting_Mode lua_api_opt_lighting_mode(lua_State *L, int index, Core_Lighting_Mode default_value)
{
    Core_Lighting_Mode result = default_value;
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, "lighting_mode");
    if (lua_isstring(L, -1)) {
        const char *mode = lua_tostring(L, -1);

        if (strcmp(mode, "none") == 0) result = CORE_LIGHTING_NONE;
        else if (strcmp(mode, "flat") == 0) result = CORE_LIGHTING_FLAT;
        else if (strcmp(mode, "gouraud") == 0) result = CORE_LIGHTING_GOURAUD;
    }
    lua_pop(L, 1);

    return result;
}

static HMM_Vec3 lua_api_opt_vec3_field(lua_State *L, int index, const char *field_name, HMM_Vec3 default_value)
{
    HMM_Vec3 result = default_value;
    int absolute_index = lua_absindex(L, index);

    lua_getfield(L, absolute_index, field_name);
    if (lua_istable(L, -1)) {
        result = lua_api_check_vec3(L, -1);
    }
    lua_pop(L, 1);

    return result;
}

static Core_Mesh_Draw_Options lua_api_check_draw_options(lua_State *L, int index)
{
    Core_Mesh_Draw_Options options = CORE_MESH_DRAW_OPTIONS_DEFAULT;

    if (lua_isnoneornil(L, index)) {
        return options;
    }

    luaL_checktype(L, index, LUA_TTABLE);

    options.near_plane = lua_api_opt_number_field(L, index, "near_plane", options.near_plane);
    options.lighting_enabled = lua_api_opt_bool_field(L, index, "lighting_enabled", options.lighting_enabled);
    options.lighting_mode = lua_api_opt_lighting_mode(L, index, options.lighting_mode);
    options.light_direction_world = lua_api_opt_vec3_field(L, index, "light_direction_world", options.light_direction_world);
    options.ambient_strength = lua_api_opt_number_field(L, index, "ambient_strength", options.ambient_strength);
    options.diffuse_strength = lua_api_opt_number_field(L, index, "diffuse_strength", options.diffuse_strength);
    options.specular_strength = lua_api_opt_number_field(L, index, "specular_strength", options.specular_strength);
    options.shininess = lua_api_opt_number_field(L, index, "shininess", options.shininess);
    options.occlusion_culling = lua_api_opt_bool_field(L, index, "occlusion_culling", options.occlusion_culling);
    options.occlusion_test_step = lua_api_opt_int_field(L, index, "occlusion_test_step", options.occlusion_test_step);
    options.fog_start = lua_api_opt_number_field(L, index, "fog_start", options.fog_start);
    options.fog_end = lua_api_opt_number_field(L, index, "fog_end", options.fog_end);
    options.fog_power = lua_api_opt_number_field(L, index, "fog_power", options.fog_power);
    options.fog_color = (uint32_t)lua_api_opt_int_field(L, index, "fog_color", (int)options.fog_color);
    options.backface_culling = lua_api_opt_bool_field(L, index, "backface_culling", options.backface_culling);

    return options;
}

static Lua_API_Sound *lua_api_get_sound_slot(int handle)
{
    int index = handle - 1;

    if (index < 0 || index >= LUA_API_MAX_SOUNDS) {
        return NULL;
    }

    if (!g_lua_api.sounds[index].active) {
        return NULL;
    }

    return &g_lua_api.sounds[index];
}

static void lua_api_reset_sound_slot(Lua_API_Sound *sound)
{
    if (sound == NULL) return;
    memset(sound, 0, sizeof(*sound));
}

static void lua_api_release_sound_slot(Lua_API_Sound *sound)
{
    if (sound == NULL) return;
    if (sound->active) {
        ma_sound_uninit(&sound->sound);
    }
    if (sound->has_decoder) {
        ma_decoder_uninit(&sound->decoder);
    }
    lua_api_reset_sound_slot(sound);
}

static void lua_api_cleanup_finished_sounds(void)
{
    for (int i = 0; i < LUA_API_MAX_SOUNDS; ++i) {
        Lua_API_Sound *sound = &g_lua_api.sounds[i];

        if (!sound->active || !sound->transient) continue;
        if (ma_sound_at_end(&sound->sound) != MA_TRUE) continue;
        lua_api_release_sound_slot(sound);
    }
}

static int lua_api_alloc_sound_slot(void)
{
    lua_api_cleanup_finished_sounds();

    for (int i = 0; i < LUA_API_MAX_SOUNDS; ++i) {
        if (!g_lua_api.sounds[i].active) {
            return i;
        }
    }

    return -1;
}

static void lua_api_release_all_sounds(void)
{
    for (int i = 0; i < LUA_API_MAX_SOUNDS; ++i) {
        if (!g_lua_api.sounds[i].active && !g_lua_api.sounds[i].has_decoder) continue;
        lua_api_release_sound_slot(&g_lua_api.sounds[i]);
    }
}

static ma_result lua_api_sound_init_from_audio_asset(Lua_API_Sound *sound, const Assets_Runtime_Audio *audio)
{
    ma_result result;
    ma_decoder_config decoder_config = ma_decoder_config_init_default();

    result = ma_decoder_init_memory(audio->data, audio->data_size, &decoder_config, &sound->decoder);
    if (result != MA_SUCCESS) {
        return result;
    }

    sound->has_decoder = true;
    result = ma_sound_init_from_data_source(&g_lua_api.audio_engine, &sound->decoder, 0, NULL, &sound->sound);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(&sound->decoder);
        sound->has_decoder = false;
        return result;
    }

    sound->active = true;
    return MA_SUCCESS;
}

static ma_result lua_api_sound_init_from_source(Lua_API_Sound *sound, const char *name_or_path)
{
    const Assets_Runtime_Audio *audio = NULL;

    lua_api_reset_sound_slot(sound);
    if (g_lua_api.context.assets != NULL) {
        audio = lua_api_find_audio(name_or_path);
    }

    if (audio != NULL) {
        return lua_api_sound_init_from_audio_asset(sound, audio);
    }

    {
        ma_result result = ma_sound_init_from_file(&g_lua_api.audio_engine, name_or_path, 0, NULL, NULL, &sound->sound);
        if (result != MA_SUCCESS) {
            return result;
        }
        sound->active = true;
        return MA_SUCCESS;
    }
}

static int lua_api_hello_world(lua_State *L)
{
    (void)L;
    puts("hello from C -> Lua API");
    return 0;
}

static int lua_api_rgfw_get_width(lua_State *L)
{
    i32 width = 0;
    i32 height = 0;

    RGFW_window_getSize(lua_api_require_window(L), &width, &height);
    lua_pushinteger(L, width);
    return 1;
}

static int lua_api_rgfw_get_height(lua_State *L)
{
    i32 width = 0;
    i32 height = 0;

    RGFW_window_getSize(lua_api_require_window(L), &width, &height);
    lua_pushinteger(L, height);
    return 1;
}

static int lua_api_rgfw_should_close(lua_State *L)
{
    lua_pushboolean(L, RGFW_window_shouldClose(lua_api_require_window(L)) != RGFW_FALSE);
    return 1;
}

static int lua_api_rgfw_close(lua_State *L)
{
    RGFW_window_setShouldClose(lua_api_require_window(L), RGFW_TRUE);
    return 0;
}

static int lua_api_rgfw_set_exit_key(lua_State *L)
{
    RGFW_window_setExitKey(lua_api_require_window(L), (RGFW_key)luaL_checkinteger(L, 1));
    return 0;
}

static int lua_api_rgfw_show_mouse(lua_State *L)
{
    RGFW_window_showMouse(lua_api_require_window(L), lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

static int lua_api_rgfw_get_mouse_vector(lua_State *L)
{
    (void)lua_api_require_window(L);
    lua_api_push_vec2(L, HMM_V2(g_lua_api.mouse_vector_x, g_lua_api.mouse_vector_y));
    return 1;
}

static int lua_api_rgfw_hold_mouse(lua_State *L)
{
    RGFW_window_holdMouse(lua_api_require_window(L));
    return 0;
}

static int lua_api_rgfw_unhold_mouse(lua_State *L)
{
    RGFW_window_unholdMouse(lua_api_require_window(L));
    return 0;
}

static int lua_api_rgfw_is_key_pressed(lua_State *L)
{
    lua_pushboolean(L, RGFW_isKeyPressed((RGFW_key)luaL_checkinteger(L, 1)) != RGFW_FALSE);
    return 1;
}

static int lua_api_rgfw_is_key_released(lua_State *L)
{
    lua_pushboolean(L, RGFW_isKeyReleased((RGFW_key)luaL_checkinteger(L, 1)) != RGFW_FALSE);
    return 1;
}

static int lua_api_rgfw_is_key_down(lua_State *L)
{
    lua_pushboolean(L, RGFW_isKeyDown((RGFW_key)luaL_checkinteger(L, 1)) != RGFW_FALSE);
    return 1;
}

static int lua_api_rgfw_is_mouse_pressed(lua_State *L)
{
    lua_pushboolean(L, RGFW_isMousePressed((RGFW_mouseButton)luaL_checkinteger(L, 1)) != RGFW_FALSE);
    return 1;
}

static int lua_api_rgfw_is_mouse_released(lua_State *L)
{
    lua_pushboolean(L, RGFW_isMouseReleased((RGFW_mouseButton)luaL_checkinteger(L, 1)) != RGFW_FALSE);
    return 1;
}

static int lua_api_rgfw_is_mouse_down(lua_State *L)
{
    lua_pushboolean(L, RGFW_isMouseDown((RGFW_mouseButton)luaL_checkinteger(L, 1)) != RGFW_FALSE);
    return 1;
}

static int lua_api_graphics_rgba(lua_State *L)
{
    int r = (int)luaL_checkinteger(L, 1);
    int g = (int)luaL_checkinteger(L, 2);
    int b = (int)luaL_checkinteger(L, 3);
    int a = (int)luaL_optinteger(L, 4, 255);

    lua_pushinteger(L, (lua_Integer)lua_api_pack_rgba(r, g, b, a));
    return 1;
}

static int lua_api_graphics_get_width(lua_State *L)
{
    (void)L;
    lua_pushinteger(L, lua_api_canvas().width);
    return 1;
}

static int lua_api_graphics_get_height(lua_State *L)
{
    (void)L;
    lua_pushinteger(L, lua_api_canvas().height);
    return 1;
}

static int lua_api_graphics_clear(lua_State *L)
{
    olivec_fill(lua_api_canvas(), (uint32_t)luaL_checkinteger(L, 1));
    return 0;
}

static int lua_api_graphics_rect(lua_State *L)
{
    olivec_rect(
        lua_api_canvas(),
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4),
        (uint32_t)luaL_checkinteger(L, 5)
    );
    return 0;
}

static int lua_api_graphics_frame(lua_State *L)
{
    olivec_frame(
        lua_api_canvas(),
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4),
        (size_t)luaL_checkinteger(L, 5),
        (uint32_t)luaL_checkinteger(L, 6)
    );
    return 0;
}

static int lua_api_graphics_circle(lua_State *L)
{
    olivec_circle(
        lua_api_canvas(),
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (uint32_t)luaL_checkinteger(L, 4)
    );
    return 0;
}

static int lua_api_graphics_line(lua_State *L)
{
    olivec_line(
        lua_api_canvas(),
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4),
        (uint32_t)luaL_checkinteger(L, 5)
    );
    return 0;
}

static int lua_api_graphics_triangle(lua_State *L)
{
    olivec_triangle(
        lua_api_canvas(),
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4),
        (int)luaL_checkinteger(L, 5),
        (int)luaL_checkinteger(L, 6),
        (uint32_t)luaL_checkinteger(L, 7)
    );
    return 0;
}

static int lua_api_graphics_set_pixel(lua_State *L)
{
    Olivec_Canvas canvas = lua_api_canvas();
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);

    if (x >= 0 && y >= 0 && x < (int)canvas.width && y < (int)canvas.height) {
        OLIVEC_PIXEL(canvas, x, y) = (uint32_t)luaL_checkinteger(L, 3);
    }

    return 0;
}

static int lua_api_graphics_draw_image(lua_State *L)
{
    const Assets_Image *image = lua_api_check_image(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int width = (int)luaL_optinteger(L, 4, image->width);
    int height = (int)luaL_optinteger(L, 5, image->height);
    const char *mode = luaL_optstring(L, 6, "blend");
    Olivec_Canvas sprite = lua_api_image_canvas(L, image);

    if (width <= 0 || height <= 0) {
        return luaL_error(L, "image draw size must be positive");
    }

    if (strcmp(mode, "blend") == 0) {
        olivec_sprite_blend(lua_api_canvas(), x, y, width, height, sprite);
        return 0;
    }
    if (strcmp(mode, "copy") == 0) {
        olivec_sprite_copy(lua_api_canvas(), x, y, width, height, sprite);
        return 0;
    }
    if (strcmp(mode, "copy_bilinear") == 0) {
        olivec_sprite_copy_bilinear(lua_api_canvas(), x, y, width, height, sprite);
        return 0;
    }

    return luaL_error(L, "unknown image draw mode '%s'", mode);
}

static int lua_api_core_begin_frame(lua_State *L)
{
    uint32_t clear_color = (uint32_t)luaL_checkinteger(L, 1);
    float clear_depth = (float)luaL_optnumber(L, 2, 1.0f);

    core_begin_frame(lua_api_canvas(), lua_api_require_zbuffer(L), clear_color, clear_depth);
    return 0;
}

static int lua_api_core_draw_text(lua_State *L)
{
    const Assets_Font *font = lua_api_check_font(L, 1);
    const char *text = luaL_checkstring(L, 2);
    int x = (int)luaL_checkinteger(L, 3);
    int y = (int)luaL_checkinteger(L, 4);
    int scale = (int)luaL_checkinteger(L, 5);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 6);

    core_draw_text(lua_api_canvas(), font, text, x, y, scale, color);
    return 0;
}

static int lua_api_core_draw_model(lua_State *L)
{
    const Assets_Model *model = lua_api_check_model(L, 1);
    HMM_Mat4 model_matrix = lua_api_check_mat4(L, 2);
    HMM_Mat4 view_matrix = lua_api_check_mat4(L, 3);
    HMM_Mat4 projection_matrix = lua_api_check_mat4(L, 4);
    uint32_t color = (uint32_t)luaL_checkinteger(L, 5);
    Core_Mesh_Draw_Options options = lua_api_check_draw_options(L, 6);

    core_draw_model(
        model,
        lua_api_canvas(),
        lua_api_require_zbuffer(L),
        model_matrix,
        view_matrix,
        projection_matrix,
        color,
        &options
    );
    return 0;
}

static int lua_api_camera_new(lua_State *L)
{
    Lua_API_Camera *camera = (Lua_API_Camera *)lua_newuserdatauv(L, sizeof(*camera), 0);

    if (lua_istable(L, 1)) {
        camera->camera.position = lua_api_check_vec3(L, 1);
        camera->camera.pitch = (float)luaL_optnumber(L, 2, 0.0f);
        camera->camera.yaw = (float)luaL_optnumber(L, 3, 0.0f);
    } else {
        camera->camera.position = HMM_V3(
            (float)luaL_optnumber(L, 1, 0.0f),
            (float)luaL_optnumber(L, 2, 0.0f),
            (float)luaL_optnumber(L, 3, 0.0f)
        );
        camera->camera.pitch = (float)luaL_optnumber(L, 4, 0.0f);
        camera->camera.yaw = (float)luaL_optnumber(L, 5, 0.0f);
    }

    luaL_getmetatable(L, LUA_API_CAMERA_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_api_camera_get_position(lua_State *L)
{
    lua_api_push_vec3(L, lua_api_check_camera(L, 1)->camera.position);
    return 1;
}

static int lua_api_camera_set_position(lua_State *L)
{
    lua_api_check_camera(L, 1)->camera.position = lua_api_check_vec3(L, 2);
    return 0;
}

static int lua_api_camera_get_pitch(lua_State *L)
{
    lua_pushnumber(L, lua_api_check_camera(L, 1)->camera.pitch);
    return 1;
}

static int lua_api_camera_set_pitch(lua_State *L)
{
    lua_api_check_camera(L, 1)->camera.pitch = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_api_camera_get_yaw(lua_State *L)
{
    lua_pushnumber(L, lua_api_check_camera(L, 1)->camera.yaw);
    return 1;
}

static int lua_api_camera_set_yaw(lua_State *L)
{
    lua_api_check_camera(L, 1)->camera.yaw = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_api_camera_look(lua_State *L)
{
    Core_Fly_Camera *camera = &lua_api_check_camera(L, 1)->camera;

    core_fly_camera_look(
        camera,
        (float)luaL_checknumber(L, 2),
        (float)luaL_checknumber(L, 3),
        (float)luaL_optnumber(L, 4, -89.9f),
        (float)luaL_optnumber(L, 5, 89.9f)
    );
    return 0;
}

static int lua_api_camera_move(lua_State *L)
{
    Core_Fly_Camera *camera = &lua_api_check_camera(L, 1)->camera;

    core_fly_camera_move(
        camera,
        (float)luaL_checknumber(L, 2),
        (float)luaL_checknumber(L, 3),
        (float)luaL_checknumber(L, 4)
    );
    return 0;
}

static int lua_api_camera_forward(lua_State *L)
{
    lua_api_push_vec3(L, core_fly_camera_forward(lua_api_check_camera(L, 1)->camera));
    return 1;
}

static int lua_api_camera_flat_forward(lua_State *L)
{
    lua_api_push_vec3(L, core_fly_camera_flat_forward(lua_api_check_camera(L, 1)->camera));
    return 1;
}

static int lua_api_camera_right(lua_State *L)
{
    lua_api_push_vec3(L, core_fly_camera_right(lua_api_check_camera(L, 1)->camera));
    return 1;
}

static int lua_api_camera_view(lua_State *L)
{
    lua_api_push_mat4(L, core_fly_camera_view(lua_api_check_camera(L, 1)->camera));
    return 1;
}

static int lua_api_hmm_vec2(lua_State *L)
{
    lua_api_push_vec2(L, HMM_V2((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

static int lua_api_hmm_vec3(lua_State *L)
{
    lua_api_push_vec3(L, HMM_V3((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

static int lua_api_hmm_vec4(lua_State *L)
{
    lua_api_push_vec4(L, HMM_V4((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)));
    return 1;
}

static int lua_api_hmm_add3(lua_State *L)
{
    lua_api_push_vec3(L, HMM_AddV3(lua_api_check_vec3(L, 1), lua_api_check_vec3(L, 2)));
    return 1;
}

static int lua_api_hmm_sub3(lua_State *L)
{
    lua_api_push_vec3(L, HMM_SubV3(lua_api_check_vec3(L, 1), lua_api_check_vec3(L, 2)));
    return 1;
}

static int lua_api_hmm_mul3f(lua_State *L)
{
    lua_api_push_vec3(L, HMM_MulV3F(lua_api_check_vec3(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

static int lua_api_hmm_dot3(lua_State *L)
{
    lua_pushnumber(L, HMM_DotV3(lua_api_check_vec3(L, 1), lua_api_check_vec3(L, 2)));
    return 1;
}

static int lua_api_hmm_cross(lua_State *L)
{
    lua_api_push_vec3(L, HMM_Cross(lua_api_check_vec3(L, 1), lua_api_check_vec3(L, 2)));
    return 1;
}

static int lua_api_hmm_len3(lua_State *L)
{
    lua_pushnumber(L, HMM_LenV3(lua_api_check_vec3(L, 1)));
    return 1;
}

static int lua_api_hmm_norm3(lua_State *L)
{
    lua_api_push_vec3(L, HMM_NormV3(lua_api_check_vec3(L, 1)));
    return 1;
}

static int lua_api_hmm_lerp3(lua_State *L)
{
    lua_api_push_vec3(L, HMM_LerpV3(lua_api_check_vec3(L, 1), (float)luaL_checknumber(L, 2), lua_api_check_vec3(L, 3)));
    return 1;
}

static int lua_api_hmm_sin(lua_State *L)
{
    lua_pushnumber(L, HMM_SinF((float)luaL_checknumber(L, 1)));
    return 1;
}

static int lua_api_hmm_cos(lua_State *L)
{
    lua_pushnumber(L, HMM_CosF((float)luaL_checknumber(L, 1)));
    return 1;
}

static int lua_api_hmm_clamp(lua_State *L)
{
    lua_pushnumber(L, HMM_Clamp((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

static int lua_api_hmm_identity4(lua_State *L)
{
    lua_api_push_mat4(L, HMM_M4D(1.0f));
    return 1;
}

static int lua_api_hmm_translate(lua_State *L)
{
    lua_api_push_mat4(L, HMM_Translate(lua_api_check_vec3(L, 1)));
    return 1;
}

static int lua_api_hmm_scale(lua_State *L)
{
    lua_api_push_mat4(L, HMM_Scale(lua_api_check_vec3(L, 1)));
    return 1;
}

static int lua_api_hmm_rotate_rh(lua_State *L)
{
    lua_api_push_mat4(L, HMM_Rotate_RH((float)luaL_checknumber(L, 1), lua_api_check_vec3(L, 2)));
    return 1;
}

static int lua_api_hmm_look_at_rh(lua_State *L)
{
    lua_api_push_mat4(L, HMM_LookAt_RH(lua_api_check_vec3(L, 1), lua_api_check_vec3(L, 2), lua_api_check_vec3(L, 3)));
    return 1;
}

static int lua_api_hmm_perspective_rh_no(lua_State *L)
{
    lua_api_push_mat4(L, HMM_Perspective_RH_NO(
        (float)luaL_checknumber(L, 1),
        (float)luaL_checknumber(L, 2),
        (float)luaL_checknumber(L, 3),
        (float)luaL_checknumber(L, 4)
    ));
    return 1;
}

static int lua_api_hmm_mul_m4(lua_State *L)
{
    lua_api_push_mat4(L, HMM_MulM4(lua_api_check_mat4(L, 1), lua_api_check_mat4(L, 2)));
    return 1;
}

static int lua_api_hmm_transform_point(lua_State *L)
{
    HMM_Vec4 value = HMM_MulM4V4(lua_api_check_mat4(L, 1), HMM_V4V(lua_api_check_vec3(L, 2), 1.0f));

    if (value.W != 0.0f) {
        lua_api_push_vec3(L, HMM_V3(value.X / value.W, value.Y / value.W, value.Z / value.W));
    } else {
        lua_api_push_vec3(L, value.XYZ);
    }

    return 1;
}

static int lua_api_hmm_transform_vector(lua_State *L)
{
    lua_api_push_vec3(L, HMM_MulM4V4(lua_api_check_mat4(L, 1), HMM_V4V(lua_api_check_vec3(L, 2), 0.0f)).XYZ);
    return 1;
}

static int lua_api_audio_init(lua_State *L)
{
    ma_result result;

    if (g_lua_api.audio_ready) {
        lua_pushboolean(L, 1);
        return 1;
    }

    result = ma_engine_init(NULL, &g_lua_api.audio_engine);
    if (result != MA_SUCCESS) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    g_lua_api.audio_ready = true;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_api_audio_shutdown(lua_State *L)
{
    (void)L;

    if (g_lua_api.audio_ready) {
        lua_api_release_all_sounds();
        ma_engine_uninit(&g_lua_api.audio_engine);
        g_lua_api.audio_ready = false;
    }

    return 0;
}

static int lua_api_audio_is_ready(lua_State *L)
{
    lua_pushboolean(L, g_lua_api.audio_ready);
    return 1;
}

static int lua_api_audio_set_master_volume(lua_State *L)
{
    if (!g_lua_api.audio_ready) {
        return luaL_error(L, "audio engine is not initialized");
    }

    ma_engine_set_volume(&g_lua_api.audio_engine, (float)luaL_checknumber(L, 1));
    return 0;
}

static int lua_api_audio_play(lua_State *L)
{
    ma_result result;
    const char *name_or_path;
    const Assets_Runtime_Audio *audio = NULL;

    if (!g_lua_api.audio_ready) {
        return luaL_error(L, "audio engine is not initialized");
    }

    name_or_path = luaL_checkstring(L, 1);
    if (g_lua_api.context.assets != NULL) {
        audio = lua_api_find_audio(name_or_path);
    }

    if (audio == NULL) {
        result = ma_engine_play_sound(&g_lua_api.audio_engine, name_or_path, NULL);
        if (result != MA_SUCCESS) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, ma_result_description(result));
            return 2;
        }

        lua_pushboolean(L, 1);
        return 1;
    }

    {
        int slot_index = lua_api_alloc_sound_slot();
        Lua_API_Sound *sound;

        if (slot_index < 0) {
            lua_pushboolean(L, 0);
            lua_pushliteral(L, "no free audio slots");
            return 2;
        }

        sound = &g_lua_api.sounds[slot_index];
        result = lua_api_sound_init_from_source(sound, name_or_path);
        if (result != MA_SUCCESS) {
            lua_api_release_sound_slot(sound);
            lua_pushboolean(L, 0);
            lua_pushstring(L, ma_result_description(result));
            return 2;
        }

        sound->transient = true;
        result = ma_sound_start(&sound->sound);
        if (result != MA_SUCCESS) {
            lua_api_release_sound_slot(sound);
            lua_pushboolean(L, 0);
            lua_pushstring(L, ma_result_description(result));
            return 2;
        }
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_api_audio_load_sound(lua_State *L)
{
    ma_result result;
    int slot_index;
    const char *name_or_path;

    if (!g_lua_api.audio_ready) {
        return luaL_error(L, "audio engine is not initialized");
    }

    slot_index = lua_api_alloc_sound_slot();
    if (slot_index < 0) {
        return luaL_error(L, "no free audio slots");
    }

    name_or_path = luaL_checkstring(L, 1);
    result = lua_api_sound_init_from_source(&g_lua_api.sounds[slot_index], name_or_path);
    if (result != MA_SUCCESS) {
        lua_api_release_sound_slot(&g_lua_api.sounds[slot_index]);
        lua_pushnil(L);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    lua_pushinteger(L, slot_index + 1);
    return 1;
}

static int lua_api_audio_unload_sound(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    lua_api_release_sound_slot(sound);
    return 0;
}

static int lua_api_audio_start_sound(lua_State *L)
{
    ma_result result;
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    result = ma_sound_start(&sound->sound);
    if (result != MA_SUCCESS) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_api_audio_stop_sound(lua_State *L)
{
    ma_result result;
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    result = ma_sound_stop(&sound->sound);
    if (result != MA_SUCCESS) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_api_audio_set_sound_volume(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    ma_sound_set_volume(&sound->sound, (float)luaL_checknumber(L, 2));
    return 0;
}

static int lua_api_audio_set_sound_pitch(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    ma_sound_set_pitch(&sound->sound, (float)luaL_checknumber(L, 2));
    return 0;
}

static int lua_api_audio_set_sound_pan(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    ma_sound_set_pan(&sound->sound, (float)luaL_checknumber(L, 2));
    return 0;
}

static int lua_api_audio_set_sound_looping(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    ma_sound_set_looping(&sound->sound, lua_toboolean(L, 2) ? MA_TRUE : MA_FALSE);
    return 0;
}

static int lua_api_audio_is_sound_playing(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    lua_pushboolean(L, ma_sound_is_playing(&sound->sound) == MA_TRUE);
    return 1;
}

static int lua_api_audio_sound_at_end(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    lua_pushboolean(L, ma_sound_at_end(&sound->sound) == MA_TRUE);
    return 1;
}

static int lua_api_audio_seek_sound(lua_State *L)
{
    ma_result result;
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    result = ma_sound_seek_to_second(&sound->sound, (float)luaL_checknumber(L, 2));
    if (result != MA_SUCCESS) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static const char *const lua_api_window_constants[] = {
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

static const Lua_API_Function_Def lua_api_window_functions[] = {
    {"get_width", lua_api_rgfw_get_width, "---@return integer\nfunction window.get_width() end\n"},
    {"get_height", lua_api_rgfw_get_height, "---@return integer\nfunction window.get_height() end\n"},
    {"should_close", lua_api_rgfw_should_close, "---@return boolean\nfunction window.should_close() end\n"},
    {"close", lua_api_rgfw_close, "function window.close() end\n"},
    {"set_exit_key", lua_api_rgfw_set_exit_key, "---@param key integer\nfunction window.set_exit_key(key) end\n"},
    {"show_mouse", lua_api_rgfw_show_mouse, "---@param visible boolean\nfunction window.show_mouse(visible) end\n"},
    {"get_mouse_vector", lua_api_rgfw_get_mouse_vector, "---@return LimerenceVec2\nfunction window.get_mouse_vector() end\n"},
    {"hold_mouse", lua_api_rgfw_hold_mouse, "function window.hold_mouse() end\n"},
    {"unhold_mouse", lua_api_rgfw_unhold_mouse, "function window.unhold_mouse() end\n"},
    {"is_key_pressed", lua_api_rgfw_is_key_pressed, "---@param key integer\n---@return boolean\nfunction window.is_key_pressed(key) end\n"},
    {"is_key_released", lua_api_rgfw_is_key_released, "---@param key integer\n---@return boolean\nfunction window.is_key_released(key) end\n"},
    {"is_key_down", lua_api_rgfw_is_key_down, "---@param key integer\n---@return boolean\nfunction window.is_key_down(key) end\n"},
    {"is_mouse_pressed", lua_api_rgfw_is_mouse_pressed, "---@param button integer\n---@return boolean\nfunction window.is_mouse_pressed(button) end\n"},
    {"is_mouse_released", lua_api_rgfw_is_mouse_released, "---@param button integer\n---@return boolean\nfunction window.is_mouse_released(button) end\n"},
    {"is_mouse_down", lua_api_rgfw_is_mouse_down, "---@param button integer\n---@return boolean\nfunction window.is_mouse_down(button) end\n"},
};

static const Lua_API_Function_Def lua_api_graphics_functions[] = {
    {"rgba", lua_api_graphics_rgba, "---@param r integer\n---@param g integer\n---@param b integer\n---@param a? integer\n---@return integer\nfunction graphics.rgba(r, g, b, a) end\n"},
    {"get_width", lua_api_graphics_get_width, "---@return integer\nfunction graphics.get_width() end\n"},
    {"get_height", lua_api_graphics_get_height, "---@return integer\nfunction graphics.get_height() end\n"},
    {"clear", lua_api_graphics_clear, "---@param color integer\nfunction graphics.clear(color) end\n"},
    {"rect", lua_api_graphics_rect, "---@param x integer\n---@param y integer\n---@param w integer\n---@param h integer\n---@param color integer\nfunction graphics.rect(x, y, w, h, color) end\n"},
    {"frame", lua_api_graphics_frame, "---@param x integer\n---@param y integer\n---@param w integer\n---@param h integer\n---@param thickness integer\n---@param color integer\nfunction graphics.frame(x, y, w, h, thickness, color) end\n"},
    {"circle", lua_api_graphics_circle, "---@param x integer\n---@param y integer\n---@param r integer\n---@param color integer\nfunction graphics.circle(x, y, r, color) end\n"},
    {"line", lua_api_graphics_line, "---@param x1 integer\n---@param y1 integer\n---@param x2 integer\n---@param y2 integer\n---@param color integer\nfunction graphics.line(x1, y1, x2, y2, color) end\n"},
    {"triangle", lua_api_graphics_triangle, "---@param x1 integer\n---@param y1 integer\n---@param x2 integer\n---@param y2 integer\n---@param x3 integer\n---@param y3 integer\n---@param color integer\nfunction graphics.triangle(x1, y1, x2, y2, x3, y3, color) end\n"},
    {"set_pixel", lua_api_graphics_set_pixel, "---@param x integer\n---@param y integer\n---@param color integer\nfunction graphics.set_pixel(x, y, color) end\n"},
    {"draw_image", lua_api_graphics_draw_image, "---@param image string\n---@param x integer\n---@param y integer\n---@param width? integer\n---@param height? integer\n---@param mode? 'blend'|'copy'|'copy_bilinear'\nfunction graphics.draw_image(image, x, y, width, height, mode) end\n"},
};

static const Lua_API_Function_Def lua_api_core_functions[] = {
    {"begin_frame", lua_api_core_begin_frame, "---@param clear_color integer\n---@param clear_depth? number\nfunction core.begin_frame(clear_color, clear_depth) end\n"},
    {"draw_text", lua_api_core_draw_text, "---@param font string\n---@param text string\n---@param x integer\n---@param y integer\n---@param scale integer\n---@param color integer\nfunction core.draw_text(font, text, x, y, scale, color) end\n"},
    {"draw_model", lua_api_core_draw_model, "---@param model string\n---@param model_matrix LimerenceMat4\n---@param view_matrix LimerenceMat4\n---@param projection_matrix LimerenceMat4\n---@param color integer\n---@param options? LimerenceDrawOptions\nfunction core.draw_model(model, model_matrix, view_matrix, projection_matrix, color, options) end\n"},
    {"camera", lua_api_camera_new, "---@param position_or_x LimerenceVec3|table|number\n---@param y_or_pitch? number\n---@param z_or_yaw? number\n---@param pitch? number\n---@param yaw? number\n---@return LimerenceCamera\nfunction core.camera(position_or_x, y_or_pitch, z_or_yaw, pitch, yaw) end\n"},
};

static const Lua_API_Function_Def lua_api_math_functions[] = {
    {"vec2", lua_api_hmm_vec2, "---@param x number\n---@param y number\n---@return LimerenceVec2\nfunction math.vec2(x, y) end\n"},
    {"vec3", lua_api_hmm_vec3, "---@param x number\n---@param y number\n---@param z number\n---@return LimerenceVec3\nfunction math.vec3(x, y, z) end\n"},
    {"vec4", lua_api_hmm_vec4, "---@param x number\n---@param y number\n---@param z number\n---@param w number\n---@return LimerenceVec4\nfunction math.vec4(x, y, z, w) end\n"},
    {"add3", lua_api_hmm_add3, "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction math.add3(a, b) end\n"},
    {"sub3", lua_api_hmm_sub3, "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction math.sub3(a, b) end\n"},
    {"mul3f", lua_api_hmm_mul3f, "---@param value LimerenceVec3\n---@param scalar number\n---@return LimerenceVec3\nfunction math.mul3f(value, scalar) end\n"},
    {"dot3", lua_api_hmm_dot3, "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return number\nfunction math.dot3(a, b) end\n"},
    {"cross", lua_api_hmm_cross, "---@param a LimerenceVec3\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction math.cross(a, b) end\n"},
    {"len3", lua_api_hmm_len3, "---@param value LimerenceVec3\n---@return number\nfunction math.len3(value) end\n"},
    {"norm3", lua_api_hmm_norm3, "---@param value LimerenceVec3\n---@return LimerenceVec3\nfunction math.norm3(value) end\n"},
    {"lerp3", lua_api_hmm_lerp3, "---@param a LimerenceVec3\n---@param t number\n---@param b LimerenceVec3\n---@return LimerenceVec3\nfunction math.lerp3(a, t, b) end\n"},
    {"sin", lua_api_hmm_sin, "---@param value number\n---@return number\nfunction math.sin(value) end\n"},
    {"cos", lua_api_hmm_cos, "---@param value number\n---@return number\nfunction math.cos(value) end\n"},
    {"clamp", lua_api_hmm_clamp, "---@param min number\n---@param value number\n---@param max number\n---@return number\nfunction math.clamp(min, value, max) end\n"},
    {"identity4", lua_api_hmm_identity4, "---@return LimerenceMat4\nfunction math.identity4() end\n"},
    {"translate", lua_api_hmm_translate, "---@param value LimerenceVec3\n---@return LimerenceMat4\nfunction math.translate(value) end\n"},
    {"scale", lua_api_hmm_scale, "---@param value LimerenceVec3\n---@return LimerenceMat4\nfunction math.scale(value) end\n"},
    {"rotate_rh", lua_api_hmm_rotate_rh, "---@param degrees number\n---@param axis LimerenceVec3\n---@return LimerenceMat4\nfunction math.rotate_rh(degrees, axis) end\n"},
    {"look_at_rh", lua_api_hmm_look_at_rh, "---@param eye LimerenceVec3\n---@param center LimerenceVec3\n---@param up LimerenceVec3\n---@return LimerenceMat4\nfunction math.look_at_rh(eye, center, up) end\n"},
    {"perspective_rh_no", lua_api_hmm_perspective_rh_no, "---@param fov_degrees number\n---@param aspect number\n---@param near_plane number\n---@param far_plane number\n---@return LimerenceMat4\nfunction math.perspective_rh_no(fov_degrees, aspect, near_plane, far_plane) end\n"},
    {"mul_m4", lua_api_hmm_mul_m4, "---@param a LimerenceMat4\n---@param b LimerenceMat4\n---@return LimerenceMat4\nfunction math.mul_m4(a, b) end\n"},
    {"transform_point", lua_api_hmm_transform_point, "---@param matrix LimerenceMat4\n---@param point LimerenceVec3\n---@return LimerenceVec3\nfunction math.transform_point(matrix, point) end\n"},
    {"transform_vector", lua_api_hmm_transform_vector, "---@param matrix LimerenceMat4\n---@param vector LimerenceVec3\n---@return LimerenceVec3\nfunction math.transform_vector(matrix, vector) end\n"},
};

static const Lua_API_Function_Def lua_api_audio_functions[] = {
    {"init", lua_api_audio_init, "---@return boolean ok, string? err\nfunction audio.init() end\n"},
    {"shutdown", lua_api_audio_shutdown, "function audio.shutdown() end\n"},
    {"is_ready", lua_api_audio_is_ready, "---@return boolean\nfunction audio.is_ready() end\n"},
    {"set_master_volume", lua_api_audio_set_master_volume, "---@param volume number\nfunction audio.set_master_volume(volume) end\n"},
    {"play", lua_api_audio_play, "---@param name_or_path string\n---@return boolean ok, string? err\nfunction audio.play(name_or_path) end\n"},
    {"load_sound", lua_api_audio_load_sound, "---@param name_or_path string\n---@return integer? handle, string? err\nfunction audio.load_sound(name_or_path) end\n"},
    {"unload_sound", lua_api_audio_unload_sound, "---@param handle integer\nfunction audio.unload_sound(handle) end\n"},
    {"start", lua_api_audio_start_sound, "---@param handle integer\n---@return boolean ok, string? err\nfunction audio.start(handle) end\n"},
    {"stop", lua_api_audio_stop_sound, "---@param handle integer\n---@return boolean ok, string? err\nfunction audio.stop(handle) end\n"},
    {"set_volume", lua_api_audio_set_sound_volume, "---@param handle integer\n---@param volume number\nfunction audio.set_volume(handle, volume) end\n"},
    {"set_pitch", lua_api_audio_set_sound_pitch, "---@param handle integer\n---@param pitch number\nfunction audio.set_pitch(handle, pitch) end\n"},
    {"set_pan", lua_api_audio_set_sound_pan, "---@param handle integer\n---@param pan number\nfunction audio.set_pan(handle, pan) end\n"},
    {"set_looping", lua_api_audio_set_sound_looping, "---@param handle integer\n---@param looping boolean\nfunction audio.set_looping(handle, looping) end\n"},
    {"is_playing", lua_api_audio_is_sound_playing, "---@param handle integer\n---@return boolean\nfunction audio.is_playing(handle) end\n"},
    {"at_end", lua_api_audio_sound_at_end, "---@param handle integer\n---@return boolean\nfunction audio.at_end(handle) end\n"},
    {"seek", lua_api_audio_seek_sound, "---@param handle integer\n---@param seconds number\n---@return boolean ok, string? err\nfunction audio.seek(handle, seconds) end\n"},
};

static const Lua_API_Function_Def lua_api_camera_methods[] = {
    {"get_position", lua_api_camera_get_position, "---@return LimerenceVec3\nfunction LimerenceCamera:get_position() end\n"},
    {"set_position", lua_api_camera_set_position, "---@param position LimerenceVec3\nfunction LimerenceCamera:set_position(position) end\n"},
    {"get_pitch", lua_api_camera_get_pitch, "---@return number\nfunction LimerenceCamera:get_pitch() end\n"},
    {"set_pitch", lua_api_camera_set_pitch, "---@param pitch number\nfunction LimerenceCamera:set_pitch(pitch) end\n"},
    {"get_yaw", lua_api_camera_get_yaw, "---@return number\nfunction LimerenceCamera:get_yaw() end\n"},
    {"set_yaw", lua_api_camera_set_yaw, "---@param yaw number\nfunction LimerenceCamera:set_yaw(yaw) end\n"},
    {"look", lua_api_camera_look, "---@param yaw_delta number\n---@param pitch_delta number\n---@param pitch_min? number\n---@param pitch_max? number\nfunction LimerenceCamera:look(yaw_delta, pitch_delta, pitch_min, pitch_max) end\n"},
    {"move", lua_api_camera_move, "---@param forward_distance number\n---@param strafe_distance number\n---@param vertical_distance number\nfunction LimerenceCamera:move(forward_distance, strafe_distance, vertical_distance) end\n"},
    {"forward", lua_api_camera_forward, "---@return LimerenceVec3\nfunction LimerenceCamera:forward() end\n"},
    {"flat_forward", lua_api_camera_flat_forward, "---@return LimerenceVec3\nfunction LimerenceCamera:flat_forward() end\n"},
    {"right", lua_api_camera_right, "---@return LimerenceVec3\nfunction LimerenceCamera:right() end\n"},
    {"view", lua_api_camera_view, "---@return LimerenceMat4\nfunction LimerenceCamera:view() end\n"},
};

static const Lua_API_Module_Def lua_api_modules[] = {
    {
        "window",
        "LimerenceWindow",
        lua_api_window_functions,
        LUA_API_ARRAY_COUNT(lua_api_window_functions),
        lua_api_window_constants,
        LUA_API_ARRAY_COUNT(lua_api_window_constants),
    },
    {
        "graphics",
        "LimerenceGraphics",
        lua_api_graphics_functions,
        LUA_API_ARRAY_COUNT(lua_api_graphics_functions),
        NULL,
        0,
    },
    {
        "core",
        "LimerenceCore",
        lua_api_core_functions,
        LUA_API_ARRAY_COUNT(lua_api_core_functions),
        NULL,
        0,
    },
    {
        "math",
        "LimerenceMath",
        lua_api_math_functions,
        LUA_API_ARRAY_COUNT(lua_api_math_functions),
        NULL,
        0,
    },
    {
        "audio",
        "LimerenceAudio",
        lua_api_audio_functions,
        LUA_API_ARRAY_COUNT(lua_api_audio_functions),
        NULL,
        0,
    },
};

static const Lua_API_Class_Def lua_api_classes[] = {
    {
        "LimerenceCamera",
        lua_api_camera_methods,
        LUA_API_ARRAY_COUNT(lua_api_camera_methods),
    },
};

static const Lua_API_Global_Def lua_api_globals[] = {
    {"hello_world", lua_api_hello_world, "function hello_world() end\n"},
};

static void lua_api_set_window_constants(lua_State *L)
{
    static const lua_Integer lua_api_window_constant_values[] = {
        RGFW_escape,
        RGFW_up,
        RGFW_down,
        RGFW_left,
        RGFW_right,
        RGFW_w,
        RGFW_a,
        RGFW_s,
        RGFW_d,
        RGFW_q,
        RGFW_e,
        RGFW_space,
        RGFW_shiftL,
        RGFW_shiftR,
        RGFW_mouseLeft,
        RGFW_mouseRight,
        RGFW_mouseMiddle,
    };

    for (size_t i = 0; i < LUA_API_ARRAY_COUNT(lua_api_window_constants); ++i) {
        lua_pushinteger(L, lua_api_window_constant_values[i]);
        lua_setfield(L, -2, lua_api_window_constants[i]);
    }
}

static void lua_api_set_module_constants(lua_State *L, const Lua_API_Module_Def *module)
{
    if (strcmp(module->module_name, "window") == 0) {
        lua_api_set_window_constants(L);
    }
}

static void lua_api_register_module(lua_State *L, const Lua_API_Module_Def *module)
{
    lua_getglobal(L, module->module_name);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    for (size_t i = 0; i < module->function_count; ++i) {
        lua_pushcfunction(L, module->functions[i].function);
        lua_setfield(L, -2, module->functions[i].name);
    }
    lua_api_set_module_constants(L, module);
    lua_setglobal(L, module->module_name);
}

static void lua_api_register_class(lua_State *L, const Lua_API_Class_Def *class_def)
{
    luaL_newmetatable(L, LUA_API_CAMERA_METATABLE);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    for (size_t i = 0; i < class_def->method_count; ++i) {
        lua_pushcfunction(L, class_def->methods[i].function);
        lua_setfield(L, -2, class_def->methods[i].name);
    }
    lua_pop(L, 1);
}

static void lua_api_register_global(lua_State *L, const Lua_API_Global_Def *global)
{
    lua_pushcfunction(L, global->function);
    lua_setglobal(L, global->name);
}

bool lua_api_init(const Lua_API_Context *context)
{

    if (context == NULL) {
        fputs("lua_api_init: context is NULL\n", stderr);
        return false;
    }

    lua_api_shutdown();
    g_lua_api.context = *context;
    g_lua_api.mouse_vector_x = 0.0f;
    g_lua_api.mouse_vector_y = 0.0f;
    g_lua_api.state = luaL_newstate();
    if (g_lua_api.state == NULL) {
        fputs("failed to create Lua state\n", stderr);
        return false;
    }

    luaL_openlibs(g_lua_api.state);
    for (size_t i = 0; i < LUA_API_ARRAY_COUNT(lua_api_classes); ++i) {
        lua_api_register_class(g_lua_api.state, &lua_api_classes[i]);
    }
    for (size_t i = 0; i < LUA_API_ARRAY_COUNT(lua_api_globals); ++i) {
        lua_api_register_global(g_lua_api.state, &lua_api_globals[i]);
    }
    for (size_t i = 0; i < LUA_API_ARRAY_COUNT(lua_api_modules); ++i) {
        lua_api_register_module(g_lua_api.state, &lua_api_modules[i]);
    }

    return true;
}

void lua_api_shutdown(void)
{
    if (g_lua_api.audio_ready) {
        lua_api_release_all_sounds();
        ma_engine_uninit(&g_lua_api.audio_engine);
        g_lua_api.audio_ready = false;
    }

    if (g_lua_api.state != NULL) {
        lua_close(g_lua_api.state);
        g_lua_api.state = NULL;
    }

    memset(&g_lua_api.context, 0, sizeof(g_lua_api.context));
    g_lua_api.mouse_vector_x = 0.0f;
    g_lua_api.mouse_vector_y = 0.0f;
}

void lua_api_set_mouse_vector(float x, float y)
{
    g_lua_api.mouse_vector_x = x;
    g_lua_api.mouse_vector_y = y;
}

const char *lua_api_stub_prelude(void)
{
    return lua_api_stub_prelude_text;
}

size_t lua_api_module_count(void)
{
    return LUA_API_ARRAY_COUNT(lua_api_modules);
}

const Lua_API_Module_Def *lua_api_module_def(size_t index)
{
    if (index >= LUA_API_ARRAY_COUNT(lua_api_modules)) {
        return NULL;
    }

    return &lua_api_modules[index];
}

size_t lua_api_class_count(void)
{
    return LUA_API_ARRAY_COUNT(lua_api_classes);
}

const Lua_API_Class_Def *lua_api_class_def(size_t index)
{
    if (index >= LUA_API_ARRAY_COUNT(lua_api_classes)) {
        return NULL;
    }

    return &lua_api_classes[index];
}

size_t lua_api_global_count(void)
{
    return LUA_API_ARRAY_COUNT(lua_api_globals);
}

const Lua_API_Global_Def *lua_api_global_def(size_t index)
{
    if (index >= LUA_API_ARRAY_COUNT(lua_api_globals)) {
        return NULL;
    }

    return &lua_api_globals[index];
}

static bool lua_api_report_call_status(int status)
{
    if (status != LUA_OK) {
        fprintf(stderr, "lua error: %s\n", lua_tostring(g_lua_api.state, -1));
        lua_pop(g_lua_api.state, 1);
        return false;
    }

    return true;
}

bool lua_api_run_file(const char *path)
{
    if (g_lua_api.state == NULL) {
        fputs("lua_api_run_file: Lua state is not initialized\n", stderr);
        return false;
    }

    return lua_api_report_call_status(luaL_dofile(g_lua_api.state, path));
}

bool lua_api_run_string(const char *chunk)
{
    if (g_lua_api.state == NULL) {
        fputs("lua_api_run_string: Lua state is not initialized\n", stderr);
        return false;
    }

    return lua_api_report_call_status(luaL_dostring(g_lua_api.state, chunk));
}

bool lua_api_set_package_path(const char *path)
{
    if (g_lua_api.state == NULL) {
        fputs("lua_api_set_package_path: Lua state is not initialized\n", stderr);
        return false;
    }

    lua_getglobal(g_lua_api.state, "package");
    if (!lua_istable(g_lua_api.state, -1)) {
        lua_pop(g_lua_api.state, 1);
        fputs("lua_api_set_package_path: package table is unavailable\n", stderr);
        return false;
    }

    lua_pushstring(g_lua_api.state, path != NULL ? path : "");
    lua_setfield(g_lua_api.state, -2, "path");
    lua_pop(g_lua_api.state, 1);
    return true;
}

bool lua_api_call_global0(const char *name)
{
    int status;

    if (g_lua_api.state == NULL) {
        fputs("lua_api_call_global0: Lua state is not initialized\n", stderr);
        return false;
    }

    lua_getglobal(g_lua_api.state, name);
    if (lua_isnil(g_lua_api.state, -1)) {
        lua_pop(g_lua_api.state, 1);
        return true;
    }

    if (!lua_isfunction(g_lua_api.state, -1)) {
        fprintf(stderr, "lua error: global '%s' is not a function\n", name);
        lua_pop(g_lua_api.state, 1);
        return false;
    }

    lua_api_cleanup_finished_sounds();
    status = lua_pcall(g_lua_api.state, 0, 0, 0);
    return lua_api_report_call_status(status);
}

bool lua_api_call_global1_number(const char *name, double value)
{
    int status;

    if (g_lua_api.state == NULL) {
        fputs("lua_api_call_global1_number: Lua state is not initialized\n", stderr);
        return false;
    }

    lua_getglobal(g_lua_api.state, name);
    if (lua_isnil(g_lua_api.state, -1)) {
        lua_pop(g_lua_api.state, 1);
        return true;
    }

    if (!lua_isfunction(g_lua_api.state, -1)) {
        fprintf(stderr, "lua error: global '%s' is not a function\n", name);
        lua_pop(g_lua_api.state, 1);
        return false;
    }

    lua_pushnumber(g_lua_api.state, value);
    lua_api_cleanup_finished_sounds();
    status = lua_pcall(g_lua_api.state, 1, 0, 0);
    return lua_api_report_call_status(status);
}
