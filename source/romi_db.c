#include "romi_db.h"
#include "romi_config.h"
#include "romi_utils.h"
#include "romi.h"

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
    "Genesis", "SMS", "MAME"
};

static const char* platform_folders[] = {
    "/dev_hdd0/ROMS",
    "/dev_hdd0/PSXISO",
    "/dev_hdd0/PS2ISO",
    "/dev_hdd0/PS3ISO",
    "/dev_hdd0/ROMS/NES",
    "/dev_hdd0/ROMS/SNES",
    "/dev_hdd0/ROMS/GB",
    "/dev_hdd0/ROMS/GBC",
    "/dev_hdd0/ROMS/GBA",
    "/dev_hdd0/ROMS/Genesis",
    "/dev_hdd0/ROMS/SMS",
    "/dev_hdd0/ROMS/MAME"
};

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
    if (p >= PlatformCount) return platform_folders[0];
    return platform_folders[p];
}

uint32_t romi_platform_filter(RomiPlatform p)
{
    switch (p) {
        case PlatformPSX:     return DbFilterPlatformPSX;
        case PlatformPS2:     return DbFilterPlatformPS2;
        case PlatformPS3:     return DbFilterPlatformPS3;
        case PlatformNES:     return DbFilterPlatformNES;
        case PlatformSNES:    return DbFilterPlatformSNES;
        case PlatformGB:      return DbFilterPlatformGB;
        case PlatformGBC:     return DbFilterPlatformGBC;
        case PlatformGBA:     return DbFilterPlatformGBA;
        case PlatformGenesis: return DbFilterPlatformGenesis;
        case PlatformSMS:     return DbFilterPlatformSMS;
        case PlatformMAME:    return DbFilterPlatformMAME;
        default:              return 0;
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

        if (col == TSV_COLUMNS && columns[3] && romi_validate_url(columns[3]))
        {
            db[db_count].platform = romi_parse_platform(columns[0]);
            db[db_count].region = romi_parse_region(columns[1]);
            db[db_count].name = columns[2];
            db[db_count].url = columns[3];
            db[db_count].size = romi_strtoll(columns[4]);
            db[db_count].presence = PresenceUnknown;
            db_item[db_count] = &db[db_count];
            db_count++;
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
    uint32_t prev_size = db_size;

    LOG("downloading database from %s", update_url);

    romi_http* http = romi_http_get(update_url, NULL, 0);
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
        db_size = prev_size;
        romi_http_close(http);
        return 0;
    }

    romi_http_close(http);
    return 1;
}

int romi_db_reload(char* error, uint32_t error_size)
{
    char path[256];

    db_total = 0;
    db_size = 0;
    db_count = 0;
    db_item_count = 0;

    if (!db_data && (db_data = malloc(MAX_DB_SIZE)) == NULL)
    {
        romi_snprintf(error, error_size, "failed to allocate memory for database");
        return 0;
    }

    for (int i = 1; i < PlatformCount; i++)
    {
        romi_snprintf(path, sizeof(path), "%s/romi_%s.tsv",
                      romi_get_config_folder(), platform_names[i]);

        if (romi_get_size(path) > 0)
            load_tsv_database(path);
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

static int matches_filter(const DbItem* item, uint32_t filter)
{
    uint32_t pf = romi_platform_filter(item->platform);
    uint32_t rf = region_filter(item->region);

    int platform_match = (filter & DbFilterAllPlatforms) == 0 || (filter & pf);
    int region_match = (filter & DbFilterAllRegions) == 0 || (filter & rf);

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

static int lower(const DbItem* a, const DbItem* b, DbSort sort, DbSortOrder order, uint32_t filter)
{
    int matches_a = matches_filter(a, filter);
    int matches_b = matches_filter(b, filter);

    if (matches_a != matches_b)
        return matches_a ? 1 : 0;

    return compare_items(a, b, sort, order) < 0;
}

static void heapify(uint32_t n, uint32_t index, DbSort sort, DbSortOrder order, uint32_t filter)
{
    uint32_t largest = index;
    uint32_t left = 2 * index + 1;
    uint32_t right = 2 * index + 2;

    if (left < n && lower(db_item[largest], db_item[left], sort, order, filter))
        largest = left;

    if (right < n && lower(db_item[largest], db_item[right], sort, order, filter))
        largest = right;

    if (largest != index)
    {
        swap(index, largest);
        heapify(n, largest, sort, order, filter);
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
        heapify(search_count, i, config->sort, config->order, config->filter);

    for (int i = search_count - 1; i >= 0; i--)
    {
        swap(i, 0);
        heapify(i, 0, config->sort, config->order, config->filter);
    }

    if (config->filter == DbFilterAll)
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
            if (matches_filter(db_item[middle], config->filter))
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
