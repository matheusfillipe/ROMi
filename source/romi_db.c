#include "romi_db.h"
#include "romi_config.h"
#include "romi_utils.h"
#include "romi.h"
#include "romi_devices.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <mini18n.h>

#define MAX_DB_SIZE (32*1024*1024)
#define MAX_DB_ITEMS 0x20000
#define TSV_COLUMNS 5

static char* db_data = NULL;
static uint32_t db_total;
static uint32_t db_size;

static DbItem db[MAX_DB_ITEMS];
static uint32_t db_count;

static DbItem* db_item[MAX_DB_ITEMS];
static uint32_t db_item_count;

static const char* platform_names[] = {
    "Unknown", "PSX", "PS2", "PS3",
    "NES", "SNES", "GB", "GBC", "GBA",
    "Genesis", "SMS",
    "Atari2600", "Atari5200", "Atari7800", "AtariLynx",
    "MAME"
};

static const char* platform_suffixes[] = {
    "ROMS",
    "PSXISO",
    "PS2ISO",
    "PS3ISO",
    "ROMS/NES",
    "ROMS/SNES",
    "ROMS/GB",
    "ROMS/GBC",
    "ROMS/GBA",
    "ROMS/Genesis",
    "ROMS/SMS",
    "ROMS/ATARI2600",
    "ROMS/ATARI5200",
    "ROMS/ATARI7800",
    "ROMS/LYNX",
    "ROMS/MAMEPLUS"
};

#define MAX_URL_LENGTH 512

static char platform_base_urls[PlatformCount][MAX_URL_LENGTH];
static int sources_loaded = 0;

RomiPlatform romi_parse_platform(const char* str)
{
    if (!str || !str[0]) return PlatformUnknown;

    if (romi_stricmp(str, "PSX") == 0 || romi_stricmp(str, "PS1") == 0) return PlatformPSX;
    if (romi_stricmp(str, "PS2") == 0) return PlatformPS2;
    if (romi_stricmp(str, "PS3") == 0) return PlatformPS3;
    if (romi_stricmp(str, "NES") == 0) return PlatformNES;
    if (romi_stricmp(str, "SNES") == 0) return PlatformSNES;
    if (romi_stricmp(str, "GB") == 0) return PlatformGB;
    if (romi_stricmp(str, "GBC") == 0) return PlatformGBC;
    if (romi_stricmp(str, "GBA") == 0) return PlatformGBA;
    if (romi_stricmp(str, "Genesis") == 0 || romi_stricmp(str, "MD") == 0) return PlatformGenesis;
    if (romi_stricmp(str, "SMS") == 0) return PlatformSMS;
    if (romi_stricmp(str, "Atari2600") == 0 || romi_stricmp(str, "ATARI") == 0) return PlatformAtari2600;
    if (romi_stricmp(str, "Atari5200") == 0) return PlatformAtari5200;
    if (romi_stricmp(str, "Atari7800") == 0) return PlatformAtari7800;
    if (romi_stricmp(str, "AtariLynx") == 0 || romi_stricmp(str, "LYNX") == 0) return PlatformAtariLynx;
    if (romi_stricmp(str, "MAME") == 0) return PlatformMAME;

    return PlatformUnknown;
}

RomiRegion romi_parse_region(const char* str)
{
    if (!str || !str[0]) return RegionUnknown;

    if (romi_stricmp(str, "USA") == 0 || romi_stricmp(str, "US") == 0) return RegionUSA;
    if (romi_stricmp(str, "EUR") == 0 || romi_stricmp(str, "Europe") == 0) return RegionEUR;
    if (romi_stricmp(str, "JPN") == 0 || romi_stricmp(str, "Japan") == 0) return RegionJPN;
    if (romi_stricmp(str, "World") == 0) return RegionWorld;
    if (romi_stricmp(str, "ASA") == 0 || romi_stricmp(str, "Asia") == 0) return RegionASA;

    return RegionUnknown;
}

const char* romi_platform_name(RomiPlatform p)
{
    if (p >= PlatformCount) return platform_names[0];
    return platform_names[p];
}

const char* romi_platform_folder(RomiPlatform p)
{
    static char path_buffer[512];
    const char* base = romi_devices_get_base_path();
    const char* suffix;

    if (p >= PlatformCount) {
        suffix = platform_suffixes[0];
    } else {
        suffix = platform_suffixes[p];
    }

    romi_snprintf(path_buffer, sizeof(path_buffer), "%s%s", base, suffix);
    return path_buffer;
}

