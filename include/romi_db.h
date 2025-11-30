#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PlatformUnknown = 0,
    PlatformPSX,
    PlatformPS2,
    PlatformPS3,
    PlatformNES,
    PlatformSNES,
    PlatformGB,
    PlatformGBC,
    PlatformGBA,
    PlatformGenesis,
    PlatformSMS,
    PlatformMAME,
    PlatformCount
} RomiPlatform;

typedef enum {
    RegionUnknown = 0,
    RegionUSA,
    RegionEUR,
    RegionJPN,
    RegionWorld,
    RegionASA,
} RomiRegion;

typedef enum {
    PresenceUnknown,
    PresenceInstalled,
    PresenceMissing,
} DbPresence;

typedef enum {
    SortByName,
    SortByRegion,
    SortByPlatform,
    SortBySize,
} DbSort;

typedef enum {
    SortAscending,
    SortDescending,
} DbSortOrder;

typedef enum {
    DbFilterRegionUSA   = 0x0001,
    DbFilterRegionEUR   = 0x0002,
    DbFilterRegionJPN   = 0x0004,
    DbFilterRegionWorld = 0x0008,
    DbFilterRegionASA   = 0x0010,

    DbFilterPlatformPSX     = 0x000100,
    DbFilterPlatformPS2     = 0x000200,
    DbFilterPlatformPS3     = 0x000400,
    DbFilterPlatformNES     = 0x000800,
    DbFilterPlatformSNES    = 0x001000,
    DbFilterPlatformGB      = 0x002000,
    DbFilterPlatformGBC     = 0x004000,
    DbFilterPlatformGBA     = 0x008000,
    DbFilterPlatformGenesis = 0x010000,
    DbFilterPlatformSMS     = 0x020000,
    DbFilterPlatformMAME    = 0x040000,

    DbFilterAllRegions = DbFilterRegionUSA | DbFilterRegionEUR | DbFilterRegionJPN |
                         DbFilterRegionWorld | DbFilterRegionASA,
    DbFilterAllPlatforms = DbFilterPlatformPSX | DbFilterPlatformPS2 | DbFilterPlatformPS3 |
                           DbFilterPlatformNES | DbFilterPlatformSNES |
                           DbFilterPlatformGB | DbFilterPlatformGBC | DbFilterPlatformGBA |
                           DbFilterPlatformGenesis | DbFilterPlatformSMS | DbFilterPlatformMAME,
    DbFilterAll = DbFilterAllRegions | DbFilterAllPlatforms,
} DbFilter;

typedef struct {
    DbPresence presence;
    RomiPlatform platform;
    RomiRegion region;
    const char* name;
    const char* url;
    int64_t size;
} DbItem;

typedef struct Config {
    DbSort sort;
    DbSortOrder order;
    uint32_t filter;
    RomiPlatform active_platform;
    uint8_t music;
    char language[3];
} Config;

int romi_db_reload(char* error, uint32_t error_size);
int romi_db_update(const char* update_url, char* error, uint32_t error_size);
void romi_db_get_update_status(uint32_t* updated, uint32_t* total);

void romi_db_configure(const char* search, const Config* config);

uint32_t romi_db_count(void);
uint32_t romi_db_total(void);
DbItem* romi_db_get(uint32_t index);
const char* romi_db_get_full_url(const DbItem* item, char* buf, size_t size);

RomiPlatform romi_parse_platform(const char* str);
RomiRegion romi_parse_region(const char* str);
const char* romi_platform_name(RomiPlatform p);
const char* romi_platform_folder(RomiPlatform p);
uint32_t romi_platform_filter(RomiPlatform p);
