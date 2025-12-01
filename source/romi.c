#include "romi.h"
#include "romi_db.h"
#include "romi_menu.h"
#include "romi_config.h"
#include "romi_dialog.h"
#include "romi_download.h"
#include "romi_utils.h"
#include "romi_style.h"

#include <stddef.h>
#include <mini18n.h>

typedef enum  {
    StateError,
    StateRefreshing,
    StateUpdateDone,
    StateMain,
    StateTerminate
} State;

static State state;

static uint32_t first_item;
static uint32_t selected_item;

static int search_active;

static char refresh_url[256];

static Config config;
static Config config_temp;

static int font_height;
static int avail_height;
static int bottom_y;

static char search_text[256];
static char error_state[256];

static void reposition(void);

static const char* romi_get_ok_str(void)
{
    return romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O;
}

static const char* romi_get_cancel_str(void)
{
    return romi_cancel_button() == ROMI_BUTTON_O ? ROMI_UTF8_O : ROMI_UTF8_X;
}

static void romi_refresh_thread(void)
{
    LOG("starting update");

    if (romi_menu_result() == MenuResultRefresh && refresh_url[0])
    {
        romi_db_update(refresh_url, error_state, sizeof(error_state));
    }

    if (romi_db_reload(error_state, sizeof(error_state)))
    {
        first_item = 0;
        selected_item = 0;
        state = StateUpdateDone;
    }
    else
    {
        state = StateError;
    }

    romi_thread_exit();
}

static void download_progress(const char* status, uint64_t downloaded, uint64_t total)
{
    if (total > 0)
    {
        int percent = (int)(downloaded * 100 / total);
        romi_dialog_set_progress(status, percent);
    }
    else
    {
        romi_dialog_set_progress(status, -1);
    }
}

static void romi_download_thread(void)
{
    DbItem* item = romi_db_get(selected_item);

    LOG("download thread start");

    romi_sleep(300);

    romi_lock_process();
    if (romi_download_rom(item, download_progress))
    {
        romi_dialog_message(item->name, _("Successfully downloaded"));
        LOG("download completed!");
    }
    romi_unlock_process();

    if (romi_dialog_is_cancelled())
    {
        romi_dialog_close();
    }

    item->presence = PresenceUnknown;
    state = StateMain;

    romi_thread_exit();
}

static uint32_t friendly_size(uint64_t size)
{
    if (size > 10ULL * 1000 * 1024 * 1024)
        return (uint32_t)(size / (1024 * 1024 * 1024));
    else if (size > 10 * 1000 * 1024)
        return (uint32_t)(size / (1024 * 1024));
    else if (size > 10 * 1000)
        return (uint32_t)(size / 1024);
    else
        return (uint32_t)size;
}

static const char* friendly_size_str(uint64_t size)
{
    if (size > 10ULL * 1000 * 1024 * 1024)
        return _("GB");
    else if (size > 10 * 1000 * 1024)
        return _("MB");
    else if (size > 10 * 1000)
        return _("KB");
    else
        return _("B");
}

int romi_check_free_space(uint64_t size)
{
    uint64_t free = romi_get_free_space();
    if (size > free + 1024 * 1024)
    {
        char error[256];
        romi_snprintf(error, sizeof(error), _("ROM requires %u %s free space, but only %u %s available"),
            friendly_size(size), friendly_size_str(size),
            friendly_size(free), friendly_size_str(free)
        );

        romi_dialog_error(error);
        return 0;
    }

    return 1;
}

static void romi_friendly_size(char* text, uint32_t textlen, int64_t size)
{
    if (size <= 0)
        text[0] = 0;
    else if (size < 1000LL)
        romi_snprintf(text, textlen, "%u " ROMI_UTF8_B, (uint32_t)size);
    else if (size < 1000LL * 1000)
        romi_snprintf(text, textlen, "%.2f " ROMI_UTF8_KB, size / 1024.f);
    else if (size < 1000LL * 1000 * 1000)
        romi_snprintf(text, textlen, "%.2f " ROMI_UTF8_MB, size / 1024.f / 1024.f);
    else
        romi_snprintf(text, textlen, "%.2f " ROMI_UTF8_GB, size / 1024.f / 1024.f / 1024.f);
}

