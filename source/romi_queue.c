#include "romi_queue.h"
#include "romi.h"
#include "romi_download.h"
#include "romi_dialog.h"
#include <string.h>
#include <stdlib.h>
#include <mini18n.h>

static DownloadQueue g_download_queue = {0};
static int cancelled = 0;

static void romi_queue_start_next(void);
static void romi_queue_download_worker(void* arg);
static void queue_progress_callback(const char* status, uint64_t downloaded, uint64_t total);

void romi_queue_init(void)
{
    memset(&g_download_queue, 0, sizeof(g_download_queue));
    g_download_queue.max_concurrent = ROMI_QUEUE_MAX_CONCURRENT_DEFAULT;
    cancelled = 0;
}

void romi_queue_shutdown(void)
{
    romi_dialog_lock();

    DownloadQueueEntry* entry = g_download_queue.head;
    while (entry) {
        if (entry->status == DownloadStatusDownloading) {
            cancelled = 1;
            romi_download_cancel();
        }
        DownloadQueueEntry* next = entry->next;
        romi_free(entry);
        entry = next;
    }

    g_download_queue.head = NULL;
    g_download_queue.tail = NULL;
    g_download_queue.count = 0;
    g_download_queue.active_count = 0;

    romi_dialog_unlock();
}

int romi_queue_add(DbItem* item)
{
    if (!item)
        return 0;

    romi_dialog_lock();

    DownloadQueueEntry* entry = (DownloadQueueEntry*)romi_malloc(sizeof(DownloadQueueEntry));
    if (!entry) {
        romi_dialog_unlock();
        return 0;
    }

    memset(entry, 0, sizeof(DownloadQueueEntry));
    entry->item = item;
    entry->status = DownloadStatusPending;
    entry->thread_id = 0;
    entry->next = NULL;
    romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Pending..."));

    if (g_download_queue.tail) {
        g_download_queue.tail->next = entry;
    } else {
        g_download_queue.head = entry;
    }
    g_download_queue.tail = entry;
    g_download_queue.count++;

    if (g_download_queue.active_count < g_download_queue.max_concurrent) {
        entry->status = DownloadStatusDownloading;
        entry->start_time = romi_time_msec();
        romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Starting..."));
        g_download_queue.active_count++;

        romi_dialog_unlock();

        romi_start_thread_arg("download_worker", romi_queue_download_worker, entry);

        return 1;
    }

    romi_dialog_unlock();
    return 1;
}

int romi_queue_remove(DownloadQueueEntry* entry)
{
    if (!entry)
        return 0;

    romi_dialog_lock();

    DownloadQueueEntry* prev = NULL;
    DownloadQueueEntry* current = g_download_queue.head;

    while (current) {
        if (current == entry) {
            if (prev) {
                prev->next = current->next;
            } else {
                g_download_queue.head = current->next;
            }

            if (current == g_download_queue.tail) {
                g_download_queue.tail = prev;
            }

            g_download_queue.count--;

            if (current->status == DownloadStatusDownloading) {
                g_download_queue.active_count--;
            }

            romi_free(current);

            romi_dialog_unlock();
            return 1;
        }
        prev = current;
        current = current->next;
    }

    romi_dialog_unlock();
    return 0;
}

int romi_queue_cancel(DownloadQueueEntry* entry)
{
    if (!entry)
        return 0;

    romi_dialog_lock();

    if (entry->status == DownloadStatusDownloading) {
        cancelled = 1;
        romi_download_cancel();
        entry->status = DownloadStatusCancelled;
    }

    romi_dialog_unlock();
    return 1;
}

int romi_queue_retry(DownloadQueueEntry* entry)
{
    if (!entry)
        return 0;

    romi_dialog_lock();

    if (entry->status == DownloadStatusFailed || entry->status == DownloadStatusCancelled) {
        entry->status = DownloadStatusDownloading;
        entry->downloaded = 0;
        entry->start_time = romi_time_msec();
        entry->error_message[0] = '\0';
        romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Retrying..."));
        g_download_queue.active_count++;

        romi_dialog_unlock();

        romi_start_thread_arg("download_worker", romi_queue_download_worker, entry);

        return 1;
    }

    romi_dialog_unlock();
    return 0;
}

DownloadQueueEntry* romi_queue_get_entry(uint32_t index)
{
    DownloadQueueEntry* entry = g_download_queue.head;
    uint32_t i = 0;

    while (entry && i < index) {
        entry = entry->next;
        i++;
    }

    return entry;
}

uint32_t romi_queue_get_count(void)
{
    return g_download_queue.count;
}

uint32_t romi_queue_get_active_count(void)
{
    return g_download_queue.active_count;
}

static void romi_queue_start_next(void)
{
    DownloadQueueEntry* entry = g_download_queue.head;

    while (entry) {
        if (entry->status == DownloadStatusPending &&
            g_download_queue.active_count < g_download_queue.max_concurrent) {

            entry->status = DownloadStatusDownloading;
            entry->start_time = romi_time_msec();
            romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Starting..."));
            g_download_queue.active_count++;

            romi_start_thread_arg("download_worker", romi_queue_download_worker, entry);
            return;
        }
        entry = entry->next;
    }
}

static void romi_queue_download_worker(void* arg)
{
    DownloadQueueEntry* entry = (DownloadQueueEntry*)arg;

    // Set our thread ID so progress callback can find us
    sys_ppu_thread_t my_thread_id;
    sysThreadGetId(&my_thread_id);
    entry->thread_id = my_thread_id;

    cancelled = 0;
    romi_lock_process();
    int success = romi_download_rom(entry->item, queue_progress_callback);
    romi_unlock_process();

    romi_dialog_lock();

    if (success) {
        entry->status = DownloadStatusCompleted;
        entry->downloaded = entry->total;
        romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Completed"));
    } else if (cancelled) {
        entry->status = DownloadStatusCancelled;
        romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Cancelled"));
    } else {
        entry->status = DownloadStatusFailed;
        romi_strncpy(entry->status_text, sizeof(entry->status_text), _("Failed"));
        romi_strncpy(entry->error_message, sizeof(entry->error_message), _("Download failed"));
    }

    g_download_queue.active_count--;
    entry->thread_id = 0;

    romi_queue_start_next();

    romi_dialog_unlock();

    romi_thread_exit();
}

static void queue_progress_callback(const char* status, uint64_t downloaded, uint64_t total)
{
    // Get current thread ID
    sys_ppu_thread_t current_thread;
    sysThreadGetId(&current_thread);

    // Find our entry (don't lock - we're only reading thread_id which is set once)
    DownloadQueueEntry* entry = g_download_queue.head;
    while (entry) {
        if (entry->thread_id == current_thread) {
            // Found our entry - update progress fields atomically
            // These writes are simple assignments, safe without lock

            // Safety: clamp downloaded to never exceed total
            if (downloaded > total && total > 0) {
                downloaded = total;
            }

            entry->downloaded = downloaded;
            entry->total = total;

            uint32_t elapsed = romi_time_msec() - entry->start_time;
            if (elapsed > 0 && downloaded > 0) {
                entry->speed = (uint32_t)((downloaded * 1000ULL) / elapsed);
            }

            // Status text needs brief lock
            if (status && status[0]) {
                romi_dialog_lock();
                romi_strncpy(entry->status_text, sizeof(entry->status_text), status);
                romi_dialog_unlock();
            }

            break;
        }
        entry = entry->next;
    }
}
