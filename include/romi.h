#pragma once

#include <stdint.h>
#include <stdarg.h>
#include "romi_dialog.h"

#define ROMI_VERSION        "0.1"

#define ROMI_BUTTON_SELECT 0x00010000
#define ROMI_BUTTON_START  0x00080000
#define ROMI_BUTTON_UP     0x00100000
#define ROMI_BUTTON_RIGHT  0x00200000
#define ROMI_BUTTON_DOWN   0x00400000
#define ROMI_BUTTON_LEFT   0x00800000

#define ROMI_BUTTON_LT     0x00000004 // L1
#define ROMI_BUTTON_RT     0x00000008 // R1
#define ROMI_BUTTON_L2     0x00000001
#define ROMI_BUTTON_R2     0x00000002

#define ROMI_BUTTON_X 0x00000040 // cross
#define ROMI_BUTTON_O 0x00000020 // circle
#define ROMI_BUTTON_T 0x00000010 // triangle
#define ROMI_BUTTON_S 0x00000080 // square

#define ROMI_UNUSED(x) (void)(x)

#define ROMI_APP_FOLDER     "/dev_hdd0/game/ROMI00001/USRDIR"
#define ROMI_TMP_FOLDER     "/dev_hdd0/tmp/romi"
#define ROMI_CONFIG_FOLDER  "/dev_hdd0/game/ROMI00001/USRDIR"


#define ROMI_COUNTOF(arr) (sizeof(arr)/sizeof(0[arr]))

#ifdef ROMI_ENABLE_LOGGING
#include <stddef.h>
#include <dbglogger.h>
#ifdef ROMI_FILE_LOGGING
void romi_dual_log(const char* format, ...);
#define LOG romi_dual_log
#else
#define LOG dbglogger_log
#endif
#else
#define LOG(...)
#endif

int romi_snprintf(char* buffer, uint32_t size, const char* msg, ...);
void romi_vsnprintf(char* buffer, uint32_t size, const char* msg, va_list args);
char* romi_strstr(const char* str, const char* sub);
int romi_stricontains(const char* str, const char* sub);
int romi_stricmp(const char* a, const char* b);
void romi_strncpy(char* dst, uint32_t size, const char* src);
char* romi_strrchr(const char* str, char ch);
uint32_t romi_strlen(const char *str);
int64_t romi_strtoll(const char* str);
void romi_memcpy(void* dst, const void* src, uint32_t size);
void romi_memmove(void* dst, const void* src, uint32_t size);
int romi_memequ(const void* a, const void* b, uint32_t size);
void* romi_malloc(uint32_t size);
void romi_free(void* ptr);

int romi_is_unsafe_mode(void);

int romi_ok_button(void);
int romi_cancel_button(void);

void romi_start(void);
int romi_update(romi_input* input);
void romi_swap(void);
void romi_end(void);

int romi_temperature_is_high(void);
int romi_get_temperature(uint8_t cpu);

uint64_t romi_get_free_space(void);
const char* romi_get_config_folder(void);
const char* romi_get_temp_folder(void);
const char* romi_get_app_folder(void);
int romi_is_incomplete(const char* titleid);
int romi_is_installed(const char* titleid);
int romi_install(const char* titleid);

uint32_t romi_time_msec();

typedef void romi_thread_entry(void);
void romi_start_thread(const char* name, romi_thread_entry* start);
void romi_thread_exit(void);
void romi_sleep(uint32_t msec);

int romi_load(const char* name, void* data, uint32_t max);
int romi_save(const char* name, const void* data, uint32_t size);

void romi_lock_process(void);
void romi_unlock_process(void);

int romi_dialog_lock(void);
int romi_dialog_unlock(void);

void romi_dialog_input_text(const char* title, const char* text);
int romi_dialog_input_update(void);
void romi_dialog_input_get_text(char* text, uint32_t size);

int romi_check_free_space(uint64_t http_length);

typedef struct romi_http romi_http;

int romi_validate_url(const char* url);
romi_http* romi_http_get(const char* url, const char* content, uint64_t offset, int use_throughput);
int romi_http_response_length(romi_http* http, int64_t* length);
int romi_http_read(romi_http* http, void* write_func, void* write_data, void* xferinfo_func);
void romi_http_close(romi_http* http);

int romi_mkdirs(const char* path);
void romi_rm(const char* file);
int64_t romi_get_size(const char* path);

// creates file (if it exists, truncates size to 0)
void* romi_create(const char* path);
// open existing file in read mode, fails if file does not exist
void* romi_open(const char* path);
// open file for writing, next write will append data to end of it
void* romi_append(const char* path);

void romi_close(void* f);

int romi_read(void* f, void* buffer, uint32_t size);
int romi_write(void* f, const void* buffer, uint32_t size);

// UI stuff
typedef void* romi_texture;

#define romi_load_image_buffer(name, type) \
    ({ extern const uint8_t name##_##type []; \
       extern const uint32_t name##_##type##_size; \
       romi_load_##type##_raw((void*) name##_##type , name##_##type##_size); \
    })

void romi_start_music(void);
void romi_stop_music(void);

romi_texture romi_load_png_raw(const void* data, uint32_t size);
romi_texture romi_load_jpg_raw(const void* data, uint32_t size);
romi_texture romi_load_png_file(const char* filename);
void romi_draw_background(romi_texture texture);
void romi_draw_texture(romi_texture texture, int x, int y);
void romi_draw_texture_z(romi_texture texture, int x, int y, int z, float scale);
void romi_free_texture(romi_texture texture);

void romi_clip_set(int x, int y, int w, int h);
void romi_clip_remove(void);
void romi_draw_rect(int x, int y, int w, int h, uint32_t color);
void romi_draw_rect_z(int x, int y, int z, int w, int h, uint32_t color);
void romi_draw_fill_rect(int x, int y, int w, int h, uint32_t color);
void romi_draw_fill_rect_z(int x, int y, int z, int w, int h, uint32_t color);
void romi_draw_text(int x, int y, uint32_t color, const char* text);
void romi_draw_text_z(int x, int y, int z, uint32_t color, const char* text);
void romi_draw_text_ttf(int x, int y, int z, uint32_t color, const char* text);
void romi_draw_marker_char(int x, int y, int z, uint32_t color, uint8_t marker);
int romi_text_width_ttf(const char* text);
int romi_text_width(const char* text);
int romi_text_height(const char* text);
