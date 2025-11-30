#pragma once

#include <stdint.h>
#include "romi_db.h"

typedef enum {
    ExtractOK = 0,
    ExtractErrorOpen,
    ExtractErrorRead,
    ExtractErrorFormat,
    ExtractErrorMemory,
    ExtractErrorWrite,
    ExtractErrorDecompress,
    ExtractCancelled,
} RomiExtractResult;

typedef void (*RomiExtractProgress)(const char* filename, uint64_t extracted, uint64_t total);

RomiExtractResult romi_extract_zip(const char* zip_path, const char* dest_folder, RomiExtractProgress progress);

const char* romi_extract_error_string(RomiExtractResult result);

int romi_is_zip_file(const char* path);

void romi_extract_cancel(void);
