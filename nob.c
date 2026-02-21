#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

Cmd cmd = {};

void append_basic() {
	cmd_append(&cmd, "gcc");
	cmd_append(&cmd, "-Wall");
	cmd_append(&cmd, "-Wextra");
}

bool convert_assets() {
	cmd.count = 0;
	append_basic();
	cmd_append(&cmd, "-o", "obj2c");
	cmd_append(&cmd, "obj2c.c");

	if (!cmd_run(&cmd)) { nob_log(ERROR, "Error compiling asset converter"); return false; }
	
	cmd.count = 0;

	Nob_File_Paths paths = {0};
	if (!nob_read_entire_dir("./assets", &paths)) { nob_log(ERROR, "Assets dir does not exist"); return false; }

	for (size_t i = 0; i < paths.count; i++) {
    		if (strstr(paths.items[i], ".obj")) {
        		nob_log(INFO, "Found .obj file: %s\n", paths.items[i]);

			#ifdef _WIN32
			cmd_append(&cmd, "obj2c.exe");
			#else
			cmd_append(&cmd, "./obj2c");
			#endif

			char full_input_path[512];
        		snprintf(full_input_path, sizeof(full_input_path), "%s/%s", "./assets", paths.items[i]);

			char c_file[256];
       			snprintf(c_file, sizeof(c_file), "%s", full_input_path);
        
        		char *dot = strrchr(c_file, '.');
        		if (dot) {
        		    strcpy(dot, ".c");
        		}
			
			cmd_append(&cmd, "-o", c_file);
			cmd_append(&cmd, "-s", "0.4");
			cmd_append(&cmd, full_input_path);

			if (!cmd_run(&cmd)) { nob_log(ERROR, "Could not convert assets"); return false; }
		}
	}
	return true;
}

void link_libraries() {
	#ifdef _WIN32
	cmd_append(&cmd, "-lgdi32");
	cmd_append(&cmd, "-lwinmm");
	cmd_append(&cmd, "-lshell32");
	cmd_append(&cmd, "-luser32");
	#elif defined(__linux__)
	cmd_append(&cmd, "-lX11");
    cmd_append(&cmd, "-lXi");
	cmd_append(&cmd, "-lXrandr");
    cmd_append(&cmd, "-lm");
	#elif defined(__APPLE__)
	cmd_append(&cmd, "-framework", "Cocoa");
	cmd_append(&cmd, "-framework", "CoreVideo");
	cmd_append(&cmd, "-framework", "IOKit");
	#else
	nob_log(ERROR, "platform currently not supported");
	#endif
}

int main(int argc, char **argv) {
	NOB_GO_REBUILD_URSELF(argc, argv);
	
	bool run = false;
	bool release = false;
	
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-run") == 0) {
			run = true;
		} else if (strcmp(argv[i], "--release") == 0) {
			release = true;
		}
	}

	if (!convert_assets()) return 1;
	
	append_basic();
	cmd_append(&cmd, "-Wno-missing-braces");
	cmd_append(&cmd, "-ggdb");
	
	if (release) {
		cmd_append(&cmd, "-O3");
	} else {
		cmd_append(&cmd, "-O0");
	}
	
	cmd_append(&cmd, "-o", "main");
	cmd_append(&cmd, "main.c");
	link_libraries();
	
	if (!cmd_run(&cmd)) return 1;
	
	if (run) {
		#ifdef _WIN32
		cmd_append(&cmd, "main.exe");
		#else
		cmd_append(&cmd, "./main");
		#endif
		if (!cmd_run(&cmd)) return 1;
	}
	
	return 0;
}