static const char* platform_str(RomiPlatform p)
{
    switch (p)
    {
        case PlatformUnknown: return _("All");
        case PlatformPSX: return "PSX";
        case PlatformPS2: return "PS2";
        case PlatformPS3: return "PS3";
        case PlatformNES: return "NES";
        case PlatformSNES: return "SNES";
        case PlatformGB: return "GB";
        case PlatformGBC: return "GBC";
        case PlatformGBA: return "GBA";
        case PlatformGenesis: return "Genesis";
        case PlatformSMS: return "SMS";
        case PlatformMAME: return "MAME";
        default: return "?";
    }
}

static const char* region_str(RomiRegion r)
{
    switch (r)
    {
        case RegionUSA: return "USA";
        case RegionEUR: return "EUR";
        case RegionJPN: return "JPN";
        case RegionWorld: return "World";
        case RegionASA: return "ASA";
        default: return "???";
    }
}

static void cb_dialog_exit(int res)
{
    state = StateTerminate;
}

static void cb_dialog_download(int res)
{
    DbItem* item = romi_db_get(selected_item);

    item->presence = PresenceMissing;
    romi_dialog_start_progress(_("Downloading..."), _("Preparing..."), 0);
    romi_start_thread("download_thread", &romi_download_thread);
}

static int check_rom_installed(DbItem* item)
{
    if (!item || !item->url)
        return 0;

    const char* folder = romi_platform_folder(item->platform);
    if (!folder)
        return 0;

    const char* slash = romi_strrchr(item->url, '/');
    if (!slash)
        return 0;

    const char* filename = slash + 1;
    char path[512];

    const char* dot = romi_strrchr(filename, '.');
    if (dot && (romi_stricmp(dot, ".zip") == 0 || romi_stricmp(dot, ".7z") == 0))
    {
        char base[256];
        int len = dot - filename;
        if (len > (int)sizeof(base) - 5)
            len = sizeof(base) - 5;
        romi_memcpy(base, filename, len);
        base[len] = 0;

        const char* exts[] = {".iso", ".bin", ".cue", ".img", ".nes", ".sfc", ".smc", ".gba", ".gbc", ".gb", ".n64", ".z64", ".v64", ""};
        for (int i = 0; exts[i][0] || i == 0; i++)
        {
            romi_snprintf(path, sizeof(path), "%s/%s%s", folder, base, exts[i]);
            if (romi_get_size(path) > 0)
                return 1;
        }
    }

    romi_snprintf(path, sizeof(path), "%s/%s", folder, filename);
    return romi_get_size(path) > 0;
}

