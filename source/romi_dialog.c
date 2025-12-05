#include "romi_dialog.h"
#include "romi_style.h"
#include "romi_utils.h"
#include "romi.h"
#include "romi_queue.h"
#include "romi_devices.h"
#include "romi_config.h"

#include <sysutil/msg.h>
#include <mini18n.h>

typedef enum {
    DialogNone,
    DialogMessage,
    DialogError,
    DialogProgress,
    DialogOkCancel,
    DialogDetails,
    DialogDownloadQueue,
    DialogDeviceSelection
} DialogType;

static DialogType dialog_type;
static char dialog_title[256];
static char dialog_text[256];
static char dialog_extra[256];
static char dialog_eta[256];
static float dialog_progress;
static int dialog_allow_close;
static int dialog_cancelled;
static DbItem* db_item = NULL;
static romi_dialog_callback_t dialog_callback = NULL;

static int32_t dialog_width;
static int32_t dialog_height;
static int32_t dialog_delta;

static uint32_t queue_selected_row = 0;
static uint32_t queue_scroll_offset = 0;
static uint32_t queue_visible_rows = 0;

static uint32_t device_selected_row = 0;
static uint32_t device_scroll_offset = 0;

#define ROMI_QUEUE_ROW_HEIGHT 50
#define ROMI_QUEUE_MAX_VISIBLE_ROWS 2
#define ROMI_DEVICE_ROW_HEIGHT 40
#define ROMI_DEVICE_MAX_VISIBLE_ROWS 8

volatile int msg_dialog_action = 0;

void romi_dialog_init(void)
{
    dialog_type = DialogNone;
    dialog_allow_close = 1;
}

int romi_dialog_is_open(void)
{
    return dialog_type != DialogNone;
}

int romi_dialog_is_cancelled(void)
{
    return dialog_cancelled;
}

void romi_dialog_allow_close(int allow)
{
    romi_dialog_lock();
    dialog_allow_close = allow;
    romi_dialog_unlock();
}

void romi_dialog_data_init(DialogType type, const char* title, const char* text)
{
    romi_strncpy(dialog_title, sizeof(dialog_title), title);
    romi_strncpy(dialog_text, sizeof(dialog_text), text);
    dialog_extra[0] = 0;
    dialog_eta[0] = 0;

    dialog_cancelled = 0;
    dialog_type = type;
    dialog_delta = 1;
}

static const char* platform_name(RomiPlatform p)
{
    switch (p)
    {
        case PlatformPSX: return "PlayStation";
        case PlatformPS2: return "PlayStation 2";
        case PlatformPS3: return "PlayStation 3";
        case PlatformNES: return "Nintendo Entertainment System";
        case PlatformSNES: return "Super Nintendo";
        case PlatformGB: return "Game Boy";
        case PlatformGBC: return "Game Boy Color";
        case PlatformGBA: return "Game Boy Advance";
        case PlatformGenesis: return "Sega Genesis";
        case PlatformSMS: return "Sega Master System";
        case PlatformMAME: return "Arcade (MAME)";
        default: return "Unknown";
    }
}

static const char* region_name(RomiRegion r)
{
    switch (r)
    {
        case RegionUSA: return "USA";
        case RegionEUR: return "Europe";
        case RegionJPN: return "Japan";
        case RegionWorld: return "World";
        case RegionASA: return "Asia";
        default: return "Unknown";
    }
}

