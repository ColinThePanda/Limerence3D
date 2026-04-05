#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef ERROR
#undef ERROR
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#include "assets_runtime.h"
#include "lua_api.h"
#include "project_runtime.h"
#include "third_party/lua-5.5.0/src/lauxlib.h"
#include "third_party/lua-5.5.0/src/lualib.h"

#include "../generated/export_runner_blob.h"

#define DEFAULT_WINDOW_WIDTH 980
#define DEFAULT_WINDOW_HEIGHT 540

typedef struct {
    const char *project_root;
    const char *export_root;
    lua_State *compile_state;
} Lua_Compile_Context;

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

static bool copy_file_to_path(const char *src_path, const char *dst_path)
{
    if (!ensure_directory_tree(temp_dir_name(dst_path))) {
        return false;
    }

    return copy_file(src_path, dst_path);
}

static bool path_has_extension(const char *path, const char *extension)
{
    const char *ext = temp_file_ext(path);
    if (ext == NULL) return false;
    return strcmp(ext, extension) == 0;
}

static bool delete_path_recursive(const char *path)
{
    Nob_File_Type type;
    Nob_File_Paths children = {0};
    bool result = true;

    if (!file_exists(path)) {
        return true;
    }
    type = get_file_type(path);

    if (type == NOB_FILE_DIRECTORY) {
        if (!read_entire_dir(path, &children)) {
            nob_return_defer(false);
        }

        for (size_t i = 0; i < children.count; ++i) {
            char *child_path;

            if (strcmp(children.items[i], ".") == 0) continue;
            if (strcmp(children.items[i], "..") == 0) continue;

            child_path = copy_string(path_join(path, children.items[i]));
            if (child_path == NULL) {
                nob_return_defer(false);
            }

            if (!delete_path_recursive(child_path)) {
                free(child_path);
                nob_return_defer(false);
            }

            free(child_path);
        }
    }

    if (!delete_file(path)) {
        nob_return_defer(false);
    }

defer:
    da_free(children);
    return result;
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

static bool create_new_project(const char *project_root)
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
    fprintf(stream, "  %s export <project-dir>\n", program_name);
}

static int lua_dump_file_writer(lua_State *L, const void *p, size_t size, void *ud)
{
    FILE *file = (FILE *)ud;
    (void)L;

    if (size == 0) return 0;
    return fwrite(p, 1, size, file) == size ? 0 : 1;
}

