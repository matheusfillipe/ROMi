#include "romi_download.h"
#include "romi_storage.h"
#include "romi_extract.h"
#include "romi.h"
#include "romi_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cancelled = 0;
static RomiDownloadProgress current_progress = NULL;
static uint64_t download_total = 0;
static uint64_t download_current = 0;

static size_t write_file_callback(void* buffer, size_t size, size_t nmemb, void* stream)
{
    void* fp = stream;
    size_t realsize = size * nmemb;

    if (romi_write(fp, buffer, realsize))
    {
        download_current += realsize;
        return realsize;
    }

    return 0;
}

static int progress_callback(void* p, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow)
{
    ROMI_UNUSED(p);
    ROMI_UNUSED(ultotal);
    ROMI_UNUSED(ulnow);

    if (cancelled)
        return 1;

    if (current_progress && dltotal > 0)
        current_progress("Downloading...", dlnow, dltotal);

    return 0;
}

static const char* get_filename_from_url(const char* url)
{
    const char* slash = romi_strrchr(url, '/');
    return slash ? (slash + 1) : url;
}

int romi_download_rom(const DbItem* item, RomiDownloadProgress progress)
{
    if (!item || !item->url)
        return 0;

    cancelled = 0;
    current_progress = progress;
    download_current = 0;
    download_total = 0;

    const char* dest_folder = romi_platform_folder(item->platform);
    const char* temp_folder = romi_get_temp_folder();
    const char* filename = get_filename_from_url(item->url);

    char temp_path[512];
    romi_snprintf(temp_path, sizeof(temp_path), "%s/%s", temp_folder, filename);

    LOG("downloading %s to %s", item->url, temp_path);

    if (progress)
        progress("Connecting...", 0, 0);

    romi_http* http = romi_http_get(item->url, NULL, 0);
    if (!http)
    {
        LOG("failed to connect to %s", item->url);
        return 0;
    }

    int64_t content_length;
    if (!romi_http_response_length(http, &content_length))
    {
        LOG("failed to get content length");
        romi_http_close(http);
        return 0;
    }

    download_total = content_length > 0 ? (uint64_t)content_length : 0;

    if (content_length > 0 && !romi_check_free_space(content_length * 2))
    {
        LOG("not enough disk space for %lld bytes", content_length);
        romi_http_close(http);
        return 0;
    }

    romi_mkdirs(temp_folder);
    void* fp = romi_create(temp_path);
    if (!fp)
    {
        LOG("failed to create temp file %s", temp_path);
        romi_http_close(http);
        return 0;
    }

    int success = romi_http_read(http, &write_file_callback, fp, &progress_callback);

    romi_close(fp);
    romi_http_close(http);

    if (!success || cancelled)
    {
        LOG("download failed or cancelled");
        romi_rm(temp_path);
        return 0;
    }

    LOG("download complete: %s (%lld bytes)", temp_path, romi_get_size(temp_path));

    romi_mkdirs(dest_folder);

    int result = 1;

    if (romi_is_zip_file(temp_path))
    {
        if (progress)
            progress("Extracting...", 0, 0);

        RomiExtractResult extract_result = romi_extract_zip(temp_path, dest_folder, NULL);

        if (extract_result != ExtractOK)
        {
            LOG("extraction failed: %s", romi_extract_error_string(extract_result));
            result = 0;
        }

        romi_rm(temp_path);
    }
    else
    {
        char dest_path[512];
        romi_snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_folder, filename);

        LOG("moving %s -> %s", temp_path, dest_path);

        if (rename(temp_path, dest_path) != 0)
        {
            LOG("failed to move file to destination");
            romi_rm(temp_path);
            result = 0;
        }
    }

    if (progress && result)
        progress("Complete!", download_total, download_total);

    current_progress = NULL;
    return result;
}

void romi_download_cancel(void)
{
    cancelled = 1;
    romi_extract_cancel();
}