uint32_t romi_platform_filter(RomiPlatform p)
{
    switch (p) {
        case PlatformPSX:         return DbFilterPlatformPSX;
        case PlatformPS2:         return DbFilterPlatformPS2;
        case PlatformPS3:         return DbFilterPlatformPS3;
        case PlatformNES:         return DbFilterPlatformNES;
        case PlatformSNES:        return DbFilterPlatformSNES;
        case PlatformGB:          return DbFilterPlatformGB;
        case PlatformGBC:         return DbFilterPlatformGBC;
        case PlatformGBA:         return DbFilterPlatformGBA;
        case PlatformGenesis:     return DbFilterPlatformGenesis;
        case PlatformSMS:         return DbFilterPlatformSMS;
        case PlatformAtari2600:   return DbFilterPlatformAtari2600;
        case PlatformAtari5200:   return DbFilterPlatformAtari5200;
        case PlatformAtari7800:   return DbFilterPlatformAtari7800;
        case PlatformAtariLynx:   return DbFilterPlatformAtariLynx;
        case PlatformMAME:        return DbFilterPlatformMAME;
        default:                  return 0;
    }
}

static uint32_t region_filter(RomiRegion r)
{
    switch (r) {
        case RegionUSA:   return DbFilterRegionUSA;
        case RegionEUR:   return DbFilterRegionEUR;
        case RegionJPN:   return DbFilterRegionJPN;
        case RegionWorld: return DbFilterRegionWorld;
        case RegionASA:   return DbFilterRegionASA;
        default:          return DbFilterAllRegions;
    }
}

static size_t write_update_data(void *buffer, size_t size, size_t nmemb, void *stream)
{
    size_t realsize = size * nmemb;
    romi_memcpy(db_data + db_size, buffer, realsize);
    db_size += realsize;
    return realsize;
}

static void load_sources(void)
{
    if (sources_loaded)
        return;

    for (int i = 0; i < PlatformCount; i++)
        platform_base_urls[i][0] = '\0';

    char data[8192];
    char path[256];
    romi_snprintf(path, sizeof(path), "%s/sources.txt", romi_get_config_folder());

    int loaded = romi_load(path, data, sizeof(data) - 1);
    if (loaded <= 0)
    {
        LOG("sources.txt not found at %s, URLs must be full paths in database", path);
        sources_loaded = 1;
        return;
    }

    data[loaded] = '\0';
    LOG("loading sources from %s", path);

    char* ptr = data;
    char* end = data + loaded;

    if (loaded > 3 && (uint8_t)ptr[0] == 0xef && (uint8_t)ptr[1] == 0xbb && (uint8_t)ptr[2] == 0xbf)
        ptr += 3;

    while (ptr < end)
    {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n' || *ptr == '\0'))
            ptr++;
        if (ptr >= end) break;

        if (*ptr == '#')
        {
            while (ptr < end && *ptr && *ptr != '\n')
                ptr++;
            continue;
        }

        char* key_start = ptr;
        while (ptr < end && *ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != '\r')
            ptr++;
        if (ptr >= end) break;

        char* key_end = ptr;
        if (key_start == key_end)
            continue;

        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\0'))
            ptr++;
        if (ptr >= end || *ptr == '\n' || *ptr == '\r')
            continue;

        char* value_start = ptr;
        while (ptr < end && *ptr && *ptr != '\n' && *ptr != '\r')
            ptr++;
        char* value_end = ptr;

        while (value_end > value_start && (*(value_end-1) == ' ' || *(value_end-1) == '\t'))
            value_end--;

        *key_end = '\0';
        *value_end = '\0';

        RomiPlatform platform = romi_parse_platform(key_start);
        if (platform != PlatformUnknown && platform < PlatformCount)
        {
            romi_strncpy(platform_base_urls[platform], MAX_URL_LENGTH, value_start);
            LOG("source %s = [%s] (len=%d)", key_start, value_start, (int)romi_strlen(value_start));
        }
    }

    sources_loaded = 1;
}

