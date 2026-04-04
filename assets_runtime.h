#ifndef ASSETS_RUNTIME_H_
#define ASSETS_RUNTIME_H_

#include "assets.h"

typedef enum {
    ASSETS_RUNTIME_TYPE_MODEL = 1,
    ASSETS_RUNTIME_TYPE_IMAGE = 2,
    ASSETS_RUNTIME_TYPE_FONT = 3,
} Assets_Runtime_Type;

typedef struct {
    const char *name;
    Assets_Model asset;
} Assets_Runtime_Model_Entry;

typedef struct {
    const char *name;
    Assets_Image asset;
} Assets_Runtime_Image_Entry;

typedef struct {
    const char *name;
    Assets_Font asset;
} Assets_Runtime_Font_Entry;

typedef struct {
    unsigned char *pack_data;
    size_t pack_size;
    Assets_Runtime_Model_Entry *models;
    size_t model_count;
    Assets_Runtime_Image_Entry *images;
    size_t image_count;
    Assets_Runtime_Font_Entry *fonts;
    size_t font_count;
} Assets_Runtime_Registry;

Assets_Status assets_build_pack_from_dir(const char *assets_dir, const char *output_path, Assets_Error *error);
Assets_Status assets_runtime_load_pack(const char *path, Assets_Runtime_Registry *out, Assets_Error *error);
void assets_runtime_unload(Assets_Runtime_Registry *registry);
const Assets_Model *assets_runtime_find_model(const Assets_Runtime_Registry *registry, const char *name);
const Assets_Image *assets_runtime_find_image(const Assets_Runtime_Registry *registry, const char *name);
const Assets_Font *assets_runtime_find_font(const Assets_Runtime_Registry *registry, const char *name);

#endif
