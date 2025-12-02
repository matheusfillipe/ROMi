#include <mini18n.h>
#include "romi_menu.h"
#include "romi_config.h"
#include "romi_style.h"
#include "romi.h"

static int menu_search_clear;

static Config   menu_config;
static uint32_t menu_selected;

static MenuResult menu_result;

static int32_t menu_width;
static int32_t menu_delta;
static int32_t romi_menu_width = 0;

static int platform_index = 0;

typedef enum {
    MenuSearch,
    MenuSearchClear,
    MenuText,
    MenuSort,
    MenuFilter,
    MenuRefresh,
    MenuMusic,
    MenuPlatform
} MenuType;

typedef struct {
    MenuType type;
    const char* text;
    uint32_t value;
} MenuEntry;

static MenuEntry menu_entries[] =
{
    { MenuSearch, "Search...", 0 },
    { MenuSearchClear, ROMI_UTF8_CLEAR " clear", 0 },

    { MenuText, "Sort by:", 0 },
    { MenuSort, "Name", SortByName },
    { MenuSort, "Region", SortByRegion },
    { MenuSort, "Platform", SortByPlatform },
    { MenuSort, "Size", SortBySize },

    { MenuText, "Platform:", 0 },
    { MenuPlatform, "All", 0 },

    { MenuText, "Regions:", 0 },
    { MenuFilter, "Asia", DbFilterRegionASA },
    { MenuFilter, "Europe", DbFilterRegionEUR },
    { MenuFilter, "Japan", DbFilterRegionJPN },
    { MenuFilter, "USA", DbFilterRegionUSA },
    { MenuFilter, "World", DbFilterRegionWorld },

    { MenuText, "Options:", 0 },
    { MenuMusic, "Music", 1 },

    { MenuRefresh, "Refresh...", 0 },
};

static const struct {
    const char* name;
    RomiPlatform platform;
} platform_entries[] = {
    { "All", PlatformUnknown },
    { "PSX", PlatformPSX },
    { "PS2", PlatformPS2 },
    { "PS3", PlatformPS3 },
    { "NES", PlatformNES },
    { "SNES", PlatformSNES },
    { "GB", PlatformGB },
    { "GBC", PlatformGBC },
    { "GBA", PlatformGBA },
    { "Genesis", PlatformGenesis },
    { "SMS", PlatformSMS },
    { "MAME", PlatformMAME },
};

#define PLATFORM_COUNT (sizeof(platform_entries) / sizeof(platform_entries[0]))

int romi_menu_is_open(void)
{
    return menu_width != 0;
}

MenuResult romi_menu_result()
{
    return menu_result;
}

void romi_menu_get(Config* config)
{
    *config = menu_config;
}

static void set_max_width(const MenuEntry* entries, int size)
{
    for (int j, i = 0; i < size; i++)
    {
        if ((j = romi_text_width(entries[i].text) + ROMI_MENU_LEFT_PADDING*2) > romi_menu_width)
            romi_menu_width = j;
    }
}

static int find_platform_index(RomiPlatform p)
{
    for (int i = 0; i < (int)PLATFORM_COUNT; i++)
    {
        if (platform_entries[i].platform == p)
            return i;
    }
    return 0;
}

void romi_menu_start(int search_clear, const Config* config)
{
    menu_search_clear = search_clear;
    menu_width = 1;
    menu_delta = 1;
    menu_config = *config;
    platform_index = find_platform_index(config->active_platform);

    menu_entries[0].text = _("Search...");
    menu_entries[2].text = _("Sort by:");
    menu_entries[3].text = _("Name");
    menu_entries[4].text = _("Region");
    menu_entries[5].text = _("Platform");
    menu_entries[6].text = _("Size");
    menu_entries[7].text = _("Platform:");
    menu_entries[8].text = _("All");
    menu_entries[9].text = _("Regions:");
    menu_entries[10].text = _("Asia");
    menu_entries[11].text = _("Europe");
    menu_entries[12].text = _("Japan");
    menu_entries[13].text = _("USA");
    menu_entries[14].text = _("World");
    menu_entries[15].text = _("Options:");
    menu_entries[16].text = _("Music");
    menu_entries[17].text = _("Refresh...");

    if (romi_menu_width)
        return;

    romi_menu_width = ROMI_MENU_WIDTH;
    set_max_width(menu_entries, ROMI_COUNTOF(menu_entries));
}

