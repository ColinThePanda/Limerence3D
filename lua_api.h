#ifndef LUA_API_H_
#define LUA_API_H_

#include <stdbool.h>

#include "assets_runtime.h"
#include "core.h"

typedef struct RGFW_window RGFW_window;

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
bool lua_api_call_global0(const char *name);
bool lua_api_call_global1_number(const char *name, double value);
void lua_api_set_mouse_vector(float x, float y);

#endif
