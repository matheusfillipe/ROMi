#include "romi_config.h"
#include "romi.h"

static char* skipnonws(char* text, char* end)
{
    while (text < end && *text != ' ' && *text != '\n' && *text != '\r')
        text++;
    return text;
}

static char* skipws(char* text, char* end)
{
    while (text < end && (*text == ' ' || *text == '\n' || *text == '\r'))
        text++;
    return text;
}

static DbSort parse_sort(const char* value)
{
    if (romi_stricmp(value, "name") == 0)
        return SortByName;
    if (romi_stricmp(value, "region") == 0)
        return SortByRegion;
    if (romi_stricmp(value, "platform") == 0)
        return SortByPlatform;
    if (romi_stricmp(value, "size") == 0)
        return SortBySize;
    return SortByName;
}

static DbSortOrder parse_order(const char* value)
{
    if (romi_stricmp(value, "asc") == 0)
        return SortAscending;
    if (romi_stricmp(value, "desc") == 0)
        return SortDescending;
    return SortAscending;
}

static uint32_t parse_filter(char* value)
{
    uint32_t result = 0;
    char* start = value;

    for (;;)
    {
        char ch = *value;
        if (ch == 0 || ch == ',')
        {
            *value = 0;

            if (romi_stricmp(start, "USA") == 0)
                result |= DbFilterRegionUSA;
            else if (romi_stricmp(start, "EUR") == 0)
                result |= DbFilterRegionEUR;
            else if (romi_stricmp(start, "JPN") == 0)
                result |= DbFilterRegionJPN;
            else if (romi_stricmp(start, "World") == 0)
                result |= DbFilterRegionWorld;
            else if (romi_stricmp(start, "ASA") == 0)
                result |= DbFilterRegionASA;
            else if (romi_stricmp(start, "PSX") == 0)
                result |= DbFilterPlatformPSX;
            else if (romi_stricmp(start, "PS2") == 0)
                result |= DbFilterPlatformPS2;
            else if (romi_stricmp(start, "PS3") == 0)
                result |= DbFilterPlatformPS3;
            else if (romi_stricmp(start, "NES") == 0)
                result |= DbFilterPlatformNES;
            else if (romi_stricmp(start, "SNES") == 0)
                result |= DbFilterPlatformSNES;
            else if (romi_stricmp(start, "GB") == 0)
                result |= DbFilterPlatformGB;
            else if (romi_stricmp(start, "GBC") == 0)
                result |= DbFilterPlatformGBC;
            else if (romi_stricmp(start, "GBA") == 0)
                result |= DbFilterPlatformGBA;
            else if (romi_stricmp(start, "Genesis") == 0)
                result |= DbFilterPlatformGenesis;
            else if (romi_stricmp(start, "SMS") == 0)
                result |= DbFilterPlatformSMS;
            else if (romi_stricmp(start, "MAME") == 0)
                result |= DbFilterPlatformMAME;

            if (ch == 0)
                break;

            value++;
            start = value;
        }
        else
        {
            value++;
        }
    }

    return result;
}

void romi_load_config(Config* config)
{
    config->sort = SortByName;
    config->order = SortAscending;
    config->filter = DbFilterAll;
    config->active_platform = PlatformUnknown;
    config->music = 1;
    config->db_update_url[0] = '\0';
    config->storage_device_index = 0;
    romi_strncpy(config->storage_device_path, sizeof(config->storage_device_path), "/dev_hdd0/");
    config->proxy_url[0] = '\0';
    config->proxy_user[0] = '\0';
    config->proxy_pass[0] = '\0';
    romi_strncpy(config->language, sizeof(config->language), romi_get_user_language());

    char data[4096];
    char path[256];
    romi_snprintf(path, sizeof(path), "%s/config.txt", romi_get_config_folder());
    LOG("config location: %s", path);

    int loaded = romi_load(path, data, sizeof(data) - 1);
    if (loaded <= 0)
    {
        LOG("config.txt cannot be loaded, using default values");
        return;
    }

    data[loaded] = '\n';
    LOG("config.txt loaded, parsing");

    char* text = data;
    char* end = data + loaded + 1;

    if (loaded > 3 && (uint8_t)text[0] == 0xef && (uint8_t)text[1] == 0xbb && (uint8_t)text[2] == 0xbf)
        text += 3;

    while (text < end)
    {
        char* key = text;
        text = skipnonws(text, end);
        if (text == end) break;
        *text++ = 0;

        text = skipws(text, end);
        if (text == end) break;

        char* value = text;
        text = skipnonws(text, end);
        if (text == end) break;
        *text++ = 0;

        text = skipws(text, end);

        if (romi_stricmp(key, "url") == 0)
            romi_strncpy(config->db_update_url, sizeof(config->db_update_url), value);
        else if (romi_stricmp(key, "sort") == 0)
            config->sort = parse_sort(value);
        else if (romi_stricmp(key, "order") == 0)
            config->order = parse_order(value);
        else if (romi_stricmp(key, "filter") == 0)
            config->filter = parse_filter(value);
        else if (romi_stricmp(key, "platform") == 0)
            config->active_platform = romi_parse_platform(value);
        else if (romi_stricmp(key, "no_music") == 0)
            config->music = 0;
        else if (romi_stricmp(key, "storage_device") == 0)
        {
            romi_strncpy(config->storage_device_path, sizeof(config->storage_device_path), value);
            LOG("loaded storage device path: %s", config->storage_device_path);
        }
        else if (romi_stricmp(key, "proxy_url") == 0)
            romi_strncpy(config->proxy_url, sizeof(config->proxy_url), value);
        else if (romi_stricmp(key, "proxy_user") == 0)
            romi_strncpy(config->proxy_user, sizeof(config->proxy_user), value);
        else if (romi_stricmp(key, "proxy_pass") == 0)
            romi_strncpy(config->proxy_pass, sizeof(config->proxy_pass), value);
    }
}

