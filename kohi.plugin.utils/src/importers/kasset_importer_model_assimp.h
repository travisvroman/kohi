#pragma once

#include "defines.h"

#define KASSET_EXPORTER_TYPE_KOHI_IMPORTER 0x00000001
#define KASSET_EXPORTER_TYPE_KOHI_IMPORTER_VERSION 0x01

KAPI b8 kasset_model_assimp_import (const char *source_path, const char *target_path, const char *material_target_dir, const char *package_name);