static int load_tsv_database(const char* path)
{
    int loaded = romi_load(path, db_data + db_size, MAX_DB_SIZE - db_size - 1);
    if (loaded <= 0)
        return 0;

    LOG("parsing database from %s (%d bytes)", path, loaded);

    char* ptr = db_data + db_size;
    char* end = ptr + loaded;
    db_size += loaded;
    db_data[db_size] = '\n';

    if (loaded > 3 && (uint8_t)ptr[0] == 0xef && (uint8_t)ptr[1] == 0xbb && (uint8_t)ptr[2] == 0xbf)
        ptr += 3;

    while (ptr < end && *ptr)
    {
        const char* columns[TSV_COLUMNS] = {0};
        uint8_t col = 0;

        while (ptr < end && col < TSV_COLUMNS)
        {
            columns[col] = ptr;

            while (ptr < end && *ptr != '\t' && *ptr != '\n' && *ptr != '\r')
                ptr++;

            if (*ptr == '\t') {
                *ptr++ = 0;
                col++;
            } else {
                *ptr++ = 0;
                col++;
                break;
            }
        }

        if (col == TSV_COLUMNS && columns[3] && columns[3][0])
        {
            RomiPlatform platform = romi_parse_platform(columns[0]);
            int valid_url = romi_validate_url(columns[3]);
            int has_base_url = (platform < PlatformCount && platform_base_urls[platform][0] != '\0');

            if (valid_url || has_base_url)
            {
                db[db_count].platform = platform;
                db[db_count].region = romi_parse_region(columns[1]);
                db[db_count].name = columns[2];
                db[db_count].url = columns[3];
                db[db_count].size = romi_strtoll(columns[4]);
                db[db_count].presence = PresenceUnknown;
                db_item[db_count] = &db[db_count];
                db_count++;
            }
        }

        if (db_count >= MAX_DB_ITEMS)
            break;

        while (ptr < end && (*ptr == '\r' || *ptr == '\n'))
            ptr++;
    }

    LOG("loaded %u items from %s", db_count - db_item_count, path);
    db_item_count = db_count;

    return 1;
}

int romi_db_update(const char* update_url, char* error, uint32_t error_size)
{
    if (!db_data || !update_url || !update_url[0])
        return 0;

    db_total = 0;
    db_size = 0;

    LOG("downloading database from %s", update_url);

    romi_http* http = romi_http_get(update_url, NULL, 0, 0);
    if (!http)
    {
        romi_snprintf(error, error_size, "%s\n%s", _("failed to download list from"), update_url);
        return 0;
    }

    int64_t length;
    if (!romi_http_response_length(http, &length))
    {
        romi_snprintf(error, error_size, "%s\n%s", _("failed to download list from"), update_url);
        romi_http_close(http);
        return 0;
    }

    if (length > (int64_t)(MAX_DB_SIZE - db_size - 1))
    {
        romi_snprintf(error, error_size, _("database is too large"));
        romi_http_close(http);
        return 0;
    }

    db_total = (uint32_t)length;

    if (!romi_http_read(http, &write_update_data, NULL, NULL))
    {
        romi_snprintf(error, error_size, "%s", _("HTTP download error"));
        db_size = 0;
        romi_http_close(http);
        return 0;
    }

    romi_http_close(http);

    char db_path[256];
    romi_snprintf(db_path, sizeof(db_path), "%s/romi_db.tsv", romi_get_config_folder());

    LOG("saving downloaded database to %s (%u bytes)", db_path, db_size);

    void* fp = romi_create(db_path);
    if (!fp)
    {
        LOG("failed to create database file %s", db_path);
        db_size = 0;
        romi_snprintf(error, error_size, _("Failed to save database file"));
        return 0;
    }

    if (!romi_write(fp, db_data, db_size))
    {
        LOG("failed to write database file");
        romi_close(fp);
        db_size = 0;
        romi_snprintf(error, error_size, _("Failed to write database file"));
        return 0;
    }

    romi_close(fp);
    LOG("database file saved successfully");

    return 1;
}

int romi_db_reload(char* error, uint32_t error_size)
{
    char path[256];

    db_total = 0;
    db_size = 0;
    db_count = 0;
    db_item_count = 0;

    load_sources();

    if (!db_data && (db_data = malloc(MAX_DB_SIZE)) == NULL)
    {
        romi_snprintf(error, error_size, "failed to allocate memory for database");
        return 0;
    }

    romi_snprintf(path, sizeof(path), "%s/romi_db.tsv", romi_get_config_folder());

    if (romi_get_size(path) > 0)
    {
        LOG("loading combined database from %s", path);
        load_tsv_database(path);
    }
    else
    {
        for (int i = 1; i < PlatformCount; i++)
        {
            romi_snprintf(path, sizeof(path), "%s/romi_%s.tsv",
                          romi_get_config_folder(), platform_names[i]);

            if (romi_get_size(path) > 0)
                load_tsv_database(path);
        }
    }

    LOG("database reload complete, %u total items", db_count);

    if (db_count == 0)
    {
        romi_snprintf(error, error_size, _("ERROR: No database files found. Place romi_*.tsv files in config folder."));
        return 0;
    }

    return 1;
}

static void swap(uint32_t a, uint32_t b)
{
    DbItem* temp = db_item[a];
    db_item[a] = db_item[b];
    db_item[b] = temp;
}

