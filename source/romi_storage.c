#include "romi_storage.h"
#include "romi_extract.h"
#include "romi.h"
#include "romi_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#define DOWNLOAD_BUFFER_SIZE (128 * 1024)

static int cancelled = 0;
static RomiStorageProgress current_progress = NULL;

static void extract_progress(const char* filename, uint64_t extracted, uint64_t total)
{
    if (current_progress)
    {
        float percent = total > 0 ? (float)extracted / (float)total : 0.0f;
        current_progress(filename ? filename : "Extracting...", 0.5f + percent * 0.5f);
    }
}

static size_t write_file_data(void* buffer, size_t size, size_t nmemb, void* stream)
{
    void* fp = stream;
    size_t realsize = size * nmemb;

    if (romi_write(fp, buffer, realsize))
        return realsize;

    return 0;
}

static int update_download_progress(void* p, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow)
{
    ROMI_UNUSED(p);
    ROMI_UNUSED(ultotal);
    ROMI_UNUSED(ulnow);

    if (cancelled)
        return 1;

    if (current_progress && dltotal > 0)
    {
        float percent = (float)dlnow / (float)dltotal;
        current_progress("Downloading...", percent * 0.5f);
    }

    return 0;
}

static char* get_filename_from_url(const char* url)
{
    const char* slash = romi_strrchr(url, '/');
    if (slash)
        return (char*)(slash + 1);
    return (char*)url;
}

RomiStorageResult romi_storage_download(const DbItem* item, RomiStorageProgress progress)
{
    if (!item || !item->url)
        return StorageErrorDownload;

    cancelled = 0;
    current_progress = progress;

    const char* dest_folder = romi_platform_folder(item->platform);
    const char* temp_folder = romi_get_temp_folder();

    char* url_filename = get_filename_from_url(item->url);
    char temp_path[512];
    romi_snprintf(temp_path, sizeof(temp_path), "%s/%s", temp_folder, url_filename);

    LOG("downloading %s to %s", item->url, temp_path);

    if (progress)
        progress("Connecting...", 0.0f);

    romi_http* http = romi_http_get(item->url, NULL, 0);
    if (!http)
    {
        LOG("failed to connect to %s", item->url);
        return StorageErrorDownload;
    }

    int64_t content_length;
    if (!romi_http_response_length(http, &content_length))
    {
        LOG("failed to get content length");
        romi_http_close(http);
        return StorageErrorDownload;
    }

    if (content_length > 0 && !romi_check_free_space(content_length * 2))
    {
        LOG("not enough disk space");
        romi_http_close(http);
        return StorageErrorDisk;
    }

    romi_mkdirs(temp_folder);
    void* fp = romi_create(temp_path);
    if (!fp)
    {
        LOG("failed to create temp file %s", temp_path);
        romi_http_close(http);
        return StorageErrorDisk;
    }

    if (!romi_http_read(http, &write_file_data, fp, &update_download_progress))
    {
        LOG("download failed");
        romi_close(fp);
        romi_rm(temp_path);
        romi_http_close(http);
        return cancelled ? StorageCancelled : StorageErrorDownload;
    }

    romi_close(fp);
    romi_http_close(http);

    if (cancelled)
    {
        romi_rm(temp_path);
        return StorageCancelled;
    }

    LOG("download complete, size = %lld", romi_get_size(temp_path));

    romi_mkdirs(dest_folder);

    RomiStorageResult result = StorageOK;

    if (romi_is_zip_file(temp_path))
    {
        if (progress)
            progress("Extracting...", 0.5f);

        RomiExtractResult extract_result = romi_extract_zip(temp_path, dest_folder, extract_progress);

        if (extract_result != ExtractOK)
        {
            LOG("extraction failed: %s", romi_extract_error_string(extract_result));
            result = (extract_result == ExtractCancelled) ? StorageCancelled : StorageErrorExtract;
        }

        romi_rm(temp_path);
    }
    else
    {
        char dest_path[512];
        char base_name[256];
        romi_strncpy(base_name, sizeof(base_name), url_filename);

        romi_snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_folder, base_name);

        LOG("moving %s to %s", temp_path, dest_path);

        if (rename(temp_path, dest_path) != 0)
        {
            LOG("failed to move file");
            romi_rm(temp_path);
            result = StorageErrorDisk;
        }
    }

    if (progress && result == StorageOK)
        progress("Complete!", 1.0f);

    current_progress = NULL;
    return result;
}

int romi_storage_check_presence(DbItem* item)
{
    if (!item)
        return 0;

    const char* folder = romi_platform_folder(item->platform);
    struct stat sb;

    if (stat(folder, &sb) != 0 || !S_ISDIR(sb.st_mode))
    {
        item->presence = PresenceMissing;
        return 0;
    }

    char search_name[256];
    romi_strncpy(search_name, sizeof(search_name), item->name);

    for (char* p = search_name; *p; p++)
    {
        if (*p == ':' || *p == '?' || *p == '*' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
            *p = '_';
    }

    DIR* dir = opendir(folder);
    if (!dir)
    {
        item->presence = PresenceMissing;
        return 0;
    }

    struct dirent* entry;
    int found = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (romi_stricontains(entry->d_name, search_name))
        {
            found = 1;
            break;
        }
    }

    closedir(dir);

    item->presence = found ? PresenceInstalled : PresenceMissing;
    return found;
}

void romi_storage_scan_installed(void)
{
    uint32_t total = romi_db_total();

    for (uint32_t i = 0; i < total; i++)
    {
        DbItem* item = romi_db_get(i);
        if (item)
            romi_storage_check_presence(item);
    }
}

void romi_storage_cancel(void)
{
    cancelled = 1;
    romi_extract_cancel();
}

const char* romi_storage_error_string(RomiStorageResult result)
{
    switch (result)
    {
        case StorageOK:            return "OK";
        case StorageErrorDownload: return "Download failed";
        case StorageErrorExtract:  return "Extraction failed";
        case StorageErrorDisk:     return "Disk error";
        case StorageErrorMemory:   return "Out of memory";
        case StorageCancelled:     return "Cancelled";
        default:                   return "Unknown error";
    }
}

const char* romi_storage_get_install_path(RomiPlatform platform, const char* filename)
{
    static char path[512];
    const char* folder = romi_platform_folder(platform);
    romi_snprintf(path, sizeof(path), "%s/%s", folder, filename);
    return path;
}