static void romi_do_main(romi_input* input)
{
    int col_platform = ROMI_MAIN_HMARGIN;
    int col_region = col_platform + romi_text_width("Genesis") + ROMI_MAIN_COLUMN_PADDING;
    int col_installed = col_region + romi_text_width("World") + ROMI_MAIN_COLUMN_PADDING;
    int col_name = col_installed + romi_text_width(ROMI_UTF8_INSTALLED) + ROMI_MAIN_COLUMN_PADDING;

    uint32_t db_count = romi_db_count();

    if (input)
    {
        if (input->active & romi_cancel_button())
        {
            input->pressed &= ~romi_cancel_button();
            romi_dialog_ok_cancel("\xE2\x98\x85  ROMi PS3 v" ROMI_VERSION "  \xE2\x98\x85", _("Exit to XMB?"), &cb_dialog_exit);
        }

        if (input->active & ROMI_BUTTON_SELECT)
        {
            input->pressed &= ~ROMI_BUTTON_SELECT;
            romi_dialog_message("\xE2\x98\x85  ROMi PS3 v" ROMI_VERSION "  \xE2\x98\x85",
                                "          ROM Downloader for PlayStation 3\n\n"
                                "           Based on PKGi by Bucanero");
        }

        if (input->active & ROMI_BUTTON_L2)
        {
            if (config.active_platform == PlatformUnknown)
                config.active_platform = PlatformCount - 1;
            else
                config.active_platform--;

            romi_db_configure(search_active ? search_text : NULL, &config);
            reposition();
            db_count = romi_db_count();
        }

        if (input->active & ROMI_BUTTON_R2)
        {
            config.active_platform++;
            if (config.active_platform >= PlatformCount)
                config.active_platform = PlatformUnknown;

            romi_db_configure(search_active ? search_text : NULL, &config);
            reposition();
            db_count = romi_db_count();
        }

        if (input->active & ROMI_BUTTON_UP)
        {
            if (selected_item == first_item && first_item > 0)
            {
                first_item--;
                selected_item = first_item;
            }
            else if (selected_item > 0)
            {
                selected_item--;
            }
            else if (selected_item == 0)
            {
                selected_item = db_count - 1;
                uint32_t max_items = avail_height / (font_height + ROMI_MAIN_ROW_PADDING) - 1;
                first_item = db_count > max_items ? db_count - max_items - 1 : 0;
            }
        }

        if (input->active & ROMI_BUTTON_DOWN)
        {
            uint32_t max_items = avail_height / (font_height + ROMI_MAIN_ROW_PADDING) - 1;
            if (selected_item == db_count - 1)
            {
                selected_item = first_item = 0;
            }
            else if (selected_item == first_item + max_items)
            {
                first_item++;
                selected_item++;
            }
            else
            {
                selected_item++;
            }
        }

        if (input->active & ROMI_BUTTON_LT)
        {
            uint32_t max_items = avail_height / (font_height + ROMI_MAIN_ROW_PADDING) - 1;
            if (first_item < max_items)
                first_item = 0;
            else
                first_item -= max_items;
            if (selected_item < max_items)
                selected_item = 0;
            else
                selected_item -= max_items;
        }

        if (input->active & ROMI_BUTTON_RT)
        {
            uint32_t max_items = avail_height / (font_height + ROMI_MAIN_ROW_PADDING) - 1;
            if (first_item + max_items < db_count - 1)
            {
                first_item += max_items;
                selected_item += max_items;
                if (selected_item >= db_count)
                    selected_item = db_count - 1;
            }
        }
    }

    int y = font_height*3/2 + ROMI_MAIN_HLINE_EXTRA + ROMI_MAIN_VMARGIN;
    int line_height = font_height + ROMI_MAIN_ROW_PADDING;
    for (uint32_t i = first_item; i < db_count; i++)
    {
        DbItem* item = romi_db_get(i);

        if (i == selected_item)
            romi_draw_fill_rect_z(0, y, ROMI_FONT_Z, VITA_WIDTH, font_height + ROMI_MAIN_ROW_PADDING - 1, ROMI_COLOR_SELECTED_BACKGROUND);

        uint32_t color = ROMI_COLOR_TEXT;

        if (item->presence == PresenceUnknown)
            item->presence = check_rom_installed(item) ? PresenceInstalled : PresenceMissing;

        char size_str[64];
        romi_friendly_size(size_str, sizeof(size_str), item->size);
        int sizew = romi_text_width(size_str);

        romi_clip_set(0, y, VITA_WIDTH, line_height);
        romi_draw_text(col_platform, y, color, platform_str(item->platform));
        romi_draw_text(col_region, y, color, region_str(item->region));

        if (item->presence == PresenceInstalled)
            romi_draw_text(col_installed, y, color, ROMI_UTF8_INSTALLED);

        romi_draw_text(VITA_WIDTH - (ROMI_MAIN_SCROLL_WIDTH + ROMI_MAIN_SCROLL_PADDING + ROMI_MAIN_HMARGIN + sizew), y, color, size_str);
        romi_clip_remove();

        romi_clip_set(col_name, y, VITA_WIDTH - ROMI_MAIN_SCROLL_WIDTH - ROMI_MAIN_SCROLL_PADDING - ROMI_MAIN_COLUMN_PADDING - sizew - col_name, line_height);
        romi_draw_text_ttf(0, 0, ROMI_FONT_Z, color, item->name);
        romi_clip_remove();

        y += font_height + ROMI_MAIN_ROW_PADDING;
        if (y > VITA_HEIGHT - (font_height + ROMI_MAIN_HLINE_EXTRA*6 + ROMI_MAIN_VMARGIN))
            break;
        else if (y + font_height > VITA_HEIGHT - (font_height + ROMI_MAIN_HLINE_EXTRA))
        {
            line_height = (VITA_HEIGHT - (font_height + ROMI_MAIN_HLINE_EXTRA)) - (y + 1);
            if (line_height < ROMI_MAIN_ROW_PADDING)
                break;
        }
    }

    if (db_count == 0)
    {
        const char* text = _("No items!");
        int w = romi_text_width(text);
        romi_draw_text((VITA_WIDTH - w) / 2, VITA_HEIGHT / 2, ROMI_COLOR_TEXT, text);
    }

    if (db_count != 0)
    {
        uint32_t max_items = (avail_height + font_height + ROMI_MAIN_ROW_PADDING - 1) / (font_height + ROMI_MAIN_ROW_PADDING) - 1;
        if (max_items < db_count)
        {
            uint32_t min_height = ROMI_MAIN_SCROLL_MIN_HEIGHT;
            uint32_t height = max_items * avail_height / db_count;
            uint32_t start = first_item * (avail_height - (height < min_height ? min_height : 0)) / db_count;
            height = max32(height, min_height);
            romi_draw_fill_rect_z(VITA_WIDTH - (ROMI_MAIN_HMARGIN + ROMI_MAIN_SCROLL_WIDTH), font_height + ROMI_MAIN_HLINE_EXTRA + ROMI_MAIN_VMARGIN + start + 2, ROMI_FONT_Z, ROMI_MAIN_SCROLL_WIDTH, height, ROMI_COLOR_SCROLL_BAR);
        }
    }

    if (input && (input->pressed & romi_ok_button()) && db_count)
    {
        input->pressed &= ~romi_ok_button();

        DbItem* item = romi_db_get(selected_item);

        if (!romi_check_free_space(item->size))
        {
            LOG("[%s] %s - no free space", platform_str(item->platform), item->name);
            romi_dialog_error(_("Not enough free space on HDD"));
        }
        else if (item->presence == PresenceInstalled)
        {
            LOG("[%s] %s - already installed", platform_str(item->platform), item->name);
            romi_dialog_ok_cancel(item->name, _("ROM already exists, download again?"), &cb_dialog_download);
        }
        else
        {
            LOG("[%s] %s - starting download", platform_str(item->platform), item->name);
            romi_dialog_start_progress(_("Downloading..."), _("Preparing..."), 0);
            romi_start_thread("download_thread", &romi_download_thread);
        }
    }
    else if (input && (input->pressed & ROMI_BUTTON_T))
    {
        input->pressed &= ~ROMI_BUTTON_T;
        config_temp = config;
        romi_menu_start(search_active, &config);
    }
    else if (input && (input->active & ROMI_BUTTON_S) && db_count)
    {
        input->pressed &= ~ROMI_BUTTON_S;
        DbItem* item = romi_db_get(selected_item);
        romi_dialog_details(item, platform_str(item->platform));
    }
}

