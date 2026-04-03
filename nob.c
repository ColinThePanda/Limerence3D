#if defined(_WIN32) && defined(__GNUC__)
#define nob_cc(cmd) nob_cmd_append((cmd), "gcc")
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#define FLAG_IMPLEMENTATION
#include "third_party/flag.h"

#define ASSETS_IMPLEMENTATION
#include "assets.h"

typedef struct {
    const char *source_path;
    const char *relative_path;
    Assets_Model_Options options;
} Model_Asset_Spec;

typedef struct {
    const char *source_path;
    const char *relative_path;
    Assets_Image_Options options;
} Image_Asset_Spec;

typedef struct {
    const char *source_path;
    const char *relative_path;
    Assets_Font_Options options;
} Font_Asset_Spec;

typedef struct {
    Nob_File_Paths *expected_outputs;
} Asset_Discovery_Context;

static Cmd cmd = {0};

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

static void begin_cc(Cmd *cmd)
{
    cmd->count = 0;
    nob_cc(cmd);
    nob_cc_flags(cmd);
    cmd_append(cmd, "-I.");
}

static const char *generated_asset_output_path(const char *symbol_name)
{
    return temp_sprintf("assets/generated/%s.asset.c", symbol_name);
}

static void append_asset_dependencies(Nob_File_Paths *inputs, const char *source_path)
{
    da_append(inputs, source_path);
    da_append(inputs, "nob.c");
    da_append(inputs, "assets.h");
    da_append(inputs, "third_party/nob.h");
    da_append(inputs, "third_party/stb_image.h");
    da_append(inputs, "third_party/stb_truetype.h");
}

static bool expected_output_contains(const Nob_File_Paths *expected_outputs, const char *path)
{
    const char *name = path_name(path);
    for (size_t i = 0; i < expected_outputs->count; ++i) {
        if (strcmp(path_name(expected_outputs->items[i]), name) == 0) {
            return true;
        }
    }
    return false;
}

static bool cleanup_generated_asset(Walk_Entry entry)
{
    Nob_File_Paths *expected_outputs = entry.data;

    if (entry.type == NOB_FILE_DIRECTORY) {
        if (entry.level > 0) *entry.action = NOB_WALK_SKIP;
        return true;
    }

    if (entry.type != NOB_FILE_REGULAR) {
        return true;
    }

    if (!sv_end_with(sv_from_cstr(entry.path), ".asset.c")) {
        return true;
    }

    if (expected_output_contains(expected_outputs, entry.path)) {
        return true;
    }

    nob_log(INFO, "Removing stale generated asset %s", path_name(entry.path));
    return delete_file(entry.path);
}

static bool cleanup_generated_assets(const Nob_File_Paths *expected_outputs)
{
    if (!file_exists("assets/generated")) {
        return true;
    }

    return walk_dir("assets/generated", cleanup_generated_asset, .data = (void *)expected_outputs);
}

static bool build_model_asset(const Model_Asset_Spec *spec, Nob_File_Paths *expected_outputs)
{
    Assets_Error error = {0};
    Nob_File_Paths inputs = {0};
    char symbol_name[256] = {0};
    const char *source_ext = temp_file_ext(spec->source_path);
    const char *output_path = NULL;
    bool result = true;

    if (source_ext == NULL || strcmp(source_ext, ".obj") != 0) {
        nob_log(ERROR, "Expected OBJ asset, got %s", spec->source_path);
        return false;
    }

    assets_make_symbol_name(symbol_name, sizeof(symbol_name), "assets_model", spec->relative_path);
    output_path = generated_asset_output_path(symbol_name);

    da_append(expected_outputs, temp_strdup(output_path));
    if (!mkdir_if_not_exists(temp_dir_name(output_path))) {
        return false;
    }

    append_asset_dependencies(&inputs, spec->source_path);

    int rebuild = needs_rebuild(output_path, inputs.items, inputs.count);
    if (rebuild < 0) nob_return_defer(false);
    if (rebuild == 0) nob_return_defer(true);

    nob_log(INFO, "Generating %s -> %s", path_name(spec->source_path), output_path);
    if (assets_compile_model_to_c(spec->source_path, output_path, symbol_name, &spec->options, &error) != ASSETS_STATUS_OK) {
        log_assets_error(&error);
        nob_return_defer(false);
    }

defer:
    da_free(inputs);
    return result;
}

