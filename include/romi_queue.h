#pragma once

#include "romi_db.h"
#include <sys/thread.h>
#include <stdint.h>

typedef enum {
    DownloadStatusPending,
    DownloadStatusDownloading,
    DownloadStatusExtracting,
    DownloadStatusCompleted,
    DownloadStatusFailed,
    DownloadStatusCancelled
} DownloadStatus;

typedef struct DownloadQueueEntry {
    DbItem* item;
    DownloadStatus status;
    uint64_t downloaded;
    uint64_t total;
    uint32_t speed;
    char status_text[128];
    char error_message[256];
    sys_ppu_thread_t thread_id;
    uint32_t start_time;
    struct DownloadQueueEntry* next;
} DownloadQueueEntry;

typedef struct {
    DownloadQueueEntry* head;
    DownloadQueueEntry* tail;
    uint32_t count;
    uint32_t active_count;
    uint32_t max_concurrent;
} DownloadQueue;

#define ROMI_QUEUE_MAX_CONCURRENT_DEFAULT 3
#define ROMI_QUEUE_MAX_CONCURRENT_LIMIT 4

void romi_queue_init(void);
void romi_queue_shutdown(void);
int romi_queue_add(DbItem* item);
int romi_queue_remove(DownloadQueueEntry* entry);
int romi_queue_cancel(DownloadQueueEntry* entry);
int romi_queue_retry(DownloadQueueEntry* entry);
DownloadQueueEntry* romi_queue_get_entry(uint32_t index);
uint32_t romi_queue_get_count(void);
uint32_t romi_queue_get_active_count(void);