int romi_do_menu(romi_input* input)
{
    if (menu_delta != 0)
    {
        menu_width += menu_delta * (int32_t)(input->delta * ROMI_ANIMATION_SPEED/ 3000);

        if (menu_delta < 0 && menu_width <= 0)
        {
            menu_width = 0;
            menu_delta = 0;
            return 0;
        }
        else if (menu_delta > 0 && menu_width >= romi_menu_width)
        {
            menu_width = romi_menu_width;
            menu_delta = 0;
        }
    }

    if (menu_width != 0)
    {
        romi_draw_fill_rect_z(VITA_WIDTH - (menu_width + ROMI_MAIN_HMARGIN), ROMI_MAIN_VMARGIN, ROMI_MENU_Z, menu_width, ROMI_MENU_HEIGHT, ROMI_COLOR_MENU_BACKGROUND);
        romi_draw_rect_z(VITA_WIDTH - (menu_width + ROMI_MAIN_HMARGIN), ROMI_MAIN_VMARGIN, ROMI_MENU_Z, menu_width, ROMI_MENU_HEIGHT, ROMI_COLOR_MENU_BORDER);
    }

    if (input->active & ROMI_BUTTON_UP)
    {
        do {
            if (menu_selected == 0)
            {
                menu_selected = ROMI_COUNTOF(menu_entries) - 1;
            }
            else
            {
                menu_selected--;
            }
        } while (menu_entries[menu_selected].type == MenuText
            || (menu_entries[menu_selected].type == MenuSearchClear && !menu_search_clear));
    }

    if (input->active & ROMI_BUTTON_DOWN)
    {
        do {
            if (menu_selected == ROMI_COUNTOF(menu_entries) - 1)
            {
                menu_selected = 0;
            }
            else
            {
                menu_selected++;
            }
        } while (menu_entries[menu_selected].type == MenuText
            || (menu_entries[menu_selected].type == MenuSearchClear && !menu_search_clear));
    }

    if (input->pressed & romi_cancel_button())
    {
        menu_result = MenuResultCancel;
        menu_delta = -1;
        return 1;
    }
    else if (input->pressed & ROMI_BUTTON_T)
    {
        menu_result = MenuResultAccept;
        menu_delta = -1;
        return 1;
    }
    else if (input->pressed & romi_ok_button())
    {
        MenuType type = menu_entries[menu_selected].type;
        if (type == MenuSearch)
        {
            menu_result = MenuResultSearch;
            menu_delta = -1;
            return 1;
        }
        if (type == MenuSearchClear)
        {
            menu_selected--;
            menu_result = MenuResultSearchClear;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuRefresh)
        {
            menu_result = MenuResultRefresh;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuSort)
        {
            DbSort value = (DbSort)menu_entries[menu_selected].value;
            if (menu_config.sort == value)
            {
                menu_config.order = menu_config.order == SortAscending ? SortDescending : SortAscending;
            }
            else
            {
                menu_config.sort = value;
            }
        }
        else if (type == MenuFilter)
        {
            menu_config.filter ^= menu_entries[menu_selected].value;
        }
        else if (type == MenuMusic)
        {
            menu_config.music ^= menu_entries[menu_selected].value;
        }
        else if (type == MenuPlatform)
        {
            platform_index++;
            if (platform_index >= (int)PLATFORM_COUNT)
                platform_index = 0;
            menu_config.active_platform = platform_entries[platform_index].platform;
        }
    }

    if (menu_width != romi_menu_width)
    {
        return 1;
    }

    int font_height = romi_text_height("M");

    int y = ROMI_MENU_TOP_PADDING;
    for (uint32_t i = 0; i < ROMI_COUNTOF(menu_entries); i++)
    {
        const MenuEntry* entry = menu_entries + i;

        MenuType type = entry->type;
        if (type == MenuText)
        {
            y += font_height;
        }
        else if (type == MenuSearchClear && !menu_search_clear)
        {
            continue;
        }
        else if (type == MenuRefresh)
        {
            y += font_height;
        }

        int x = VITA_WIDTH - (romi_menu_width + ROMI_MAIN_HMARGIN) + ROMI_MENU_LEFT_PADDING;

        char text[64];
        if (type == MenuSearch || type == MenuSearchClear || type == MenuText || type == MenuRefresh)
        {
            romi_strncpy(text, sizeof(text), entry->text);
        }
        else if (type == MenuSort)
        {
            if (menu_config.sort == (DbSort)entry->value)
            {
                romi_snprintf(text, sizeof(text), "%s %s",
                    menu_config.order == SortAscending ? ROMI_UTF8_SORT_ASC : ROMI_UTF8_SORT_DESC,
                    entry->text);
            }
            else
            {
                x += romi_text_width(ROMI_UTF8_SORT_ASC " ");
                romi_strncpy(text, sizeof(text), entry->text);
            }
        }
        else if (type == MenuFilter)
        {
            romi_snprintf(text, sizeof(text), "%s %s",
                menu_config.filter & entry->value ? ROMI_UTF8_CHECK_ON : ROMI_UTF8_CHECK_OFF,
                entry->text);
        }
        else if (type == MenuMusic)
        {
            romi_snprintf(text, sizeof(text), "%s %s",
                menu_config.music == (int)entry->value ? ROMI_UTF8_CHECK_ON : ROMI_UTF8_CHECK_OFF, entry->text);
        }
        else if (type == MenuPlatform)
        {
            romi_snprintf(text, sizeof(text), ROMI_UTF8_CLEAR " %s", platform_entries[platform_index].name);
        }

        romi_draw_text_ttf(x, y, ROMI_MENU_TEXT_Z, (menu_selected == i) ? ROMI_COLOR_TEXT_MENU_SELECTED : ROMI_COLOR_TEXT_MENU, text);

        y += font_height;
    }

    return 1;
}