static bool build_image_asset(const Image_Asset_Spec *spec, Nob_File_Paths *expected_outputs)
{
    Assets_Error error = {0};
    Nob_File_Paths inputs = {0};
    char symbol_name[256] = {0};
    const char *source_ext = temp_file_ext(spec->source_path);
    const char *output_path = NULL;
    bool result = true;

    if (source_ext == NULL || (strcmp(source_ext, ".png") != 0 && strcmp(source_ext, ".jpg") != 0 && strcmp(source_ext, ".jpeg") != 0)) {
        nob_log(ERROR, "Expected image asset, got %s", spec->source_path);
        return false;
    }

    assets_make_symbol_name(symbol_name, sizeof(symbol_name), "assets_image", spec->relative_path);
    output_path = generated_asset_output_path(symbol_name);

    da_append(expected_outputs, temp_strdup(output_path));
    if (!mkdir_if_not_exists(temp_dir_name(output_path))) {
        return false;
    }

    append_asset_dependencies(&inputs, spec->source_path);

    int rebuild = needs_rebuild(output_path, inputs.items, inputs.count);
    if (rebuild < 0) nob_return_defer(false);
    if (rebuild == 0) nob_return_defer(true);

    nob_log(INFO, "Generating %s -> %s", path_name(spec->source_path), output_path);
    if (assets_compile_image_to_c(spec->source_path, output_path, symbol_name, &spec->options, &error) != ASSETS_STATUS_OK) {
        log_assets_error(&error);
        nob_return_defer(false);
    }

defer:
    da_free(inputs);
    return result;
}

static bool build_font_asset(const Font_Asset_Spec *spec, Nob_File_Paths *expected_outputs)
{
    Assets_Error error = {0};
    Nob_File_Paths inputs = {0};
    char symbol_name[256] = {0};
    const char *source_ext = temp_file_ext(spec->source_path);
    const char *output_path = NULL;
    bool result = true;

    if (source_ext == NULL || strcmp(source_ext, ".ttf") != 0) {
        nob_log(ERROR, "Expected TTF font asset, got %s", spec->source_path);
        return false;
    }

    assets_make_symbol_name(symbol_name, sizeof(symbol_name), "assets_font", spec->relative_path);
    output_path = generated_asset_output_path(symbol_name);

    da_append(expected_outputs, temp_strdup(output_path));
    if (!mkdir_if_not_exists(temp_dir_name(output_path))) {
        return false;
    }

    append_asset_dependencies(&inputs, spec->source_path);

    int rebuild = needs_rebuild(output_path, inputs.items, inputs.count);
    if (rebuild < 0) nob_return_defer(false);
    if (rebuild == 0) nob_return_defer(true);

    nob_log(INFO, "Generating %s -> %s", path_name(spec->source_path), output_path);
    if (assets_compile_font_to_c(spec->source_path, output_path, symbol_name, &spec->options, &error) != ASSETS_STATUS_OK) {
        log_assets_error(&error);
        nob_return_defer(false);
    }

defer:
    da_free(inputs);
    return result;
}

static const char *asset_relative_path(const char *path)
{
    if (strncmp(path, "assets/", 7) == 0 || strncmp(path, "assets\\", 7) == 0) {
        return path + 7;
    }

    return path;
}

static Assets_Model_Options default_model_options_for_path(const char *relative_path)
{
    Assets_Model_Options options = {0};
    options.scale = 1.0f;

    if (strcmp(relative_path, "utahTeapot.obj") == 0) {
        options.scale = 0.4f;
    }

    return options;
}

static Assets_Image_Options default_image_options(void)
{
    Assets_Image_Options options = {0};
    options.force_rgba = 1;
    return options;
}