static void romi_do_refresh(void)
{
    char text[256];

    uint32_t updated;
    uint32_t total;
    romi_db_get_update_status(&updated, &total);

    if (total == 0)
        romi_snprintf(text, sizeof(text), "%s... %.2f %s", _("Refreshing"), (uint32_t)updated / 1024.f, _("KB"));
    else
        romi_snprintf(text, sizeof(text), "%s... %u%%", _("Refreshing"), updated * 100U / total);

    int w = romi_text_width(text);
    romi_draw_text((VITA_WIDTH - w) / 2, VITA_HEIGHT / 2, ROMI_COLOR_TEXT, text);
}

static void romi_do_head(void)
{
    char title[256];
    romi_snprintf(title, sizeof(title), "ROMi PS3 v%s - %s", ROMI_VERSION, platform_str(config.active_platform));
    romi_draw_text(ROMI_MAIN_HMARGIN, ROMI_MAIN_VMARGIN, ROMI_COLOR_TEXT_HEAD, title);

    romi_draw_fill_rect(0, font_height + ROMI_MAIN_VMARGIN, VITA_WIDTH, ROMI_MAIN_HLINE_HEIGHT, ROMI_COLOR_HLINE);

    char battery[256];
    romi_snprintf(battery, sizeof(battery), "CPU: %u""\xf8""C RSX: %u""\xf8""C", romi_get_temperature(0), romi_get_temperature(1));

    uint32_t color = romi_temperature_is_high() ? ROMI_COLOR_BATTERY_LOW : ROMI_COLOR_BATTERY_CHARGING;
    int rightw = romi_text_width(battery);
    romi_draw_text(VITA_WIDTH - ROMI_MAIN_HLINE_EXTRA - (rightw + ROMI_MAIN_HMARGIN), ROMI_MAIN_VMARGIN, color, battery);

    if (search_active)
    {
        char text[256];
        int left = romi_text_width(search_text) + ROMI_MAIN_TEXT_PADDING;
        int right = rightw + ROMI_MAIN_TEXT_PADDING;

        romi_snprintf(text, sizeof(text), ">> %s <<", search_text);

        romi_clip_set(left, ROMI_MAIN_VMARGIN, VITA_WIDTH - right - left, font_height + ROMI_MAIN_HLINE_EXTRA);
        romi_draw_text((VITA_WIDTH - romi_text_width(text)) / 2, ROMI_MAIN_VMARGIN, ROMI_COLOR_TEXT_TAIL, text);
        romi_clip_remove();
    }
}

