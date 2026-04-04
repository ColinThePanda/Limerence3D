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

typedef struct {
    bool active;
    ma_sound sound;
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

static const Assets_Model *lua_api_find_model(const char *name)
{
    return assets_runtime_find_model(g_lua_api.context.assets, name);
}

static const Assets_Font *lua_api_find_font(const char *name)
{
    return assets_runtime_find_font(g_lua_api.context.assets, name);
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

static int lua_api_alloc_sound_slot(void)
{
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
        if (!g_lua_api.sounds[i].active) continue;
        ma_sound_uninit(&g_lua_api.sounds[i].sound);
        g_lua_api.sounds[i].active = false;
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
    const char *path;

    if (!g_lua_api.audio_ready) {
        return luaL_error(L, "audio engine is not initialized");
    }

    path = luaL_checkstring(L, 1);
    result = ma_engine_play_sound(&g_lua_api.audio_engine, path, NULL);
    if (result != MA_SUCCESS) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_api_audio_load_sound(lua_State *L)
{
    ma_result result;
    int slot_index;
    const char *path;

    if (!g_lua_api.audio_ready) {
        return luaL_error(L, "audio engine is not initialized");
    }

    slot_index = lua_api_alloc_sound_slot();
    if (slot_index < 0) {
        return luaL_error(L, "no free audio slots");
    }

    path = luaL_checkstring(L, 1);
    result = ma_sound_init_from_file(&g_lua_api.audio_engine, path, 0, NULL, NULL, &g_lua_api.sounds[slot_index].sound);
    if (result != MA_SUCCESS) {
        lua_pushnil(L);
        lua_pushstring(L, ma_result_description(result));
        return 2;
    }

    g_lua_api.sounds[slot_index].active = true;
    lua_pushinteger(L, slot_index + 1);
    return 1;
}

static int lua_api_audio_unload_sound(lua_State *L)
{
    Lua_API_Sound *sound = lua_api_get_sound_slot((int)luaL_checkinteger(L, 1));

    if (sound == NULL) {
        return luaL_error(L, "invalid sound handle");
    }

    ma_sound_uninit(&sound->sound);
    sound->active = false;
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

static void lua_api_set_constants(lua_State *L)
{
    lua_pushinteger(L, RGFW_escape);
    lua_setfield(L, -2, "key_escape");
    lua_pushinteger(L, RGFW_up);
    lua_setfield(L, -2, "key_up");
    lua_pushinteger(L, RGFW_down);
    lua_setfield(L, -2, "key_down");
    lua_pushinteger(L, RGFW_left);
    lua_setfield(L, -2, "key_left");
    lua_pushinteger(L, RGFW_right);
    lua_setfield(L, -2, "key_right");
    lua_pushinteger(L, RGFW_w);
    lua_setfield(L, -2, "key_w");
    lua_pushinteger(L, RGFW_a);
    lua_setfield(L, -2, "key_a");
    lua_pushinteger(L, RGFW_s);
    lua_setfield(L, -2, "key_s");
    lua_pushinteger(L, RGFW_d);
    lua_setfield(L, -2, "key_d");
    lua_pushinteger(L, RGFW_q);
    lua_setfield(L, -2, "key_q");
    lua_pushinteger(L, RGFW_e);
    lua_setfield(L, -2, "key_e");
    lua_pushinteger(L, RGFW_space);
    lua_setfield(L, -2, "key_space");
    lua_pushinteger(L, RGFW_shiftL);
    lua_setfield(L, -2, "key_shift_left");
    lua_pushinteger(L, RGFW_shiftR);
    lua_setfield(L, -2, "key_shift_right");
    lua_pushinteger(L, RGFW_mouseLeft);
    lua_setfield(L, -2, "mouse_left");
    lua_pushinteger(L, RGFW_mouseRight);
    lua_setfield(L, -2, "mouse_right");
    lua_pushinteger(L, RGFW_mouseMiddle);
    lua_setfield(L, -2, "mouse_middle");
}

static void lua_api_register_module(lua_State *L, const char *name, const luaL_Reg *functions)
{
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    if (strcmp(name, "rgfw") == 0) {
        lua_api_set_constants(L);
    }
    lua_setglobal(L, name);
}

bool lua_api_init(const Lua_API_Context *context)
{
    static const luaL_Reg rgfw_functions[] = {
        {"get_width", lua_api_rgfw_get_width},
        {"get_height", lua_api_rgfw_get_height},
        {"should_close", lua_api_rgfw_should_close},
        {"close", lua_api_rgfw_close},
        {"set_exit_key", lua_api_rgfw_set_exit_key},
        {"show_mouse", lua_api_rgfw_show_mouse},
        {"get_mouse_vector", lua_api_rgfw_get_mouse_vector},
        {"hold_mouse", lua_api_rgfw_hold_mouse},
        {"unhold_mouse", lua_api_rgfw_unhold_mouse},
        {"is_key_pressed", lua_api_rgfw_is_key_pressed},
        {"is_key_released", lua_api_rgfw_is_key_released},
        {"is_key_down", lua_api_rgfw_is_key_down},
        {"is_mouse_pressed", lua_api_rgfw_is_mouse_pressed},
        {"is_mouse_released", lua_api_rgfw_is_mouse_released},
        {"is_mouse_down", lua_api_rgfw_is_mouse_down},
        {0},
    };
    static const luaL_Reg graphics_functions[] = {
        {"rgba", lua_api_graphics_rgba},
        {"get_width", lua_api_graphics_get_width},
        {"get_height", lua_api_graphics_get_height},
        {"clear", lua_api_graphics_clear},
        {"rect", lua_api_graphics_rect},
        {"frame", lua_api_graphics_frame},
        {"circle", lua_api_graphics_circle},
        {"line", lua_api_graphics_line},
        {"triangle", lua_api_graphics_triangle},
        {"set_pixel", lua_api_graphics_set_pixel},
        {0},
    };
    static const luaL_Reg core_functions[] = {
        {"begin_frame", lua_api_core_begin_frame},
        {"draw_text", lua_api_core_draw_text},
        {"draw_model", lua_api_core_draw_model},
        {"camera", lua_api_camera_new},
        {0},
    };
    static const luaL_Reg hmm_functions[] = {
        {"vec2", lua_api_hmm_vec2},
        {"vec3", lua_api_hmm_vec3},
        {"vec4", lua_api_hmm_vec4},
        {"add3", lua_api_hmm_add3},
        {"sub3", lua_api_hmm_sub3},
        {"mul3f", lua_api_hmm_mul3f},
        {"dot3", lua_api_hmm_dot3},
        {"cross", lua_api_hmm_cross},
        {"len3", lua_api_hmm_len3},
        {"norm3", lua_api_hmm_norm3},
        {"lerp3", lua_api_hmm_lerp3},
        {"sin", lua_api_hmm_sin},
        {"cos", lua_api_hmm_cos},
        {"clamp", lua_api_hmm_clamp},
        {"identity4", lua_api_hmm_identity4},
        {"translate", lua_api_hmm_translate},
        {"scale", lua_api_hmm_scale},
        {"rotate_rh", lua_api_hmm_rotate_rh},
        {"look_at_rh", lua_api_hmm_look_at_rh},
        {"perspective_rh_no", lua_api_hmm_perspective_rh_no},
        {"mul_m4", lua_api_hmm_mul_m4},
        {"transform_point", lua_api_hmm_transform_point},
        {"transform_vector", lua_api_hmm_transform_vector},
        {0},
    };
    static const luaL_Reg audio_functions[] = {
        {"init", lua_api_audio_init},
        {"shutdown", lua_api_audio_shutdown},
        {"is_ready", lua_api_audio_is_ready},
        {"set_master_volume", lua_api_audio_set_master_volume},
        {"play", lua_api_audio_play},
        {"load_sound", lua_api_audio_load_sound},
        {"unload_sound", lua_api_audio_unload_sound},
        {"start", lua_api_audio_start_sound},
        {"stop", lua_api_audio_stop_sound},
        {"set_volume", lua_api_audio_set_sound_volume},
        {"set_pitch", lua_api_audio_set_sound_pitch},
        {"set_pan", lua_api_audio_set_sound_pan},
        {"set_looping", lua_api_audio_set_sound_looping},
        {"is_playing", lua_api_audio_is_sound_playing},
        {"at_end", lua_api_audio_sound_at_end},
        {"seek", lua_api_audio_seek_sound},
        {0},
    };
    static const luaL_Reg camera_methods[] = {
        {"get_position", lua_api_camera_get_position},
        {"set_position", lua_api_camera_set_position},
        {"get_pitch", lua_api_camera_get_pitch},
        {"set_pitch", lua_api_camera_set_pitch},
        {"get_yaw", lua_api_camera_get_yaw},
        {"set_yaw", lua_api_camera_set_yaw},
        {"look", lua_api_camera_look},
        {"move", lua_api_camera_move},
        {"forward", lua_api_camera_forward},
        {"flat_forward", lua_api_camera_flat_forward},
        {"right", lua_api_camera_right},
        {"view", lua_api_camera_view},
        {0},
    };

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
    luaL_newmetatable(g_lua_api.state, LUA_API_CAMERA_METATABLE);
    lua_pushvalue(g_lua_api.state, -1);
    lua_setfield(g_lua_api.state, -2, "__index");
    luaL_setfuncs(g_lua_api.state, camera_methods, 0);
    lua_pop(g_lua_api.state, 1);
    lua_pushcfunction(g_lua_api.state, lua_api_hello_world);
    lua_setglobal(g_lua_api.state, "hello_world");
    lua_api_register_module(g_lua_api.state, "window", rgfw_functions);
    lua_api_register_module(g_lua_api.state, "graphics", graphics_functions);
    lua_api_register_module(g_lua_api.state, "core", core_functions);
    lua_api_register_module(g_lua_api.state, "math", hmm_functions);
    lua_api_register_module(g_lua_api.state, "audio", audio_functions);

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
    status = lua_pcall(g_lua_api.state, 1, 0, 0);
    return lua_api_report_call_status(status);
}
