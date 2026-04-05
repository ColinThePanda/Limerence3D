#ifndef LUA_API_H_
#define LUA_API_H_

#include <stdbool.h>
#include <stddef.h>

#include "assets_runtime.h"
#include "core.h"
#include "third_party/lua-5.5.0/src/lua.h"

typedef struct RGFW_window RGFW_window;

typedef struct {
    const char *name;
    lua_CFunction function;
    const char *stub;
} Lua_API_Function_Def;

typedef struct {
    const char *module_name;
    const char *class_name;
    const Lua_API_Function_Def *functions;
    size_t function_count;
    const char *const *constants;
    size_t constant_count;
} Lua_API_Module_Def;

typedef struct {
    const char *class_name;
    const Lua_API_Function_Def *methods;
    size_t method_count;
} Lua_API_Class_Def;

typedef struct {
    const char *name;
    lua_CFunction function;
    const char *stub;
} Lua_API_Global_Def;

typedef struct {
    RGFW_window *window;
    Olivec_Canvas canvas;
    float *zbuffer;
    Assets_Runtime_Registry *assets;
} Lua_API_Context;

bool lua_api_init(const Lua_API_Context *context);
void lua_api_shutdown(void);
bool lua_api_run_file(const char *path);
bool lua_api_run_string(const char *chunk);
bool lua_api_set_package_path(const char *path);
bool lua_api_call_global0(const char *name);
bool lua_api_call_global1_number(const char *name, double value);
void lua_api_set_mouse_vector(float x, float y);
const char *lua_api_stub_prelude(void);
size_t lua_api_module_count(void);
const Lua_API_Module_Def *lua_api_module_def(size_t index);
size_t lua_api_class_count(void);
const Lua_API_Class_Def *lua_api_class_def(size_t index);
size_t lua_api_global_count(void);
const Lua_API_Global_Def *lua_api_global_def(size_t index);

#endif