void romi_dialog_details(DbItem *item, const char* content_type)
{
    romi_dialog_lock();

    char size_str[64];
    if (item->size > 0)
    {
        if (item->size < 1024LL * 1024)
            romi_snprintf(size_str, sizeof(size_str), "%.2f KB", item->size / 1024.f);
        else if (item->size < 1024LL * 1024 * 1024)
            romi_snprintf(size_str, sizeof(size_str), "%.2f MB", item->size / 1024.f / 1024.f);
        else
            romi_snprintf(size_str, sizeof(size_str), "%.2f GB", item->size / 1024.f / 1024.f / 1024.f);
    }
    else
    {
        romi_strncpy(size_str, sizeof(size_str), "Unknown");
    }

    romi_snprintf(dialog_extra, sizeof(dialog_extra),
        "%s: %s\n%s: %s\n%s: %s",
        _("Platform"), platform_name(item->platform),
        _("Region"), region_name(item->region),
        _("Size"), size_str);

    romi_dialog_data_init(DialogDetails, item->name, dialog_extra);

    if (item->url)
    {
        const char* slash = romi_strrchr(item->url, '/');
        const char* filename = slash ? slash + 1 : item->url;
        const char* ext = romi_strrchr(filename, '.');
        if (ext)
            romi_snprintf(dialog_extra, sizeof(dialog_extra), "%s: %s", _("Extension"), ext);
        else
            dialog_extra[0] = 0;
    }
    else
    {
        dialog_extra[0] = 0;
    }

    db_item = item;
    romi_dialog_unlock();
}

void romi_dialog_message(const char* title, const char* text)
{
    romi_dialog_lock();
    romi_dialog_data_init(DialogMessage, title, text);
    romi_dialog_unlock();
}

void romi_dialog_ok_cancel(const char* title, const char* text, romi_dialog_callback_t callback)
{
    romi_dialog_lock();
    romi_dialog_data_init(DialogOkCancel, title, text);
    dialog_callback = callback;
    romi_dialog_unlock();
}

void romi_dialog_error(const char* text)
{
    romi_dialog_lock();
    romi_dialog_data_init(DialogError, _("ERROR"), text);
    romi_dialog_unlock();
}

void romi_dialog_start_progress(const char* title, const char* text, float progress)
{
    romi_dialog_lock();
    romi_dialog_data_init(DialogProgress, title, text);
    dialog_progress = progress;
    romi_dialog_unlock();
}

void romi_dialog_set_progress(const char* text, int percent)
{
    romi_dialog_lock();
    romi_strncpy(dialog_text, sizeof(dialog_text), text);
    dialog_progress = (percent < 0) ? -1.0f : percent / 100.0f;
    romi_dialog_unlock();
}

void romi_dialog_set_progress_title(const char* title)
{
    romi_dialog_lock();
    romi_strncpy(dialog_title, sizeof(dialog_title), title);
    romi_dialog_unlock();
}

void romi_dialog_update_progress(const char* text, const char* extra, const char* eta, float progress)
{
    romi_dialog_lock();

    romi_strncpy(dialog_text, sizeof(dialog_text), text);
    romi_strncpy(dialog_extra, sizeof(dialog_extra), extra ? extra : "");
    romi_strncpy(dialog_eta, sizeof(dialog_eta), eta ? eta : "");

    dialog_progress = (progress > 1.0f) ? 1.0f : progress;

    romi_dialog_unlock();
}

void romi_dialog_close(void)
{
    dialog_delta = -1;
}

void romi_dialog_open_download_queue(void)
{
    romi_dialog_lock();
    romi_dialog_data_init(DialogDownloadQueue, _("Download Queue"), "");
    queue_selected_row = 0;
    queue_scroll_offset = 0;
    queue_visible_rows = 0;
    romi_dialog_unlock();
}

void romi_dialog_open_device_selection(void)
{
    romi_dialog_lock();
    romi_dialog_data_init(DialogDeviceSelection, _("Storage Device"), "");
    romi_devices_scan();
    device_selected_row = romi_devices_get_selected_index();
    device_scroll_offset = 0;
    if (device_selected_row >= ROMI_DEVICE_MAX_VISIBLE_ROWS)
        device_scroll_offset = device_selected_row - ROMI_DEVICE_MAX_VISIBLE_ROWS + 1;
    romi_dialog_unlock();
}