static bool compile_lua_file_to_luac(lua_State *L, const char *src_path, const char *dst_path)
{
    FILE *file = NULL;
    int dump_status;

    if (!ensure_directory_tree(temp_dir_name(dst_path))) {
        fprintf(stderr, "failed to create output directory for %s\n", dst_path);
        return false;
    }

    if (luaL_loadfilex(L, src_path, NULL) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", src_path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    file = fopen(dst_path, "wb");
    if (file == NULL) {
        perror(dst_path);
        lua_pop(L, 1);
        return false;
    }

    dump_status = lua_dump(L, lua_dump_file_writer, file, 1);
    fclose(file);
    lua_pop(L, 1);

    if (dump_status != 0) {
        fprintf(stderr, "failed to write Lua bytecode: %s\n", dst_path);
        delete_file(dst_path);
        return false;
    }

    return true;
}

static const char *relative_to_root(const char *root, const char *path)
{
    size_t prefix_len = strlen(root);
    const char *relative = path;

    if (strncmp(path, root, prefix_len) == 0) {
        relative = path + prefix_len;
        while (is_path_sep(*relative)) {
            relative += 1;
        }
    }

    return relative;
}

static char *lua_source_to_luac_relative_path(const char *relative_path)
{
    size_t count = strlen(relative_path);
    size_t dot_index = count;
    char *result = NULL;

    for (size_t i = count; i > 0; --i) {
        char ch = relative_path[i - 1];
        if (ch == '.') {
            dot_index = i - 1;
            break;
        }
        if (is_path_sep(ch)) {
            break;
        }
    }

    result = (char *)malloc(dot_index + strlen(".luac") + 1);
    if (result == NULL) return NULL;

    memcpy(result, relative_path, dot_index);
    memcpy(result + dot_index, ".luac", strlen(".luac") + 1);
    normalize_slashes(result);
    return result;
}

static bool collect_project_lua_for_export(Walk_Entry entry)
{
    Lua_Compile_Context *context = entry.data;
    const char *relative;
    char *luac_relative = NULL;
    char *dst_path = NULL;
    bool ok = true;

    if (entry.type == NOB_FILE_DIRECTORY) {
        const char *name = path_name(entry.path);

        if (strcmp(name, "generated") == 0 || strcmp(name, ".limerence") == 0) {
            *entry.action = NOB_WALK_SKIP;
        }
        return true;
    }

    if (entry.type != NOB_FILE_REGULAR || !path_has_extension(entry.path, ".lua")) {
        return true;
    }

    relative = relative_to_root(context->project_root, entry.path);
    luac_relative = lua_source_to_luac_relative_path(relative);
    if (luac_relative == NULL) {
        return false;
    }

    dst_path = copy_string(path_join(context->export_root, luac_relative));
    if (dst_path == NULL) {
        free(luac_relative);
        return false;
    }

    ok = compile_lua_file_to_luac(context->compile_state, entry.path, dst_path);
    free(luac_relative);
    free(dst_path);
    return ok;
}

static bool ensure_project_pack_for_export(const char *project_root, char **pack_path_out)
{
    bool result = true;
    const char *pack_path = NULL;
    char *generated_root = NULL;
    char *empty_pack_path = NULL;

    *pack_path_out = NULL;

    if (!project_runtime_build_pack_if_needed(project_root, &pack_path)) {
        return false;
    }

    if (pack_path != NULL) {
        *pack_path_out = copy_string(pack_path);
        free((void *)pack_path);
        return *pack_path_out != NULL;
    }

    generated_root = copy_string(path_join(project_root, "generated"));
    empty_pack_path = copy_string(path_join(project_root, "generated/assets.pack"));
    if (generated_root == NULL || empty_pack_path == NULL) {
        nob_return_defer(false);
    }
    if (!mkdir_if_not_exists(generated_root)) {
        nob_return_defer(false);
    }
    {
        Assets_Error error = {0};
        if (assets_build_empty_pack(empty_pack_path, &error) != ASSETS_STATUS_OK) {
            if (error.source_path[0] != '\0') {
                fprintf(stderr, "%s: %s\n", error.source_path, error.message);
            } else {
                fprintf(stderr, "%s\n", error.message);
            }
            nob_return_defer(false);
        }
    }

    *pack_path_out = copy_string(empty_pack_path);

defer:
    free((void *)pack_path);
    free(generated_root);
    free(empty_pack_path);
    return result;
}

static bool write_export_runner_binary(const char *output_path)
{
    if (g_export_runner_blob_size == 0 || g_export_runner_blob == NULL) {
        fprintf(stderr, "this limerence build does not contain an embedded export runner\n");
        return false;
    }

    if (!ensure_directory_tree(temp_dir_name(output_path))) {
        return false;
    }
    if (!nob_write_entire_file(output_path, g_export_runner_blob, g_export_runner_blob_size)) {
        fprintf(stderr, "failed to write export runner: %s\n", output_path);
        return false;
    }

#ifndef _WIN32
    if (chmod(output_path, 0755) != 0) {
        perror(output_path);
        return false;
    }
#endif

    return true;
}

static bool export_project(const char *project_root)
{
    bool result = true;
    char *main_lua_path = copy_string(path_join(project_root, "main.lua"));
    char *generated_root = copy_string(path_join(project_root, "generated"));
    char *export_root = copy_string(path_join(project_root, "generated/export"));
    char *pack_path = NULL;
    char *pack_dst_path = NULL;
    char *runner_dst_path = NULL;
    lua_State *compile_state = NULL;
    Lua_Compile_Context compile_context = {0};

    if (main_lua_path == NULL || generated_root == NULL || export_root == NULL) {
        nob_return_defer(false);
    }

    if (!file_exists(main_lua_path)) {
        fprintf(stderr, "missing Lua entry point: %s\n", main_lua_path);
        nob_return_defer(false);
    }

    if (!ensure_project_pack_for_export(project_root, &pack_path)) {
        nob_return_defer(false);
    }

    if (!mkdir_if_not_exists(generated_root)) {
        nob_return_defer(false);
    }
    if (!delete_path_recursive(export_root)) {
        nob_return_defer(false);
    }
    if (!ensure_directory_tree(export_root)) {
        nob_return_defer(false);
    }

    pack_dst_path = copy_string(path_join(export_root, "assets.pack"));
#ifdef _WIN32
    runner_dst_path = copy_string(path_join(export_root, temp_sprintf("%s.exe", path_name(project_root))));
#else
    runner_dst_path = copy_string(path_join(export_root, path_name(project_root)));
#endif
    if (pack_dst_path == NULL || runner_dst_path == NULL) {
        nob_return_defer(false);
    }

    if (!write_export_runner_binary(runner_dst_path)) {
        nob_return_defer(false);
    }
    if (!copy_file_to_path(pack_path, pack_dst_path)) {
        nob_return_defer(false);
    }

    compile_state = luaL_newstate();
    if (compile_state == NULL) {
        fputs("failed to create Lua compiler state\n", stderr);
        nob_return_defer(false);
    }

    compile_context.project_root = project_root;
    compile_context.export_root = export_root;
    compile_context.compile_state = compile_state;
    if (!walk_dir(project_root, collect_project_lua_for_export, .data = &compile_context)) {
        nob_return_defer(false);
    }

    nob_log(INFO, "Exported project to %s", export_root);
    nob_log(INFO, "Runner: %s", runner_dst_path);

defer:
    if (compile_state != NULL) lua_close(compile_state);
    free(main_lua_path);
    free(generated_root);
    free(export_root);
    free(pack_path);
    free(pack_dst_path);
    free(runner_dst_path);
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

        if (!create_new_project(project_root)) {
            goto defer;
        }

        exit_code = 0;
        goto defer;
    }

    if (strcmp(argv[1], "export") == 0) {
        if (argc != 3) {
            print_usage(stderr, argv[0]);
            goto defer;
        }

        project_root = copy_string(resolve_path(repo_root, argv[2]));
        if (project_root == NULL) {
            goto defer;
        }

        if (!export_project(project_root)) {
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

    if (!project_runtime_run_dev(repo_root, project_root)) {
        goto defer;
    }

    exit_code = 0;

defer:
    free(project_main_lua);
    free(project_root);
    free(repo_root);
    return exit_code;
}
