#ifndef LUA_API_H_
#define LUA_API_H_

#include <stdbool.h>

#include "core.h"

typedef struct RGFW_window RGFW_window;

typedef struct {
    RGFW_window *window;
    Olivec_Canvas canvas;
    float *zbuffer;
} Lua_API_Context;

bool lua_api_init(const Lua_API_Context *context);
void lua_api_shutdown(void);
bool lua_api_run_string(const char *chunk);

#endif