void romi_do_dialog(romi_input* input)
{
    romi_dialog_lock();

    if (dialog_allow_close)
    {
        if ((dialog_type == DialogMessage || dialog_type == DialogError || dialog_type == DialogDetails) && (input->pressed & romi_ok_button()))
        {
            dialog_delta = -1;
        }
        else if ((dialog_type == DialogProgress || dialog_type == DialogOkCancel) && (input->pressed & romi_cancel_button()))
        {
            dialog_cancelled = 1;
        }
        else if (dialog_type == DialogOkCancel && (input->pressed & romi_ok_button()))
        {
            dialog_delta = -1;
            if (dialog_callback)
            {
                dialog_callback(MDIALOG_OK);
                dialog_callback = NULL;
            }
        }
        else if (dialog_type == DialogDownloadQueue)
        {
            uint32_t queue_count = romi_queue_get_count();

            // Up/Down navigation
            if ((input->pressed & ROMI_BUTTON_UP) && queue_selected_row > 0)
            {
                queue_selected_row--;
                if (queue_selected_row < queue_scroll_offset)
                    queue_scroll_offset = queue_selected_row;
            }
            else if ((input->pressed & ROMI_BUTTON_DOWN) && queue_selected_row < queue_count - 1)
            {
                queue_selected_row++;
                if (queue_selected_row >= queue_scroll_offset + ROMI_QUEUE_MAX_VISIBLE_ROWS)
                    queue_scroll_offset = queue_selected_row - ROMI_QUEUE_MAX_VISIBLE_ROWS + 1;
            }

            // Square button: Close dialog, keep downloads running
            if (input->pressed & ROMI_BUTTON_S)
            {
                dialog_delta = -1;
            }

            // X button: Retry failed or remove completed
            if (input->pressed & romi_ok_button())
            {
                DownloadQueueEntry* entry = romi_queue_get_entry(queue_selected_row);
                if (entry)
                {
                    if (entry->status == DownloadStatusFailed || entry->status == DownloadStatusCancelled)
                    {
                        romi_queue_retry(entry);
                    }
                    else if (entry->status == DownloadStatusCompleted)
                    {
                        romi_queue_remove(entry);
                        if (queue_selected_row >= romi_queue_get_count() && queue_selected_row > 0)
                            queue_selected_row--;
                        if (romi_queue_get_count() == 0)
                            dialog_delta = -1;
                    }
                }
            }

            // Circle button: Cancel download or remove entry
            if (input->pressed & romi_cancel_button())
            {
                DownloadQueueEntry* entry = romi_queue_get_entry(queue_selected_row);
                if (entry)
                {
                    if (entry->status == DownloadStatusDownloading)
                    {
                        romi_queue_cancel(entry);
                    }
                    else
                    {
                        romi_queue_remove(entry);
                        if (queue_selected_row >= romi_queue_get_count() && queue_selected_row > 0)
                            queue_selected_row--;
                        if (romi_queue_get_count() == 0)
                            dialog_delta = -1;
                    }
                }
            }
        }
        else if (dialog_type == DialogDeviceSelection)
        {
            uint32_t device_count = romi_devices_count();

            // Up/Down navigation
            if ((input->pressed & ROMI_BUTTON_UP) && device_selected_row > 0)
            {
                device_selected_row--;
                if (device_selected_row < device_scroll_offset)
                    device_scroll_offset = device_selected_row;
            }
            else if ((input->pressed & ROMI_BUTTON_DOWN) && device_selected_row < device_count - 1)
            {
                device_selected_row++;
                if (device_selected_row >= device_scroll_offset + ROMI_DEVICE_MAX_VISIBLE_ROWS)
                    device_scroll_offset = device_selected_row - ROMI_DEVICE_MAX_VISIBLE_ROWS + 1;
            }

            // X button: Select device
            if (input->pressed & romi_ok_button())
            {
                romi_devices_set_selected(device_selected_row);

                Config config;
                romi_load_config(&config);
                const RomiDevice* device = romi_devices_get_selected();
                if (device) {
                    romi_strncpy(config.storage_device_path, sizeof(config.storage_device_path), device->path);
                    romi_save_config(&config);
                    LOG("saved device path to config: %s", device->path);
                }

                dialog_delta = -1;
            }

            // Circle button: Cancel selection
            if (input->pressed & romi_cancel_button())
            {
                dialog_delta = -1;
            }
        }
    }

    if (dialog_delta != 0)
    {
        dialog_width += dialog_delta * (int32_t)(input->delta * ROMI_ANIMATION_SPEED / 1000);
        dialog_height += dialog_delta * (int32_t)(input->delta * ROMI_ANIMATION_SPEED / 500);

        if (dialog_delta < 0 && (dialog_width <= 0 || dialog_height <= 0))
        {
            dialog_type = DialogNone;
            dialog_text[0] = 0;
            dialog_extra[0] = 0;
            dialog_eta[0] = 0;

            dialog_width = 0;
            dialog_height = 0;
            dialog_delta = 0;

            romi_dialog_unlock();
            return;
        }
        else if (dialog_delta > 0)
        {
            if (dialog_width >= ROMI_DIALOG_WIDTH && dialog_height >= ROMI_DIALOG_HEIGHT)
            {
                dialog_delta = 0;
            }
            dialog_width = min32(dialog_width, ROMI_DIALOG_WIDTH);
            dialog_height = min32(dialog_height, ROMI_DIALOG_HEIGHT);
        }
    }

    DialogType local_type = dialog_type;
    char local_title[256];
    char local_text[256];
    char local_extra[256];
    char local_eta[256];
    float local_progress = dialog_progress;
    int local_allow_close = dialog_allow_close;
    int32_t local_width = dialog_width;
    int32_t local_height = dialog_height;

    romi_strncpy(local_title, sizeof(local_title), dialog_title);
    romi_strncpy(local_text, sizeof(local_text), dialog_text);
    romi_strncpy(local_extra, sizeof(local_extra), dialog_extra);
    romi_strncpy(local_eta, sizeof(local_eta), dialog_eta);

    romi_dialog_unlock();

    if (local_width != 0 && local_height != 0)
    {
        romi_draw_fill_rect_z((VITA_WIDTH - local_width) / 2, (VITA_HEIGHT - local_height) / 2, ROMI_MENU_Z, local_width, local_height, ROMI_COLOR_MENU_BACKGROUND);
        romi_draw_rect_z((VITA_WIDTH - local_width) / 2, (VITA_HEIGHT - local_height) / 2, ROMI_MENU_Z, local_width, local_height, ROMI_COLOR_MENU_BORDER);
    }

    if (local_width != ROMI_DIALOG_WIDTH || local_height != ROMI_DIALOG_HEIGHT)
    {
        return;
    }

    int font_height = romi_text_height("M");

    int w = VITA_WIDTH - 2 * ROMI_DIALOG_HMARGIN;
    int h = VITA_HEIGHT - 2 * ROMI_DIALOG_VMARGIN;

    if (local_title[0])
    {
        uint32_t color;
        if (local_type == DialogError)
        {
            color = ROMI_COLOR_TEXT_ERROR;
        }
        else
        {
            color = ROMI_COLOR_TEXT_DIALOG;
        }

        int width = romi_text_width_ttf(local_title);
        if (width > w + 2 * ROMI_DIALOG_PADDING)
        {
            romi_clip_set(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, ROMI_DIALOG_VMARGIN + font_height, w - 2 * ROMI_DIALOG_PADDING, h - 2 * ROMI_DIALOG_PADDING);
            romi_draw_text_ttf(0, 0, ROMI_DIALOG_TEXT_Z, color, local_title);
            romi_clip_remove();
        }
        else
        {
            romi_draw_text_ttf((VITA_WIDTH - width) / 2, ROMI_DIALOG_VMARGIN + font_height, ROMI_DIALOG_TEXT_Z, color, local_title);
        }
    }

    if (local_type == DialogProgress)
    {
        int extraw = romi_text_width(local_extra);

        int availw = VITA_WIDTH - 2 * (ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING) - (extraw ? extraw + 10 : 10);
        romi_clip_set(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, VITA_HEIGHT / 2 - font_height - ROMI_DIALOG_PROCESS_BAR_PADDING, availw, font_height + 2);
        romi_draw_text_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, VITA_HEIGHT / 2 - font_height - ROMI_DIALOG_PROCESS_BAR_PADDING, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, local_text);
        romi_clip_remove();

        if (local_extra[0])
        {
            romi_draw_text_z(ROMI_DIALOG_HMARGIN + w - (ROMI_DIALOG_PADDING + extraw), VITA_HEIGHT / 2 - font_height - ROMI_DIALOG_PROCESS_BAR_PADDING, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, local_extra);
        }

        if (local_progress < 0)
        {
            uint32_t avail = w - 2 * ROMI_DIALOG_PADDING;

            uint32_t start = (romi_time_msec() / 2) % (avail + ROMI_DIALOG_PROCESS_BAR_CHUNK);
            uint32_t end = start < ROMI_DIALOG_PROCESS_BAR_CHUNK ? start : start + ROMI_DIALOG_PROCESS_BAR_CHUNK > avail + ROMI_DIALOG_PROCESS_BAR_CHUNK ? avail : start;
            start = start < ROMI_DIALOG_PROCESS_BAR_CHUNK ? 0 : start - ROMI_DIALOG_PROCESS_BAR_CHUNK;

            romi_draw_fill_rect_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, VITA_HEIGHT / 2, ROMI_MENU_Z, avail, ROMI_DIALOG_PROCESS_BAR_HEIGHT, ROMI_COLOR_PROGRESS_BACKGROUND);
            romi_draw_fill_rect_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING + start, VITA_HEIGHT / 2, ROMI_MENU_Z, end - start, ROMI_DIALOG_PROCESS_BAR_HEIGHT, ROMI_COLOR_PROGRESS_BAR);
        }
        else
        {
            romi_draw_fill_rect_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, VITA_HEIGHT / 2, ROMI_MENU_Z, w - 2 * ROMI_DIALOG_PADDING, ROMI_DIALOG_PROCESS_BAR_HEIGHT, ROMI_COLOR_PROGRESS_BACKGROUND);
            romi_draw_fill_rect_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, VITA_HEIGHT / 2, ROMI_MENU_Z, (int)((w - 2 * ROMI_DIALOG_PADDING) * local_progress), ROMI_DIALOG_PROCESS_BAR_HEIGHT, ROMI_COLOR_PROGRESS_BAR);

            char percent[256];
            romi_snprintf(percent, sizeof(percent), "%.0f%%", local_progress * 100.f);

            int percentw = romi_text_width(percent);
            romi_draw_text_z((VITA_WIDTH - percentw) / 2, VITA_HEIGHT / 2 + ROMI_DIALOG_PROCESS_BAR_HEIGHT + ROMI_DIALOG_PROCESS_BAR_PADDING, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, percent);
        }

        if (local_eta[0])
        {
            romi_draw_text_z(ROMI_DIALOG_HMARGIN + w - (ROMI_DIALOG_PADDING + romi_text_width(local_eta)), VITA_HEIGHT / 2 + ROMI_DIALOG_PROCESS_BAR_HEIGHT + ROMI_DIALOG_PROCESS_BAR_PADDING, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, local_eta);
        }

        if (local_allow_close)
        {
            char text[256];
            romi_snprintf(text, sizeof(text), _("press %s to cancel"), romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_O : ROMI_UTF8_X);
            romi_draw_text_z((VITA_WIDTH - romi_text_width(text)) / 2, ROMI_DIALOG_VMARGIN + h - 2 * font_height, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, text);
        }
    }
    else if (local_type == DialogDetails)
    {
        romi_draw_text_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, ROMI_DIALOG_VMARGIN + ROMI_DIALOG_PADDING + font_height*2, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, local_text);

        if (local_extra[0])
        {
            romi_draw_text_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, ROMI_DIALOG_VMARGIN + ROMI_DIALOG_PADDING + font_height*5, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, local_extra);
        }

        if (local_allow_close)
        {
            char text[256];
            romi_snprintf(text, sizeof(text), _("press %s to close"), romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O);
            romi_draw_text_z((VITA_WIDTH - romi_text_width(text)) / 2, ROMI_DIALOG_VMARGIN + h - 2 * font_height, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, text);
        }
    }
    else if (local_type == DialogDownloadQueue)
    {
        int y_offset = ROMI_DIALOG_VMARGIN + ROMI_DIALOG_PADDING + font_height * 2;
        uint32_t queue_count = romi_queue_get_count();
        uint32_t visible_rows = min32(queue_count, ROMI_QUEUE_MAX_VISIBLE_ROWS);

        // Render queue entries
        for (uint32_t i = 0; i < visible_rows; i++)
        {
            uint32_t entry_index = queue_scroll_offset + i;
            DownloadQueueEntry* entry = romi_queue_get_entry(entry_index);

            if (!entry)
                break;

            int row_y = y_offset + (i * ROMI_QUEUE_ROW_HEIGHT);
            int row_x = ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING;
            int row_width = w - 2 * ROMI_DIALOG_PADDING;
            
            // Draw selection highlight BEHIND text (higher Z = further back)
            if (entry_index == queue_selected_row)
            {
                romi_draw_fill_rect_z(row_x, row_y, ROMI_DIALOG_TEXT_Z + 10, row_width, ROMI_QUEUE_ROW_HEIGHT - 2, ROMI_COLOR_SELECTED_BACKGROUND);
            }

            // Build status text first to calculate its width
            char status_text[64];
            if (entry->status == DownloadStatusDownloading && entry->speed > 0)
            {
                if (entry->speed > 1024 * 1024)
                    romi_snprintf(status_text, sizeof(status_text), "%.1f MB/s", entry->speed / (1024.0f * 1024.0f));
                else if (entry->speed > 1024)
                    romi_snprintf(status_text, sizeof(status_text), "%.1f KB/s", entry->speed / 1024.0f);
                else
                    romi_snprintf(status_text, sizeof(status_text), "%u B/s", entry->speed);
            }
            else
            {
                romi_strncpy(status_text, sizeof(status_text), entry->status_text);
            }
            int status_text_width = romi_text_width(status_text);

            // Draw filename - truncate to avoid overlap with status text
            char filename_buf[128];
            const char* filename = entry->item ? entry->item->name : "NO ITEM";
            int filename_max_width = row_width - status_text_width - 20;  // 20px gap between name and status
            romi_truncate_text(filename_buf, sizeof(filename_buf), filename, filename_max_width);
            romi_draw_text_z(row_x + 5, row_y + 3, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, filename_buf);

            // Draw status text on right side
            romi_draw_text_z(row_x + row_width - status_text_width - 5, row_y + 3, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, status_text);

            // Draw progress bar for active downloads (below the title text)
            if ((entry->status == DownloadStatusDownloading || entry->status == DownloadStatusExtracting) && entry->total > 0)
            {
                int progress_y = row_y + 22;
                int progress_width = row_width - 80;
                float progress_ratio = (float)entry->downloaded / (float)entry->total;
                if (progress_ratio > 1.0f) progress_ratio = 1.0f;  // Clamp to 100% max
                int percent = (int)(progress_ratio * 100);

                // Progress bar background (behind fill)
                romi_draw_fill_rect_z(row_x + 5, progress_y, ROMI_DIALOG_TEXT_Z + 5, progress_width, 10, ROMI_COLOR_PROGRESS_BACKGROUND);
                // Progress bar fill (in front of background)
                romi_draw_fill_rect_z(row_x + 5, progress_y, ROMI_DIALOG_TEXT_Z, (int)(progress_width * progress_ratio), 10, ROMI_COLOR_PROGRESS_BAR);
                
                // Progress percentage on the right side
                char percent_text[16];
                romi_snprintf(percent_text, sizeof(percent_text), "%d%%", percent);
                romi_draw_text_z(row_x + row_width - 50, progress_y - 2, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, percent_text);
            }
        }

        // Draw scrollbar if needed
        if (queue_count > ROMI_QUEUE_MAX_VISIBLE_ROWS)
        {
            int scrollbar_x = ROMI_DIALOG_HMARGIN + w - 5;
            int scrollbar_y = y_offset;
            int scrollbar_height = ROMI_QUEUE_MAX_VISIBLE_ROWS * ROMI_QUEUE_ROW_HEIGHT;
            int thumb_height = max32(20, (ROMI_QUEUE_MAX_VISIBLE_ROWS * scrollbar_height) / queue_count);
            int thumb_y = scrollbar_y + ((queue_scroll_offset * scrollbar_height) / queue_count);

            romi_draw_fill_rect_z(scrollbar_x, scrollbar_y, ROMI_DIALOG_TEXT_Z, 3, scrollbar_height, ROMI_COLOR_PROGRESS_BACKGROUND);
            romi_draw_fill_rect_z(scrollbar_x, thumb_y, ROMI_DIALOG_TEXT_Z, 3, thumb_height, ROMI_COLOR_PROGRESS_BAR);
        }

        // Instructions at bottom - dynamic based on selected entry status
        if (local_allow_close)
        {
            char text[256];
            DownloadQueueEntry* selected = romi_queue_get_entry(queue_selected_row);

            if (selected)
            {
                const char* ok_button_str = romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O;
                const char* cancel_button_str = romi_cancel_button() == ROMI_BUTTON_O ? ROMI_UTF8_O : ROMI_UTF8_X;

                if (selected->status == DownloadStatusFailed || selected->status == DownloadStatusCancelled)
                {
                    // X=retry, O=remove, Square=hide
                    romi_snprintf(text, sizeof(text), "%s %s  %s %s  %s %s",
                        ok_button_str, _("retry"),
                        cancel_button_str, _("remove"),
                        ROMI_UTF8_SQUARE, _("hide"));
                }
                else if (selected->status == DownloadStatusDownloading)
                {
                    // O=cancel, Square=hide
                    romi_snprintf(text, sizeof(text), "%s %s  %s %s",
                        cancel_button_str, _("cancel"),
                        ROMI_UTF8_SQUARE, _("hide"));
                }
                else if (selected->status == DownloadStatusCompleted)
                {
                    // X=remove, Square=hide
                    romi_snprintf(text, sizeof(text), "%s %s  %s %s",
                        ok_button_str, _("remove"),
                        ROMI_UTF8_SQUARE, _("hide"));
                }
                else
                {
                    // Default: O=remove, Square=hide
                    romi_snprintf(text, sizeof(text), "%s %s  %s %s",
                        cancel_button_str, _("remove"),
                        ROMI_UTF8_SQUARE, _("hide"));
                }
            }
            else
            {
                // No selection, just show hide
                romi_snprintf(text, sizeof(text), "%s %s",
                    ROMI_UTF8_SQUARE, _("hide"));
            }

            romi_draw_text_z((VITA_WIDTH - romi_text_width(text)) / 2, ROMI_DIALOG_VMARGIN + h - 2 * font_height, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, text);
        }
    }
    else if (local_type == DialogDeviceSelection)
    {
        int y_offset = ROMI_DIALOG_VMARGIN + ROMI_DIALOG_PADDING + font_height * 2;
        uint32_t device_count = romi_devices_count();
        uint32_t visible_rows = min32(device_count, ROMI_DEVICE_MAX_VISIBLE_ROWS);

        // Render device entries
        for (uint32_t i = 0; i < visible_rows; i++)
        {
            uint32_t device_index = device_scroll_offset + i;
            const RomiDevice* device = romi_devices_get(device_index);

            if (!device)
                break;

            int row_y = y_offset + (i * ROMI_DEVICE_ROW_HEIGHT);
            int row_x = ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING;
            int row_width = w - 2 * ROMI_DIALOG_PADDING;

            // Draw selection highlight
            if (device_index == device_selected_row)
            {
                romi_draw_fill_rect_z(row_x, row_y, ROMI_DIALOG_TEXT_Z + 10, row_width, ROMI_DEVICE_ROW_HEIGHT - 2, ROMI_COLOR_SELECTED_BACKGROUND);
            }

            // Draw device path
            char device_text[256];
            if (device->available)
            {
                romi_snprintf(device_text, sizeof(device_text), "%s", device->path);
            }
            else
            {
                romi_snprintf(device_text, sizeof(device_text), "%s [unavailable]", device->path);
            }

            romi_draw_text_z(row_x + 5, row_y + 10, ROMI_DIALOG_TEXT_Z,
                device->available ? ROMI_COLOR_TEXT_DIALOG : ROMI_COLOR_TEXT_ERROR,
                device_text);
        }

        // Draw scrollbar if needed
        if (device_count > ROMI_DEVICE_MAX_VISIBLE_ROWS)
        {
            int scrollbar_x = ROMI_DIALOG_HMARGIN + w - 5;
            int scrollbar_y = y_offset;
            int scrollbar_height = ROMI_DEVICE_MAX_VISIBLE_ROWS * ROMI_DEVICE_ROW_HEIGHT;
            int thumb_height = max32(20, (ROMI_DEVICE_MAX_VISIBLE_ROWS * scrollbar_height) / device_count);
            int thumb_y = scrollbar_y + ((device_scroll_offset * scrollbar_height) / device_count);

            romi_draw_fill_rect_z(scrollbar_x, scrollbar_y, ROMI_DIALOG_TEXT_Z, 3, scrollbar_height, ROMI_COLOR_PROGRESS_BACKGROUND);
            romi_draw_fill_rect_z(scrollbar_x, thumb_y, ROMI_DIALOG_TEXT_Z, 3, thumb_height, ROMI_COLOR_PROGRESS_BAR);
        }

        // Instructions at bottom
        if (local_allow_close)
        {
            char text[256];
            const char* ok_button_str = romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O;
            const char* cancel_button_str = romi_cancel_button() == ROMI_BUTTON_O ? ROMI_UTF8_O : ROMI_UTF8_X;

            romi_snprintf(text, sizeof(text), "%s %s  %s %s",
                ok_button_str, _("select"),
                cancel_button_str, _("cancel"));

            romi_draw_text_z((VITA_WIDTH - romi_text_width(text)) / 2, ROMI_DIALOG_VMARGIN + h - 2 * font_height, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, text);
        }
    }
    else
    {
        uint32_t color;
        if (local_type == DialogMessage || local_type == DialogOkCancel)
        {
            color = ROMI_COLOR_TEXT_DIALOG;
        }
        else
        {
            color = ROMI_COLOR_TEXT_ERROR;
        }

        int textw = romi_text_width(local_text);
        if (textw > w + 2 * ROMI_DIALOG_PADDING)
        {
            romi_clip_set(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, ROMI_DIALOG_VMARGIN + ROMI_DIALOG_PADDING, w - 2 * ROMI_DIALOG_PADDING, h - 2 * ROMI_DIALOG_PADDING);
            romi_draw_text_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, VITA_HEIGHT / 2 - font_height / 2, ROMI_DIALOG_TEXT_Z, color, local_text);
            romi_clip_remove();
        }
        else
        {
            romi_draw_text_z((VITA_WIDTH - textw) / 2, VITA_HEIGHT / 2 - font_height / 2, ROMI_DIALOG_TEXT_Z, color, local_text);
        }

        if (local_allow_close)
        {
            char text[256];
            if (local_type == DialogOkCancel)
                romi_snprintf(text, sizeof(text), "%s %s  %s %s", romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O, _("Enter"), romi_cancel_button() == ROMI_BUTTON_O ? ROMI_UTF8_O : ROMI_UTF8_X, _("Back"));
            else
                romi_snprintf(text, sizeof(text), _("press %s to close"), romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O);

            romi_draw_text_z((VITA_WIDTH - romi_text_width(text)) / 2, ROMI_DIALOG_VMARGIN + h - 2 * font_height, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, text);
        }
    }
}

void msg_dialog_event(msgButton button, void *userdata)
{
    switch(button) {

        case MSG_DIALOG_BTN_YES:
            msg_dialog_action = 1;
            break;
        case MSG_DIALOG_BTN_NO:
        case MSG_DIALOG_BTN_ESCAPE:
        case MSG_DIALOG_BTN_NONE:
            msg_dialog_action = 2;
            break;
        default:
		    break;
    }
}

int romi_msg_dialog(int tdialog, const char * str)
{
    msg_dialog_action = 0;

    msgType mtype = MSG_DIALOG_NORMAL;
    mtype |= (tdialog ? (MSG_DIALOG_BTN_TYPE_YESNO  | MSG_DIALOG_DEFAULT_CURSOR_NO) : MSG_DIALOG_BTN_TYPE_OK);

    msgDialogOpen2(mtype, str, msg_dialog_event, NULL, NULL);

    while(!msg_dialog_action)
    {
        romi_swap();
    }

    msgDialogAbort();
    romi_sleep(100);

    return (msg_dialog_action == 1);
}
