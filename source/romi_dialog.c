#include "romi_dialog.h"
#include "romi_style.h"
#include "romi_utils.h"
#include "romi.h"

#include <sysutil/msg.h>
#include <mini18n.h>

typedef enum {
    DialogNone,
    DialogMessage,
    DialogError,
    DialogProgress,
    DialogOkCancel,
    DialogDetails
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
        if (slash)
            romi_strncpy(dialog_extra, sizeof(dialog_extra), slash + 1);
        else
            romi_strncpy(dialog_extra, sizeof(dialog_extra), item->url);
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
            romi_draw_text_z(ROMI_DIALOG_HMARGIN + ROMI_DIALOG_PADDING, ROMI_DIALOG_VMARGIN + ROMI_DIALOG_PADDING + font_height*6, ROMI_DIALOG_TEXT_Z, ROMI_COLOR_TEXT_DIALOG, local_extra);
        }

        if (local_allow_close)
        {
            char text[256];
            romi_snprintf(text, sizeof(text), _("press %s to close"), romi_ok_button() == ROMI_BUTTON_X ? ROMI_UTF8_X : ROMI_UTF8_O);
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