static int matches_filter(const DbItem* item, uint32_t filter, RomiPlatform active_platform)
{
    uint32_t rf = region_filter(item->region);
    int region_match = (filter & DbFilterAllRegions) == 0 || (filter & rf);

    int platform_match;
    if (active_platform != PlatformUnknown)
    {
        platform_match = (item->platform == active_platform);
    }
    else
    {
        uint32_t pf = romi_platform_filter(item->platform);
        platform_match = (filter & DbFilterAllPlatforms) == 0 || (filter & pf);
    }

    return platform_match && region_match;
}

static int compare_items(const DbItem* a, const DbItem* b, DbSort sort, DbSortOrder order)
{
    int cmp = 0;

    switch (sort) {
        case SortByName:
            cmp = romi_stricmp(a->name, b->name);
            break;
        case SortByRegion:
            cmp = (int)a->region - (int)b->region;
            if (cmp == 0) cmp = romi_stricmp(a->name, b->name);
            break;
        case SortByPlatform:
            cmp = (int)a->platform - (int)b->platform;
            if (cmp == 0) cmp = romi_stricmp(a->name, b->name);
            break;
        case SortBySize:
            cmp = (a->size < b->size) ? -1 : (a->size > b->size) ? 1 : 0;
            break;
    }

    return (order == SortAscending) ? cmp : -cmp;
}

static int lower(const DbItem* a, const DbItem* b, DbSort sort, DbSortOrder order, uint32_t filter, RomiPlatform active_platform)
{
    int matches_a = matches_filter(a, filter, active_platform);
    int matches_b = matches_filter(b, filter, active_platform);

    if (matches_a != matches_b)
        return matches_a ? 1 : 0;

    return compare_items(a, b, sort, order) < 0;
}

static void heapify(uint32_t n, uint32_t index, DbSort sort, DbSortOrder order, uint32_t filter, RomiPlatform active_platform)
{
    uint32_t largest = index;
    uint32_t left = 2 * index + 1;
    uint32_t right = 2 * index + 2;

    if (left < n && lower(db_item[largest], db_item[left], sort, order, filter, active_platform))
        largest = left;

    if (right < n && lower(db_item[largest], db_item[right], sort, order, filter, active_platform))
        largest = right;

    if (largest != index)
    {
        swap(index, largest);
        heapify(n, largest, sort, order, filter, active_platform);
    }
}

void romi_db_configure(const char* search, const Config* config)
{
    uint32_t search_count;

    if (!search || !search[0])
    {
        search_count = db_count;
    }
    else
    {
        uint32_t write = 0;
        for (uint32_t read = 0; read < db_count; read++)
        {
            if (romi_stricontains(db_item[read]->name, search))
            {
                if (write < read)
                    swap(read, write);
                write++;
            }
        }
        search_count = write;
    }

    if (search_count == 0)
    {
        db_item_count = 0;
        return;
    }

    for (int i = search_count / 2 - 1; i >= 0; i--)
        heapify(search_count, i, config->sort, config->order, config->filter, config->active_platform);

    for (int i = search_count - 1; i >= 0; i--)
    {
        swap(i, 0);
        heapify(i, 0, config->sort, config->order, config->filter, config->active_platform);
    }

    if (config->filter == DbFilterAll && config->active_platform == PlatformUnknown)
    {
        db_item_count = search_count;
    }
    else
    {
        uint32_t low = 0;
        uint32_t high = search_count - 1;
        while (low <= high)
        {
            uint32_t middle = (low + high) / 2;
            if (matches_filter(db_item[middle], config->filter, config->active_platform))
                low = middle + 1;
            else
            {
                if (middle == 0) break;
                high = middle - 1;
            }
        }
        db_item_count = low;
    }
}

void romi_db_get_update_status(uint32_t* updated, uint32_t* total)
{
    *updated = db_size;
    *total = db_total;
}

uint32_t romi_db_count(void)
{
    return db_item_count;
}

uint32_t romi_db_total(void)
{
    return db_count;
}

DbItem* romi_db_get(uint32_t index)
{
    return index < db_item_count ? db_item[index] : NULL;
}

const char* romi_db_get_full_url(const DbItem* item, char* buf, size_t size)
{
    if (!item || !buf || size == 0)
        return NULL;

    const char* base = platform_base_urls[item->platform];
    LOG("get_full_url: platform=%d base=[%s]", item->platform, base ? base : "NULL");
    if (!base || !base[0])
    {
        romi_snprintf(buf, size, "%s", item->url);
        return buf;
    }

    if (strncmp(item->url, "http://", 7) == 0 || strncmp(item->url, "https://", 8) == 0)
    {
        romi_snprintf(buf, size, "%s", item->url);
        return buf;
    }

    romi_snprintf(buf, size, "%s%s", base, item->url);
    LOG("get_full_url: result=[%s]", buf);
    return buf;
}