static Assets_Font_Options default_font_options(void)
{
    Assets_Font_Options options = {0};
    options.pixel_height = 32.0f;
    options.first_codepoint = 32;
    options.codepoint_count = 95;
    options.atlas_width = 512;
    options.atlas_height = 512;
    return options;
}

static bool build_asset_from_path(const char *source_path, const char *relative_path, Nob_File_Paths *expected_outputs)
{
    const char *ext = temp_file_ext(source_path);

    if (ext == NULL) {
        return true;
    }

    if (strcmp(ext, ".obj") == 0) {
        Model_Asset_Spec spec = {
            .source_path = source_path,
            .relative_path = relative_path,
            .options = default_model_options_for_path(relative_path),
        };
        return build_model_asset(&spec, expected_outputs);
    }

    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        Image_Asset_Spec spec = {
            .source_path = source_path,
            .relative_path = relative_path,
            .options = default_image_options(),
        };
        return build_image_asset(&spec, expected_outputs);
    }

    if (strcmp(ext, ".ttf") == 0) {
        Font_Asset_Spec spec = {
            .source_path = source_path,
            .relative_path = relative_path,
            .options = default_font_options(),
        };
        return build_font_asset(&spec, expected_outputs);
    }

    return true;
}

static bool discover_and_build_asset(Walk_Entry entry)
{
    Asset_Discovery_Context *context = entry.data;

    if (entry.type == NOB_FILE_DIRECTORY) {
        if (strcmp(path_name(entry.path), "generated") == 0) {
            *entry.action = NOB_WALK_SKIP;
        }
        return true;
    }

    if (entry.type != NOB_FILE_REGULAR) {
        return true;
    }

    return build_asset_from_path(entry.path, asset_relative_path(entry.path), context->expected_outputs);
}

static bool convert_assets(void)
{
    bool result = true;
    size_t temp_mark = temp_save();
    Nob_File_Paths expected_outputs = {0};
    Asset_Discovery_Context context = {
        .expected_outputs = &expected_outputs,
    };

    if (!mkdir_if_not_exists("assets/generated")) nob_return_defer(false);

    if (!walk_dir("assets", discover_and_build_asset, .data = &context)) {
        nob_return_defer(false);
    }

    if (!cleanup_generated_assets(&expected_outputs)) {
        nob_return_defer(false);
    }

defer:
    da_free(expected_outputs);
    temp_rewind(temp_mark);
    return result;
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
        cmd_append(&cmd, "-O3", "-DNDEBUG", "-march=native", "-ffast-math", "-flto", "-fno-strict-aliasing");
    } else {
        cmd_append(&cmd, "-ggdb");
    }

    nob_cc_output(&cmd, "main");
    nob_cc_inputs(&cmd, "main.c", "core.c");
    append_platform_libraries(&cmd);
    return cmd_run(&cmd);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(
        argc,
        argv,
        "main.c",
        "assets.h",
        "third_party/nob.h",
        "third_party/stb_image.h",
        "third_party/stb_truetype.h",
        "third_party/RGFW.h",
        "third_party/olive.c",
        "third_party/HandmadeMath.h"
    );

    bool run = false;
    bool release = false;

    flag_bool_var(&run, "run", false, "Run the built executable after a successful build.");
    flag_bool_var(&release, "-release", false, "Build with release optimizations.");
    if (!flag_parse(argc, argv)) {
        flag_print_error(stderr);
        fprintf(stderr, "\n");
        flag_print_options(stderr);
        return 1;
    }

    if (flag_rest_argc() > 0) {
        fprintf(stderr, "ERROR: unexpected positional arguments\n");
        flag_print_options(stderr);
        return 1;
    }

    if (!convert_assets()) return 1;
    if (!build_main(release)) return 1;

    if (run) {
#ifdef _WIN32
        cmd.count = 0;
        cmd_append(&cmd, "main.exe");
#else
        cmd.count = 0;
        cmd_append(&cmd, "./main");
#endif
        if (!cmd_run(&cmd)) return 1;
    }

    return 0;
}