static void romi_do_tail(void)
{
    romi_draw_fill_rect_z(0, bottom_y - font_height/2, ROMI_FONT_Z, VITA_WIDTH, ROMI_MAIN_HLINE_HEIGHT, ROMI_COLOR_HLINE);

    uint32_t count = romi_db_count();
    uint32_t total = romi_db_total();

    char text[256];
    if (count == total)
        romi_snprintf(text, sizeof(text), "%s: %u", _("Count"), count);
    else
        romi_snprintf(text, sizeof(text), "%s: %u (%u)", _("Count"), count, total);
    romi_draw_text(ROMI_MAIN_HMARGIN, bottom_y, ROMI_COLOR_TEXT_TAIL, text);

    char size[64];
    romi_friendly_size(size, sizeof(size), romi_get_free_space());

    char free_str[64];
    romi_snprintf(free_str, sizeof(free_str), "%s: %s", _("Free"), size);

    int rightw = romi_text_width(free_str);
    romi_draw_text(VITA_WIDTH - (ROMI_MAIN_HLINE_EXTRA + ROMI_MAIN_HMARGIN + rightw), bottom_y, ROMI_COLOR_TEXT_TAIL, free_str);

    int left = romi_text_width(text) + ROMI_MAIN_TEXT_PADDING;
    int right = rightw + ROMI_MAIN_TEXT_PADDING;

    if (romi_menu_is_open())
        romi_snprintf(text, sizeof(text), "%s %s  " ROMI_UTF8_T " %s  %s %s", romi_get_ok_str(), _("Select"), _("Close"), romi_get_cancel_str(), _("Cancel"));
    else
        romi_snprintf(text, sizeof(text), "%s %s  " ROMI_UTF8_T " %s  " ROMI_UTF8_S " %s  %s %s", romi_get_ok_str(), _("Download"), _("Menu"), _("Details"), romi_get_cancel_str(), _("Exit"));

    romi_clip_set(left, bottom_y, VITA_WIDTH - right - left, VITA_HEIGHT - bottom_y);
    romi_draw_text_z((VITA_WIDTH - romi_text_width(text)) / 2, bottom_y, ROMI_FONT_Z, ROMI_COLOR_TEXT_TAIL, text);
    romi_clip_remove();
}

static void romi_do_error(void)
{
    romi_draw_text((VITA_WIDTH - romi_text_width(error_state)) / 2, VITA_HEIGHT / 2, ROMI_COLOR_TEXT_ERROR, error_state);
}

static void reposition(void)
{
    uint32_t count = romi_db_count();
    if (first_item + selected_item < count)
        return;

    uint32_t max_items = (avail_height + font_height + ROMI_MAIN_ROW_PADDING - 1) / (font_height + ROMI_MAIN_ROW_PADDING) - 1;
    if (count > max_items)
    {
        uint32_t delta = selected_item - first_item;
        first_item = count - max_items;
        selected_item = first_item + delta;
    }
    else
    {
        first_item = 0;
        selected_item = 0;
    }
}

