#ifdef ERROR
#undef ERROR
#endif

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "third_party/nob.h"

#include "project_runtime.h"

int main(void)
{
    const char *exe_path = temp_running_executable_path();
    const char *bundle_root = temp_dir_name(exe_path);

    if (exe_path == NULL || bundle_root == NULL) {
        nob_log(ERROR, "failed to resolve export runner path");
        return 1;
    }
    if (!set_current_dir(bundle_root)) {
        nob_log(ERROR, "failed to set current directory to %s", bundle_root);
        return 1;
    }
    if (!project_runtime_run_export(bundle_root)) {
        return 1;
    }

    return 0;
}
