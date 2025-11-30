#pragma once

#include <stdint.h>
#include "romi_db.h"

typedef void (*RomiDownloadProgress)(const char* status, uint64_t downloaded, uint64_t total);

int romi_download_rom(const DbItem* item, RomiDownloadProgress progress);

void romi_download_cancel(void);

char* romi_http_download_buffer(const char* url, uint32_t* buf_size);