static const char* sort_str(DbSort sort)
{
    switch (sort)
    {
        case SortByName:     return "name";
        case SortByRegion:   return "region";
        case SortByPlatform: return "platform";
        case SortBySize:     return "size";
        default:             return "name";
    }
}

static const char* order_str(DbSortOrder order)
{
    switch (order)
    {
        case SortAscending:  return "asc";
        case SortDescending: return "desc";
        default:             return "asc";
    }
}

void romi_save_config(const Config* config)
{
    char data[4096];
    int len = 0;

    if (config->db_update_url[0])
        len += romi_snprintf(data + len, sizeof(data) - len, "url %s\n", config->db_update_url);

    len += romi_snprintf(data + len, sizeof(data) - len, "sort %s\n", sort_str(config->sort));
    len += romi_snprintf(data + len, sizeof(data) - len, "order %s\n", order_str(config->order));

    if (config->active_platform != PlatformUnknown)
        len += romi_snprintf(data + len, sizeof(data) - len, "platform %s\n", romi_platform_name(config->active_platform));

    if (!config->music)
        len += romi_snprintf(data + len, sizeof(data) - len, "no_music 1\n");

    len += romi_snprintf(data + len, sizeof(data) - len, "filter ");
    const char* sep = "";

    if (config->filter & DbFilterRegionUSA)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sUSA", sep); sep = ","; }
    if (config->filter & DbFilterRegionEUR)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sEUR", sep); sep = ","; }
    if (config->filter & DbFilterRegionJPN)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sJPN", sep); sep = ","; }
    if (config->filter & DbFilterRegionWorld)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sWorld", sep); sep = ","; }
    if (config->filter & DbFilterRegionASA)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sASA", sep); sep = ","; }
    if (config->filter & DbFilterPlatformPSX)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sPSX", sep); sep = ","; }
    if (config->filter & DbFilterPlatformPS2)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sPS2", sep); sep = ","; }
    if (config->filter & DbFilterPlatformPS3)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sPS3", sep); sep = ","; }
    if (config->filter & DbFilterPlatformNES)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sNES", sep); sep = ","; }
    if (config->filter & DbFilterPlatformSNES)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sSNES", sep); sep = ","; }
    if (config->filter & DbFilterPlatformGB)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sGB", sep); sep = ","; }
    if (config->filter & DbFilterPlatformGBC)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sGBC", sep); sep = ","; }
    if (config->filter & DbFilterPlatformGBA)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sGBA", sep); sep = ","; }
    if (config->filter & DbFilterPlatformGenesis)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sGenesis", sep); sep = ","; }
    if (config->filter & DbFilterPlatformSMS)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sSMS", sep); sep = ","; }
    if (config->filter & DbFilterPlatformMAME)
    { len += romi_snprintf(data + len, sizeof(data) - len, "%sMAME", sep); sep = ","; }

    len += romi_snprintf(data + len, sizeof(data) - len, "\n");

    if (config->storage_device_path[0] != '\0')
        len += romi_snprintf(data + len, sizeof(data) - len, "storage_device %s\n", config->storage_device_path);

    if (config->proxy_url[0])
    {
        len += romi_snprintf(data + len, sizeof(data) - len, "proxy_url %s\n", config->proxy_url);
        if (config->proxy_user[0])
            len += romi_snprintf(data + len, sizeof(data) - len, "proxy_user %s\n", config->proxy_user);
        if (config->proxy_pass[0])
            len += romi_snprintf(data + len, sizeof(data) - len, "proxy_pass %s\n", config->proxy_pass);
    }

    char path[256];
    romi_snprintf(path, sizeof(path), "%s/config.txt", romi_get_config_folder());

    if (romi_save(path, data, len))
        LOG("saved config.txt");
    else
        LOG("cannot save config.txt");
}
