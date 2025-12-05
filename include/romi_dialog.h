#pragma once

#include <stdint.h>
#include "romi_db.h"

#define MDIALOG_OK      0 
#define MDIALOG_YESNO   1 

typedef struct romi_input {
    uint64_t delta;   // microseconds from previous frame
    uint32_t pressed; // button pressed in last frame
    uint32_t down;    // button is currently down
    uint32_t active;  // button is pressed in last frame, or held down for a long time (10 frames)
} romi_input;

typedef void (*romi_dialog_callback_t)(int);

void romi_dialog_init(void);

int romi_dialog_is_open(void);
int romi_dialog_is_cancelled(void);
void romi_dialog_allow_close(int allow);
void romi_dialog_message(const char* title, const char* text);
void romi_dialog_error(const char* text);
void romi_dialog_details(DbItem* item, const char* type);
void romi_dialog_ok_cancel(const char* title, const char* text, romi_dialog_callback_t callback);

void romi_dialog_start_progress(const char* title, const char* text, float progress);
void romi_dialog_set_progress_title(const char* title);
void romi_dialog_set_progress(const char* text, int percent);
void romi_dialog_update_progress(const char* text, const char* extra, const char* eta, float progress);

void romi_dialog_close(void);

void romi_dialog_open_download_queue(void);
void romi_dialog_open_device_selection(void);

void romi_do_dialog(romi_input* input);

int romi_msg_dialog(int tdialog, const char * str);