static void romi_load_language(const char* lang)
{
    char path[256];
    romi_snprintf(path, sizeof(path), ROMI_APP_FOLDER "/LANG/%s.po", lang);
    LOG("Loading language file (%s)...", path);
    mini18n_set_locale(path);
}

int main(int argc, const char* argv[])
{
    romi_start();

    romi_load_config(&config);
    if (config.music)
        romi_start_music();

    romi_load_language(config.language);
    romi_dialog_init();

    font_height = romi_text_height("M");
    avail_height = VITA_HEIGHT - 2 * (font_height + ROMI_MAIN_HLINE_EXTRA*2 + ROMI_MAIN_VMARGIN);
    bottom_y = VITA_HEIGHT - (ROMI_MAIN_VMARGIN + font_height);

    romi_snprintf(refresh_url, sizeof(refresh_url), "%s", config.db_update_url);

    state = StateRefreshing;
    romi_start_thread("refresh_thread", &romi_refresh_thread);

    romi_texture background = romi_load_image_buffer(background, jpg);

    romi_input input = {0, 0, 0, 0};
    while (romi_update(&input) && (state != StateTerminate))
    {
        romi_draw_background(background);

        if (state == StateUpdateDone)
        {
            romi_db_configure(NULL, &config);
            state = StateMain;
        }

        romi_do_head();
        switch (state)
        {
        case StateError:
            romi_do_error();
            if (!romi_menu_is_open())
            {
                config_temp = config;
                romi_menu_start(search_active, &config);
            }
            break;

        case StateRefreshing:
            romi_do_refresh();
            break;

        case StateMain:
            romi_do_main(romi_dialog_is_open() || romi_menu_is_open() ? NULL : &input);
            break;

        default:
            break;
        }

        romi_do_tail();

        if (romi_dialog_is_open())
        {
            romi_do_dialog(&input);

            if (romi_dialog_is_cancelled())
                romi_dialog_close();
        }

        if (romi_dialog_input_update())
        {
            search_active = 1;
            romi_dialog_input_get_text(search_text, sizeof(search_text));
            romi_db_configure(search_text, &config);
            reposition();
        }

        if (romi_menu_is_open())
        {
            if (romi_do_menu(&input))
            {
                Config new_config;
                romi_menu_get(&new_config);
                if (config_temp.sort != new_config.sort ||
                    config_temp.order != new_config.order ||
                    config_temp.filter != new_config.filter ||
                    config_temp.active_platform != new_config.active_platform)
                {
                    config_temp = new_config;
                    romi_db_configure(search_active ? search_text : NULL, &config_temp);
                    reposition();
                }
                else if (config_temp.music != new_config.music)
                {
                    config_temp = new_config;
                    (config_temp.music ? romi_start_music() : romi_stop_music());
                }
            }
            else
            {
                MenuResult mres = romi_menu_result();
                if (mres == MenuResultSearch)
                {
                    romi_dialog_input_text(_("Search"), search_text);
                }
                else if (mres == MenuResultSearchClear)
                {
                    search_active = 0;
                    search_text[0] = 0;
                    romi_db_configure(NULL, &config);
                }
                else if (mres == MenuResultCancel)
                {
                    if (config_temp.sort != config.sort || config_temp.order != config.order ||
                        config_temp.filter != config.filter || config_temp.active_platform != config.active_platform)
                    {
                        romi_db_configure(search_active ? search_text : NULL, &config);
                        reposition();
                    }
                    if (config_temp.music != config.music)
                        (config.music ? romi_start_music() : romi_stop_music());
                }
                else if (mres == MenuResultAccept)
                {
                    romi_menu_get(&config);
                    romi_save_config(&config);
                }
                else if (mres == MenuResultRefresh)
                {
                    state = StateRefreshing;
                    romi_start_thread("refresh_thread", &romi_refresh_thread);
                }
            }
        }

        romi_swap();
    }

    LOG("finished");
    mini18n_close();
    romi_free_texture(background);
    romi_end();
    return 0;
}
