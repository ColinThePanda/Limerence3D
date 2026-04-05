#ifndef PROJECT_RUNTIME_H_
#define PROJECT_RUNTIME_H_

#include <stdbool.h>

bool project_runtime_build_pack_if_needed(const char *project_root, const char **pack_path_out);
bool project_runtime_run_dev(const char *repo_root, const char *project_root);
bool project_runtime_run_export(const char *bundle_root);

#endif
