#pragma once

#include <stdint.h>
#include "romi_db.h"

typedef enum {
    StorageOK = 0,
    StorageErrorDownload,
    StorageErrorExtract,
    StorageErrorDisk,
    StorageErrorMemory,
    StorageCancelled,
} RomiStorageResult;

typedef void (*RomiStorageProgress)(const char* status, float percent);

RomiStorageResult romi_storage_download(const DbItem* item, RomiStorageProgress progress);

int romi_storage_check_presence(DbItem* item);

void romi_storage_scan_installed(void);

void romi_storage_cancel(void);

const char* romi_storage_error_string(RomiStorageResult result);

const char* romi_storage_get_install_path(RomiPlatform platform, const char* filename);
