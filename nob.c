#if defined(_WIN32) && defined(__GNUC__)
#define nob_cc(cmd) nob_cmd_append((cmd), "gcc")
#endif

#ifdef ERROR
#undef ERROR
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#define FLAG_IMPLEMENTATION
#include "third_party/flag.h"

static Cmd cmd = {0};
static const char *lua_sources[] = {
    "third_party/lua-5.5.0/src/lapi.c",
    "third_party/lua-5.5.0/src/lauxlib.c",
    "third_party/lua-5.5.0/src/lbaselib.c",
    "third_party/lua-5.5.0/src/lcode.c",
    "third_party/lua-5.5.0/src/lcorolib.c",
    "third_party/lua-5.5.0/src/lctype.c",
    "third_party/lua-5.5.0/src/ldblib.c",
    "third_party/lua-5.5.0/src/ldebug.c",
    "third_party/lua-5.5.0/src/ldo.c",
    "third_party/lua-5.5.0/src/ldump.c",
    "third_party/lua-5.5.0/src/lfunc.c",
    "third_party/lua-5.5.0/src/lgc.c",
    "third_party/lua-5.5.0/src/linit.c",
    "third_party/lua-5.5.0/src/liolib.c",
    "third_party/lua-5.5.0/src/llex.c",
    "third_party/lua-5.5.0/src/lmathlib.c",
    "third_party/lua-5.5.0/src/lmem.c",
    "third_party/lua-5.5.0/src/loadlib.c",
    "third_party/lua-5.5.0/src/lobject.c",
    "third_party/lua-5.5.0/src/lopcodes.c",
    "third_party/lua-5.5.0/src/loslib.c",
    "third_party/lua-5.5.0/src/lparser.c",
    "third_party/lua-5.5.0/src/lstate.c",
    "third_party/lua-5.5.0/src/lstring.c",
    "third_party/lua-5.5.0/src/lstrlib.c",
    "third_party/lua-5.5.0/src/ltable.c",
    "third_party/lua-5.5.0/src/ltablib.c",
    "third_party/lua-5.5.0/src/ltm.c",
    "third_party/lua-5.5.0/src/lundump.c",
    "third_party/lua-5.5.0/src/lutf8lib.c",
    "third_party/lua-5.5.0/src/lvm.c",
    "third_party/lua-5.5.0/src/lzio.c",
};

static void begin_cc(Cmd *cmd)
{
    cmd->count = 0;
    nob_cc(cmd);
    nob_cc_flags(cmd);
    cmd_append(cmd, "-I.");
    cmd_append(cmd, "-Ithird_party/lua-5.5.0/src");
}

static void append_platform_libraries(Cmd *cmd)
{
#ifdef _WIN32
    cmd_append(cmd, "-lgdi32", "-lwinmm", "-lshell32", "-luser32");
#elif defined(__linux__)
    cmd_append(cmd, "-lX11", "-lXi", "-lXrandr", "-lm");
#elif defined(__APPLE__)
    cmd_append(cmd, "-framework", "Cocoa");
    cmd_append(cmd, "-framework", "CoreVideo");
    cmd_append(cmd, "-framework", "IOKit");
#else
    nob_log(ERROR, "platform currently not supported");
#endif
}

static bool build_main(bool release)
{
    begin_cc(&cmd);
    cmd_append(&cmd, "-Wno-missing-braces");

    if (release) {
        cmd_append(&cmd, "-O3", "-DNDEBUG", "-ffast-math", "-flto", "-fno-strict-aliasing");
    } else {
        cmd_append(&cmd, "-O1", "-ggdb");
    }

    nob_cc_output(&cmd, "limerence");
    nob_cc_inputs(&cmd, "src/main.c", "src/core.c", "src/lua_api.c", "src/assets_runtime.c");
    for (size_t i = 0; i < NOB_ARRAY_LEN(lua_sources); ++i) {
        cmd_append(&cmd, lua_sources[i]);
    }
    append_platform_libraries(&cmd);
    return cmd_run(&cmd);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(
        argc,
        argv,
        "src/main.c",
        "src/assets.h",
        "src/assets_runtime.c",
        "src/assets_runtime.h",
        "src/lua_api.c",
        "src/lua_api.h",
        "third_party/nob.h",
        "third_party/stb_image.h",
        "third_party/stb_truetype.h",
        "third_party/RGFW.h",
        "third_party/olive.c",
        "third_party/HandmadeMath.h",
        "third_party/lua-5.5.0/src/lua.h",
        "third_party/lua-5.5.0/src/lauxlib.h",
        "third_party/lua-5.5.0/src/lualib.h"
    );

    bool release = false;

    flag_bool_var(&release, "-release", false, "Build with release optimizations.");
    if (!flag_parse(argc, argv)) {
        flag_print_error(stderr);
        fprintf(stderr, "\n");
        flag_print_options(stderr);
        return 1;
    }

    if (flag_rest_argc() > 0) {
        nob_log(ERROR, "unexpected positional arguments");
        flag_print_options(stderr);
        return 1;
    }

    if (!build_main(release)) return 1;

    return 0;
}
