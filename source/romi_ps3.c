#include "romi.h"
#include "romi_style.h"
#include "romi_queue.h"

#include <sys/stat.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sysutil/osk.h>

#include <io/pad.h>
#include <lv2/sysfs.h>
#include <lv2/process.h>
#include <net/net.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <ya2d/ya2d.h>
#include <curl/curl.h>

#include "ttf_render.h"

#include <mikmod.h>
#include "mikmod_loader.h"

#ifdef ROMI_FILE_LOGGING
#include <stdarg.h>
void romi_dual_log(const char* format, ...) {
    char buffer[1024];
    va_list args;

    // Format the message
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Log to UDP (for PS3 real hardware)
    dbglogger_log("%s", buffer);

    // Also log to file (for RPCS3)
    FILE* f = fopen("/dev_hdd0/game/ROMI00001/USRDIR/romi_debug.log", "a");
    if (f) {
        fprintf(f, "%s\n", buffer);
        fclose(f);
    }
}
#endif

#define OSKDIALOG_FINISHED          0x503
#define OSKDIALOG_UNLOADED          0x504
#define OSKDIALOG_INPUT_ENTERED     0x505
#define OSKDIALOG_INPUT_CANCELED    0x506

#define ROMI_OSK_INPUT_LENGTH 128

#define SCE_IME_DIALOG_MAX_TITLE_LENGTH	(128)
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH	(512)

#define ANALOG_CENTER       0x78
#define ANALOG_THRESHOLD    0x68
#define ANALOG_MIN          (ANALOG_CENTER - ANALOG_THRESHOLD)
#define ANALOG_MAX          (ANALOG_CENTER + ANALOG_THRESHOLD)

#define ROMI_USER_AGENT "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36"

#define ROMI_CURL_BUFFER_SIZE   (512 * 1024L)    // 512 KB - optimized for throughput
#define ROMI_FILE_BUFFER_SIZE   (256 * 1024)


struct romi_http
{
    int used;
    uint64_t size;
    uint64_t offset;
    CURL *curl;
};

typedef struct 
{
    romi_texture circle;
    romi_texture cross;
    romi_texture triangle;
    romi_texture square;
} t_tex_buttons;

typedef struct
{
    char *memory;
    size_t size;
} curl_memory_t;

extern Config config;
int proxy_failed = 0;

static sys_mutex_t g_dialog_lock;
static uint32_t cpu_temp_c[2];

static int g_ok_button;
static int g_cancel_button;
static uint32_t g_button_frame_count;
static u64 g_time;

static int g_ime_active;
static int osk_action = 0;
static int osk_level = 0;

static sys_mem_container_t container_mem;
static oskCallbackReturnParam OutputReturnedParam;

volatile int osk_event = 0;
volatile int osk_unloaded = 0;

static uint16_t g_ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t g_ime_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t g_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static romi_http g_http[4];
static t_tex_buttons tex_buttons;

static MREADER *mem_reader;
static MODULE *module;


int romi_snprintf(char* buffer, uint32_t size, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, size - 1, msg, args);
    va_end(args);
    buffer[len] = 0;
    return len;
}

void romi_vsnprintf(char* buffer, uint32_t size, const char* msg, va_list args)
{
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, size - 1, msg, args);
    buffer[len] = 0;
}

char* romi_strstr(const char* str, const char* sub)
{
    return strstr(str, sub);
}

int romi_stricontains(const char* str, const char* sub)
{
    return strcasestr(str, sub) != NULL;
}

int romi_stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

void romi_strncpy(char* dst, uint32_t size, const char* src)
{
    strncpy(dst, src, size);
}

char* romi_strrchr(const char* str, char ch)
{
    return strrchr(str, ch);
}

uint32_t romi_strlen(const char *str)
{
    return strlen(str);
}

int64_t romi_strtoll(const char* str)
{
    int64_t res = 0;
    const char* s = str;
    if (*s && *s == '-')
    {
        s++;
    }
    while (*s)
    {
        res = res * 10 + (*s - '0');
        s++;
    }

    return str[0] == '-' ? -res : res;
}

void *romi_malloc(uint32_t size)
{
    return malloc(size);
}

void romi_free(void *ptr)
{
    free(ptr);
}

void romi_memcpy(void* dst, const void* src, uint32_t size)
{
    memcpy(dst, src, size);
}

void romi_memmove(void* dst, const void* src, uint32_t size)
{
    memmove(dst, src, size);
}

int romi_memequ(const void* a, const void* b, uint32_t size)
{
    return memcmp(a, b, size) == 0;
}

static void romi_start_debug_log(void)
{
#ifdef ROMI_ENABLE_LOGGING
    dbglogger_init();
    LOG("ROMi logging initialized");
#endif
}

static void romi_stop_debug_log(void)
{
#ifdef ROMI_ENABLE_LOGGING
    dbglogger_stop();
#endif
}

int romi_ok_button(void)
{
    return g_ok_button;
}

int romi_cancel_button(void)
{
    return g_cancel_button;
}

static void music_update_thread(void)
{
    while (module)
    {
        MikMod_Update();
        usleep(1000);
    }
    romi_thread_exit();
}

void init_music(void)
{
    MikMod_InitThreads();
    
    /* register the driver and module loaders */
    MikMod_RegisterDriver(&drv_psl1ght);    
    MikMod_RegisterLoader(&load_s3m);
    MikMod_RegisterLoader(&load_it);
    MikMod_RegisterLoader(&load_xm);
    MikMod_RegisterLoader(&load_mod);
    
    /* init the library */
    md_mode |= DMODE_SOFT_MUSIC | DMODE_STEREO | DMODE_HQMIXER | DMODE_16BITS;
    
    if (MikMod_Init("")) {
        LOG("Could not initialize sound: %s", MikMod_strerror(MikMod_errno));
        return;
    }
    
    LOG("Init %s", MikMod_InfoDriver());
    LOG("Loader %s", MikMod_InfoLoader());
    
    mem_reader = new_mikmod_mem_reader(lost_painting_bin, lost_painting_bin_size);
    module = Player_LoadGeneric(mem_reader, 64, 0);
    module->wrap = TRUE;

    romi_start_thread("music_thread", &music_update_thread);
}

void romi_start_music(void)
{
    if (module) {
        /* start module */
        LOG("Playing %s", module->songname);
        Player_Start(module);
    } else
        LOG("Could not load module: %s", MikMod_strerror(MikMod_errno));
}

void romi_stop_music(void)
{
    LOG("Stop music");
    Player_Stop();
}

void end_music(void)
{
    Player_Free(module);
    
    delete_mikmod_mem_reader(mem_reader);
    MikMod_Exit();
}

static int sys_game_get_temperature(int sel, u32 *temperature) 
{
    u32 temp;
  
    lv2syscall2(383, (u64) sel, (u64) &temp); 
    *temperature = (temp >> 24);
    return_to_user_prog(int);
}

int romi_dialog_lock(void)
{
    int res = sysMutexLock(g_dialog_lock, 0);
    if (res != 0)
    {
        LOG("dialog lock failed error=0x%08x", res);
    }
    return (res == 0);
}

int romi_dialog_unlock(void)
{
    int res = sysMutexUnlock(g_dialog_lock);
    if (res != 0)
    {
        LOG("dialog unlock failed error=0x%08x", res);
    }
    return (res == 0);
}

static int convert_to_utf16(const char* utf8, uint16_t* utf16, uint32_t available)
{
    int count = 0;
    while (*utf8)
    {
        uint8_t ch = (uint8_t)*utf8++;
        uint32_t code;
        uint32_t extra;

        if (ch < 0x80)
        {
            code = ch;
            extra = 0;
        }
        else if ((ch & 0xe0) == 0xc0)
        {
            code = ch & 31;
            extra = 1;
        }
        else if ((ch & 0xf0) == 0xe0)
        {
            code = ch & 15;
            extra = 2;
        }
        else
        {
            // TODO: this assumes there won't be invalid utf8 codepoints
            code = ch & 7;
            extra = 3;
        }

        for (uint32_t i=0; i<extra; i++)
        {
            uint8_t next = (uint8_t)*utf8++;
            if (next == 0 || (next & 0xc0) != 0x80)
            {
                return count;
            }
            code = (code << 6) | (next & 0x3f);
        }

        if (code < 0xd800 || code >= 0xe000)
        {
            if (available < 1) return count;
            utf16[count++] = (uint16_t)code;
            available--;
        }
        else // surrogate pair
        {
            if (available < 2) return count;
            code -= 0x10000;
            utf16[count++] = 0xd800 | (code >> 10);
            utf16[count++] = 0xdc00 | (code & 0x3ff);
            available -= 2;
        }
    }
    utf16[count]=0;
    return count;
}

static int convert_from_utf16(const uint16_t* utf16, char* utf8, uint32_t size)
{
    int count = 0;
    while (*utf16)
    {
        uint32_t code;
        uint16_t ch = *utf16++;
        if (ch < 0xd800 || ch >= 0xe000)
        {
            code = ch;
        }
        else // surrogate pair
        {
            uint16_t ch2 = *utf16++;
            if (ch < 0xdc00 || ch > 0xe000 || ch2 < 0xd800 || ch2 > 0xdc00)
            {
                return count;
            }
            code = 0x10000 + ((ch & 0x03FF) << 10) + (ch2 & 0x03FF);
        }

        if (code < 0x80)
        {
            if (size < 1) return count;
            utf8[count++] = (char)code;
            size--;
        }
        else if (code < 0x800)
        {
            if (size < 2) return count;
            utf8[count++] = (char)(0xc0 | (code >> 6));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 2;
        }
        else if (code < 0x10000)
        {
            if (size < 3) return count;
            utf8[count++] = (char)(0xe0 | (code >> 12));
            utf8[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 3;
        }
        else
        {
            if (size < 4) return count;
            utf8[count++] = (char)(0xf0 | (code >> 18));
            utf8[count++] = (char)(0x80 | ((code >> 12) & 0x3f));
            utf8[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 4;
        }
    }
    utf8[count]=0;
    return count;
}


static void osk_exit(void)
{
    if(osk_level == 2) {
        oskAbort();
        oskUnloadAsync(&OutputReturnedParam);
        
        osk_event = 0;
        osk_action=-1;
    }

    if(osk_level >= 1) {
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        sysMemContainerDestroy(container_mem);
    }

}

static void osk_event_handle(u64 status, u64 param, void * userdata)
{
    switch((u32) status) 
    {
	    case OSKDIALOG_INPUT_CANCELED:
		    osk_event = OSKDIALOG_INPUT_CANCELED;
		    break;

        case OSKDIALOG_UNLOADED:
		    osk_unloaded = 1;
		    break;

        case OSKDIALOG_INPUT_ENTERED:
	    	osk_event = OSKDIALOG_INPUT_ENTERED;
		    break;

	    case OSKDIALOG_FINISHED:
	    	osk_event = OSKDIALOG_FINISHED;
		    break;

        default:
            break;
    }
}


void romi_dialog_input_text(const char* title, const char* text)
{
    oskParam DialogOskParam;
    oskInputFieldInfo inputFieldInfo;
	int ret = 0;       
    osk_level = 0;
    
	if(sysMemContainerCreate(&container_mem, 8*1024*1024) < 0) {
	    ret = -1;
	    goto error_end;
    }

    osk_level = 1;

    convert_to_utf16(title, g_ime_title, ROMI_COUNTOF(g_ime_title) - 1);
    convert_to_utf16(text, g_ime_text, ROMI_COUNTOF(g_ime_text) - 1);
    
    inputFieldInfo.message =  g_ime_title;
    inputFieldInfo.startText = g_ime_text;
    inputFieldInfo.maxLength = ROMI_OSK_INPUT_LENGTH;
       
    OutputReturnedParam.res = OSK_NO_TEXT;
    OutputReturnedParam.len = ROMI_OSK_INPUT_LENGTH;
    OutputReturnedParam.str = g_ime_input;

    memset(g_ime_input, 0, sizeof(g_ime_input));

    if(oskSetKeyLayoutOption (OSK_10KEY_PANEL | OSK_FULLKEY_PANEL) < 0) {
        ret = -2; 
        goto error_end;
    }

    DialogOskParam.firstViewPanel = OSK_PANEL_TYPE_ALPHABET_FULL_WIDTH;
    DialogOskParam.allowedPanels = (OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL | OSK_PANEL_TYPE_ENGLISH);

    if(oskAddSupportLanguage (DialogOskParam.allowedPanels) < 0) {
        ret = -3; 
        goto error_end;
    }

    if(oskSetLayoutMode( OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER ) < 0) {
        ret = -4; 
        goto error_end;
    }

    oskPoint pos = {0.0, 0.0};

    DialogOskParam.controlPoint = pos;
    DialogOskParam.prohibitFlags = OSK_PROHIBIT_RETURN;
    if(oskSetInitialInputDevice(OSK_DEVICE_PAD) < 0) {
        ret = -5; 
        goto error_end;
    }
    
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, osk_event_handle, NULL);
    
    osk_action = 0;
    osk_unloaded = 0;
    
    if(oskLoadAsync(container_mem, (const void *) &DialogOskParam, (const void *) &inputFieldInfo) < 0) {
        ret= -6; 
        goto error_end;
    }

    osk_level = 2;

    if (ret == 0)
    {
        g_ime_active = 1;
        return;
    }

error_end:
    LOG("Keyboard Init failed, error 0x%08x", ret);

    osk_exit();
    osk_level = 0;
}

int romi_dialog_input_update(void)
{
    if (!g_ime_active)
    {
        return 0;
    }
    
    if (!osk_unloaded)
    {
        switch(osk_event) 
        {
            case OSKDIALOG_INPUT_ENTERED:
                oskGetInputText(&OutputReturnedParam);
                osk_event = 0;
                break;

            case OSKDIALOG_INPUT_CANCELED:
                oskAbort();
                oskUnloadAsync(&OutputReturnedParam);

                osk_event = 0;
                osk_action = -1;
                break;

            case OSKDIALOG_FINISHED:
                if (osk_action != -1) osk_action = 1;
                oskUnloadAsync(&OutputReturnedParam);
                osk_event = 0;
                break;

            default:    
                break;
        }
    }
    else
    {
        g_ime_active = 0;

        if ((OutputReturnedParam.res == OSK_OK) && (osk_action == 1))
        {
            osk_exit();
            return 1;
        } 
         
        osk_exit();
    }

    return 0;
}

void romi_dialog_input_get_text(char* text, uint32_t size)
{
    convert_from_utf16(g_ime_input, text, size - 1);
    LOG("input: %s", text);
}

void load_ttf_fonts()
{
	LOG("loading TTF fonts");

	TTFUnloadFont();

	// Use same fonts as PKGi - LATIN2 works for Portuguese!
	if(TTFLoadFont(0, "/dev_flash/data/font/SCE-PS3-SR-R-LATIN2.TTF", NULL, 0) == 0)
		LOG("Font 0 (SR-LATIN2) loaded successfully");
	else
		LOG("ERROR: Failed to load Font 0 (SR-LATIN2)");

	if(TTFLoadFont(1, "/dev_flash/data/font/SCE-PS3-DH-R-CGB.TTF", NULL, 0) == 0)
		LOG("Font 1 (DH-CGB) loaded successfully");
	else
		LOG("ERROR: Failed to load Font 1 (DH-CGB)");

	if(TTFLoadFont(2, "/dev_flash/data/font/SCE-PS3-SR-R-JPN.TTF", NULL, 0) == 0)
		LOG("Font 2 (JPN) loaded successfully");
	else
		LOG("ERROR: Failed to load Font 2 (JPN)");

	if(TTFLoadFont(3, "/dev_flash/data/font/SCE-PS3-YG-R-KOR.TTF", NULL, 0) == 0)
		LOG("Font 3 (KOR) loaded successfully");
	else
		LOG("ERROR: Failed to load Font 3 (KOR)");
	
	ya2d_texturePointer = (u32*) init_ttf_table((u16*) ya2d_texturePointer);
}

static void sys_callback(uint64_t status, uint64_t param, void* userdata)
{
    switch (status) {
        case SYSUTIL_EXIT_GAME:
            romi_end();
            sysProcessExit(1);
            break;
        
        case SYSUTIL_MENU_OPEN:
        case SYSUTIL_MENU_CLOSE:
            break;

        default:
            break;
    }
}

void romi_start(void)
{
    romi_start_debug_log();
    
    netInitialize();

    LOG("initializing Network");
    sysModuleLoad(SYSMODULE_NET);
    curl_global_init(CURL_GLOBAL_ALL);

    sys_mutex_attr_t mutex_attr;
    mutex_attr.attr_protocol = SYS_MUTEX_PROTOCOL_FIFO;
    mutex_attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
    mutex_attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
    mutex_attr.attr_adaptive = SYS_MUTEX_ATTR_ADAPTIVE;
    strcpy(mutex_attr.name, "dialog");

    int ret = sysMutexCreate(&g_dialog_lock, &mutex_attr);
    if (ret != 0) {
        LOG("mutex create error (%x)", ret);
    }

    romi_queue_init();

    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_ENTER_BUTTON_ASSIGN, &ret);
    if (ret == 0)
    {
        g_ok_button = ROMI_BUTTON_O;
        g_cancel_button = ROMI_BUTTON_X;
    }
    else
    {
        g_ok_button = ROMI_BUTTON_X;
        g_cancel_button = ROMI_BUTTON_O;
    }
    
	ya2d_init();

	ya2d_paddata[0].ANA_L_H = ANALOG_CENTER;
	ya2d_paddata[0].ANA_L_V = ANALOG_CENTER;

    tex_buttons.circle   = romi_load_image_buffer(CIRCLE, png);
    tex_buttons.cross    = romi_load_image_buffer(CROSS, png);
    tex_buttons.triangle = romi_load_image_buffer(TRIANGLE, png);
    tex_buttons.square   = romi_load_image_buffer(SQUARE, png);

    SetFontSize(ROMI_FONT_WIDTH, ROMI_FONT_HEIGHT);
    SetFontZ(ROMI_FONT_Z);

    load_ttf_fonts();

    romi_mkdirs(ROMI_TMP_FOLDER);

    init_music();

    // register exit callback
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_callback, NULL);

    g_time = romi_time_msec();
}

int romi_update(romi_input* input)
{
	ya2d_controlsRead();
    
    uint32_t previous = input->down;
    memcpy(&input->down, &ya2d_paddata[0].button[2], sizeof(uint32_t));

    if (ya2d_paddata[0].ANA_L_V < ANALOG_MIN)
        input->down |= ROMI_BUTTON_UP;
        
    if (ya2d_paddata[0].ANA_L_V > ANALOG_MAX)
        input->down |= ROMI_BUTTON_DOWN;
        
    if (ya2d_paddata[0].ANA_L_H < ANALOG_MIN)
        input->down |= ROMI_BUTTON_LEFT;
        
    if (ya2d_paddata[0].ANA_L_H > ANALOG_MAX)
        input->down |= ROMI_BUTTON_RIGHT;

    input->pressed = input->down & ~previous;
    input->active = input->pressed;

    if (input->down == previous)
    {
        if (g_button_frame_count >= 10)
        {
            input->active = input->down;
        }
        g_button_frame_count++;
    }
    else
    {
        g_button_frame_count = 0;
    }

#ifdef ROMI_ENABLE_LOGGING
    if ((input->active & ROMI_BUTTON_RIGHT) && (input->active & ROMI_BUTTON_LEFT)) {
        LOG("screenshot");
        dbglogger_screenshot_tmp(0);
    }
#endif

	ya2d_screenClear();
	ya2d_screenBeginDrawing();
	reset_ttf_frame();

    uint64_t time = romi_time_msec();
    input->delta = time - g_time;
    g_time = time;

    return 1;
}

void romi_swap(void)
{
	ya2d_screenFlip();
}

void romi_end(void)
{
    if (module) end_music();

    romi_queue_shutdown();

    curl_global_cleanup();
    romi_stop_debug_log();

    romi_free_texture(tex_buttons.circle);
    romi_free_texture(tex_buttons.cross);
    romi_free_texture(tex_buttons.triangle);
    romi_free_texture(tex_buttons.square);

	ya2d_deinit();

    sysMutexDestroy(g_dialog_lock);

#ifdef ROMI_ENABLE_LOGGING
    sysProcessExitSpawn2("/dev_hdd0/game/PSL145310/RELOAD.SELF", NULL, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);
#endif

    sysProcessExit(0);
}

int romi_get_temperature(uint8_t cpu)
{
    static uint32_t t = 0;

    if (t++ % 0x100 == 0)
    {
        sys_game_get_temperature(0, &cpu_temp_c[0]);
        sys_game_get_temperature(1, &cpu_temp_c[1]);
    }

    return cpu_temp_c[cpu];
}

int romi_temperature_is_high(void)
{
    return ((cpu_temp_c[0] >= 70 || cpu_temp_c[1] >= 70));
}

uint64_t romi_get_free_space(void)
{
    u32 blockSize;
    static uint32_t t = 0;
    static uint64_t freeSize = 0;

    if (t++ % 0x200 == 0)
    {
        sysFsGetFreeSize("/dev_hdd0/", &blockSize, &freeSize);
        freeSize *= blockSize;
    }

    return (freeSize);
}

const char* romi_get_config_folder(void)
{
    return ROMI_APP_FOLDER;
}

const char* romi_get_temp_folder(void)
{
    return ROMI_TMP_FOLDER;
}

const char* romi_get_app_folder(void)
{
    return ROMI_APP_FOLDER;
}

int romi_is_incomplete(const char* titleid)
{
    char path[256];
    romi_snprintf(path, sizeof(path), "%s/%s.resume", romi_get_temp_folder(), titleid);

    struct stat st;
    int res = stat(path, &st);
    return (res == 0);
}

int romi_dir_exists(const char* path)
{
    LOG("checking if folder %s exists", path);

    struct stat sb;
    if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        return 1;
    }
    return 0;
}

int romi_is_installed(const char* content)
{    
    char path[128];
    snprintf(path, sizeof(path), "/dev_hdd0/game/%.9s", content + 7);

    return (romi_dir_exists(path));
}

uint32_t romi_time_msec()
{
    return ya2d_millis();
}

void romi_thread_exit()
{
	sysThreadExit(0);
}

void romi_start_thread(const char* name, romi_thread_entry* start)
{
	s32 ret;
	sys_ppu_thread_t id;

	ret = sysThreadCreate(&id, (void (*)(void *))start, NULL, 1500, 1024*1024, THREAD_JOINABLE, (char*)name);
	LOG("sysThreadCreate: %s (0x%08x)",name, id);

    if (ret != 0)
    {
        LOG("failed to start %s thread", name);
    }
}

void romi_start_thread_arg(const char* name, romi_thread_entry_arg* start, void* arg)
{
	s32 ret;
	sys_ppu_thread_t id;

	ret = sysThreadCreate(&id, (void (*)(void *))start, arg, 1500, 1024*1024, THREAD_JOINABLE, (char*)name);
	LOG("sysThreadCreate: %s (0x%08x)",name, id);

    if (ret != 0)
    {
        LOG("failed to start %s thread", name);
    }
}

void romi_sleep(uint32_t msec)
{
    usleep(msec * 1000);
}

int romi_load(const char* name, void* data, uint32_t max)
{
    FILE* fd = fopen(name, "rb");
    if (!fd)
    {
        return -1;
    }
    
    char* data8 = data;

    int total = 0;
    
    while (max != 0)
    {
        int read = fread(data8 + total, 1, max, fd);
        if (read < 0)
        {
            total = -1;
            break;
        }
        else if (read == 0)
        {
            break;
        }
        total += read;
        max -= read;
    }

    fclose(fd);
    return total;
}

int romi_save(const char* name, const void* data, uint32_t size)
{
    FILE* fd = fopen(name, "wb");
    if (!fd)
    {
        return 0;
    }

    int ret = 1;
    const char* data8 = data;
    while (size != 0)
    {
        int written = fwrite(data8, 1, size, fd);
        if (written <= 0)
        {
            ret = 0;
            break;
        }
        data8 += written;
        size -= written;
    }

    fclose(fd);
    return ret;
}

void romi_lock_process(void)
{
/*
    if (__atomic_fetch_add(&g_power_lock, 1, __ATOMIC_SEQ_CST) == 0)
    {
        LOG("locking shell functionality");
        if (sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) < 0)
        {
            LOG("sceShellUtilLock failed");
        }
    }
    */
}

void romi_unlock_process(void)
{
/*
    if (__atomic_sub_fetch(&g_power_lock, 1, __ATOMIC_SEQ_CST) == 0)
    {
        LOG("unlocking shell functionality");
        if (sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) < 0)
        {
            LOG("sceShellUtilUnlock failed");
        }
    }
    */
}

romi_texture romi_load_jpg_raw(const void* data, uint32_t size)
{
    ya2d_Texture *tex = ya2d_loadJPGfromBuffer(data, size);

    if (!tex)
    {
        LOG("failed to load texture");
    }
    return tex;
}

romi_texture romi_load_png_raw(const void* data, uint32_t size)
{
    ya2d_Texture *tex = ya2d_loadPNGfromBuffer(data, size);

    if (!tex)
    {
        LOG("failed to load texture");
    }
    return tex;
}

romi_texture romi_load_png_file(const char* filename)
{
    ya2d_Texture *tex = ya2d_loadPNGfromFile(filename);

    if (!tex)
    {
        LOG("failed to load texture file %s", filename);
    }
    return tex;
}

void romi_draw_texture(romi_texture texture, int x, int y)
{
    ya2d_drawTexture((ya2d_Texture*) texture, x, y);
}

void romi_draw_background(romi_texture texture)
{
    ya2d_drawTextureEx((ya2d_Texture*) texture, 0, 0, YA2D_DEFAULT_Z, VITA_WIDTH, VITA_HEIGHT);
}

void romi_draw_texture_z(romi_texture texture, int x, int y, int z, float scale)
{
    ya2d_drawTextureZ((ya2d_Texture*) texture, x, y, z, scale);
}

void romi_free_texture(romi_texture texture)
{
    ya2d_freeTexture((ya2d_Texture*) texture);
}


void romi_clip_set(int x, int y, int w, int h)
{
    set_ttf_window(x, y, w, h*2, 0);
}

void romi_clip_remove(void)
{
    set_ttf_window(0, 0, VITA_WIDTH, VITA_HEIGHT, 0);
}

void romi_draw_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    ya2d_drawFillRect(x, y, w, h, RGBA_COLOR(color, 255));
}

void romi_draw_fill_rect_z(int x, int y, int z, int w, int h, uint32_t color)
{
    ya2d_drawFillRectZ(x, y, z, w, h, RGBA_COLOR(color, 255));
}

void romi_draw_rect_z(int x, int y, int z, int w, int h, uint32_t color)
{
	ya2d_drawRectZ(x, y, z, w, h, RGBA_COLOR(color, 255));
}

void romi_draw_rect(int x, int y, int w, int h, uint32_t color)
{
	ya2d_drawRect(x, y, w, h, RGBA_COLOR(color, 255));
}

void romi_draw_text_z(int x, int y, int z, uint32_t color, const char* text)
{
    int i=x, j=y;
    SetFontColor(RGBA_COLOR(color, 255), 0);
    const uint8_t* utext = (const uint8_t*)text;

    while (*utext) {
        // Check for newline
        if (*utext == '\n') {
            i = x;
            j += ROMI_FONT_HEIGHT;
            utext++;
            continue;
        }

        // Check for button markers
        if (*utext == '\xfa') {
            romi_draw_texture_z(tex_buttons.circle, i, j, z, 0.5f);
            i += ROMI_FONT_WIDTH;
            utext++;
            continue;
        }
        if (*utext == '\xfb') {
            romi_draw_texture_z(tex_buttons.cross, i, j, z, 0.5f);
            i += ROMI_FONT_WIDTH;
            utext++;
            continue;
        }
        if (*utext == '\xfc') {
            romi_draw_texture_z(tex_buttons.triangle, i, j, z, 0.5f);
            i += ROMI_FONT_WIDTH;
            utext++;
            continue;
        }
        if (*utext == '\xfd') {
            romi_draw_texture_z(tex_buttons.square, i, j, z, 0.5f);
            i += ROMI_FONT_WIDTH;
            utext++;
            continue;
        }

        // Check for UTF-8 multi-byte sequence
        if (*utext & 0x80) {
            uint32_t codepoint = 0;
            int bytes = 0;

            if ((*utext & 0xE0) == 0xC0) {
                // 2-byte sequence
                codepoint = (*utext++ & 0x1F);
                bytes = 1;
            } else if ((*utext & 0xF0) == 0xE0) {
                // 3-byte sequence
                codepoint = (*utext++ & 0x0F);
                bytes = 2;
            } else if ((*utext & 0xF8) == 0xF0) {
                // 4-byte sequence
                codepoint = (*utext++ & 0x07);
                bytes = 3;
            } else {
                // Invalid UTF-8, skip
                utext++;
                continue;
            }

            // Read continuation bytes
            for (int b = 0; b < bytes; b++) {
                if (!*utext || (*utext & 0xC0) != 0x80) break;
                codepoint = (codepoint << 6) | (*utext++ & 0x3F);
            }

            // Render UTF-8 character using TTF (create single-char string for display_ttf_string)
            char utf8_char[5];
            int len = 0;
            const uint8_t* backtrack = utext - (bytes + 1);
            while (len <= bytes) {
                utf8_char[len] = *backtrack++;
                len++;
            }
            utf8_char[len] = '\0';

            Z_ttf = z;
            display_ttf_string(i, j, utf8_char, RGBA_COLOR(color, 255), 0, ROMI_FONT_WIDTH+6, ROMI_FONT_HEIGHT+2);
            i += ROMI_FONT_WIDTH;
            continue;
        }

        // Regular ASCII character
        DrawChar(i, j, z, *utext);
        i += ROMI_FONT_WIDTH;
        utext++;
    }
}

void romi_draw_text_ttf(int x, int y, int z, uint32_t color, const char* text)
{
    Z_ttf = z;
    display_ttf_string(x+ROMI_FONT_SHADOW, y+ROMI_FONT_SHADOW, text, RGBA_COLOR(ROMI_COLOR_TEXT_SHADOW, 128), 0, ROMI_FONT_WIDTH+6, ROMI_FONT_HEIGHT+2);
    display_ttf_string(x, y, text, RGBA_COLOR(color, 255), 0, ROMI_FONT_WIDTH+6, ROMI_FONT_HEIGHT+2);
}

int romi_text_width_ttf(const char* text)
{
    return (display_ttf_string(0, 0, text, 0, 0, ROMI_FONT_WIDTH+6, ROMI_FONT_HEIGHT+2));
}

void romi_draw_marker_char(int x, int y, int z, uint32_t color, uint8_t marker)
{
    // Handle button markers as textures (original behavior)
    switch(marker) {
        case 0xFA: // ROMI_UTF8_O (circle)
            romi_draw_texture_z(tex_buttons.circle, x, y, z, 0.5f);
            return;
        case 0xFB: // ROMI_UTF8_X (cross)
            romi_draw_texture_z(tex_buttons.cross, x, y, z, 0.5f);
            return;
        case 0xFC: // ROMI_UTF8_T (triangle)
            romi_draw_texture_z(tex_buttons.triangle, x, y, z, 0.5f);
            return;
        case 0xFD: // ROMI_UTF8_S (square)
            romi_draw_texture_z(tex_buttons.square, x, y, z, 0.5f);
            return;
    }
    // For other markers (checkboxes, sort icons, clear), use bitmap font
    SetFontColor(RGBA_COLOR(color, 255), 0);
    DrawChar(x, y, z, marker);
}


void romi_draw_text(int x, int y, uint32_t color, const char* text)
{
    SetFontColor(RGBA_COLOR(ROMI_COLOR_TEXT_SHADOW, 128), 0);
    DrawString((float)x+ROMI_FONT_SHADOW, (float)y+ROMI_FONT_SHADOW, (char *)text);

    SetFontColor(RGBA_COLOR(color, 200), 0);
    DrawString((float)x, (float)y, (char *)text);
}


int romi_text_width(const char* text)
{
    return (strlen(text) * ROMI_FONT_WIDTH) + ROMI_FONT_SHADOW;
}

int romi_text_height(const char* text)
{
    return ROMI_FONT_HEIGHT + ROMI_FONT_SHADOW+1;
}

char* romi_truncate_text(char* dest, size_t dest_size, const char* src, int max_width)
{
    if (!src || !dest || dest_size == 0)
    {
        if (dest && dest_size > 0)
            dest[0] = '\0';
        return dest;
    }

    int ellipsis_width = ROMI_FONT_WIDTH * 3;

    if (romi_text_width(src) <= max_width)
    {
        romi_strncpy(dest, dest_size, src);
        return dest;
    }

    if (max_width < ellipsis_width)
    {
        dest[0] = '\0';
        return dest;
    }

    int available_width = max_width - ellipsis_width;
    size_t len = 0;
    const uint8_t* utext = (const uint8_t*)src;

    while (*utext && len + 4 < dest_size)
    {
        const uint8_t* next = utext;

        if (*next & 0x80)
        {
            if ((*next & 0xE0) == 0xC0)
                next += 2;
            else if ((*next & 0xF0) == 0xE0)
                next += 3;
            else if ((*next & 0xF8) == 0xF0)
                next += 4;
            else
                next++;
        }
        else
        {
            next++;
        }

        size_t char_bytes = next - utext;
        char temp[dest_size];
        romi_memcpy(temp, src, len + char_bytes);
        temp[len + char_bytes] = '\0';

        if (romi_text_width(temp) > available_width)
            break;

        romi_memcpy(dest + len, utext, char_bytes);
        len += char_bytes;
        utext = next;
    }

    dest[len] = '\0';
    if (len + 3 < dest_size)
    {
        dest[len] = '.';
        dest[len + 1] = '.';
        dest[len + 2] = '.';
        dest[len + 3] = '\0';
    }

    return dest;
}

int romi_validate_url(const char* url)
{
    if (url[0] == 0)
    {
        return 0;
    }
    if ((romi_strstr(url, "http://") == url) || (romi_strstr(url, "https://") == url) ||
        (romi_strstr(url, "ftp://") == url)  || (romi_strstr(url, "ftps://") == url))
    {
        return 1;
    }
    return 0;
}

// TCP socket tuning callback for optimized throughput
__attribute__((unused)) static int romi_tcp_tune_callback(void *clientp, curl_socket_t sockfd, curlsocktype purpose)
{
    ROMI_UNUSED(clientp);

    if (purpose == CURLSOCKTYPE_IPCXN)
    {
        int recv_buf = 512 * 1024;  // 512 KB receive buffer
        int send_buf = 256 * 1024;  // 256 KB send buffer

        // Increase TCP receive window for better throughput
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf)) != 0)
        {
            LOG("TCP tuning: Failed to set SO_RCVBUF (errno=%d)", errno);
        }
        else
        {
            LOG("TCP tuning: SO_RCVBUF set to %d KB", recv_buf / 1024);
        }

        // Increase TCP send window
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf)) != 0)
        {
            LOG("TCP tuning: Failed to set SO_SNDBUF (errno=%d)", errno);
        }
        else
        {
            LOG("TCP tuning: SO_SNDBUF set to %d KB", send_buf / 1024);
        }
    }

    return CURL_SOCKOPT_OK;
}

void romi_curl_init(CURL *curl)
{
    static struct curl_slist *headers = NULL;

    // Mimic wget headers exactly - Myrient whitelists wget/curl
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Wget/1.24");

    // Match wget's minimal headers
    if (!headers)
    {
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Accept-Encoding: identity");
        LOG("CURL: Using wget-style headers (Wget/1.24, Accept-Encoding: identity)");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // don't verify the certificate's name against host
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // don't verify the peer's SSL certificate
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    // Set SSL VERSION to TLS 1.2
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    // Set timeout for the connection to build
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // maximum number of redirects allowed
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
    // Fail the request if the HTTP code returned is equal to or larger than 400
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    // request using SSL for the FTP transfer if available
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

    // Configure HTTP/HTTPS proxy if provided in config
    if (config.proxy_url[0] && !proxy_failed)
    {
        curl_easy_setopt(curl, CURLOPT_PROXY, config.proxy_url);
        LOG("CURL: Using proxy %s", config.proxy_url);

        // Set proxy authentication if provided
        if (config.proxy_user[0])
        {
            char proxy_auth[256];
            if (config.proxy_pass[0])
                romi_snprintf(proxy_auth, sizeof(proxy_auth), "%s:%s",
                             config.proxy_user, config.proxy_pass);
            else
                romi_strncpy(proxy_auth, sizeof(proxy_auth), config.proxy_user);

            curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxy_auth);
            LOG("CURL: Proxy authentication configured");
        }

        // Set proxy type to auto-detect (supports HTTP, HTTPS, SOCKS4, SOCKS5)
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

        // Disable SSL verification when using TLS-terminating proxy (mitmproxy, squid ssl_bump)
        // This allows the proxy to intercept and re-encrypt with modern ciphers
        // Security: Proxy validates upstream, acceptable for ROM downloads
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        LOG("CURL: SSL verification disabled for TLS-terminating proxy");
    }
    else if (proxy_failed)
    {
        LOG("CURL: Proxy previously failed, using direct connection");
    }

    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, ROMI_CURL_BUFFER_SIZE);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);

#ifdef DEBUGLOG
    // Enable verbose CURL logging in debug builds to diagnose issues
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif
}

// Initialize CURL with throughput optimization for large downloads
static CURL* romi_curl_init_throughput(int enable_throughput_mode)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        LOG("curl_easy_init failed");
        return NULL;
    }

    // Apply standard configuration
    romi_curl_init(curl);

    if (enable_throughput_mode)
    {
        // Disable TCP_NODELAY for bulk transfers (enable Nagle's algorithm)
        curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 0L);
        LOG("CURL: Throughput mode ENABLED (Nagle's algorithm ON, CURL buffer=%d KB)", ROMI_CURL_BUFFER_SIZE / 1024);

        // Add socket tuning callback (requires CURLOPT_SOCKOPTFUNCTION support)
        #ifdef CURLOPT_SOCKOPTFUNCTION
        curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, romi_tcp_tune_callback);
        LOG("CURL: Socket tuning callback registered");
        #else
        LOG("CURL: Warning - CURLOPT_SOCKOPTFUNCTION not available, socket tuning disabled");
        #endif
    }
    else
    {
        // Keep TCP_NODELAY enabled for low-latency (metadata/small requests)
        LOG("CURL: Low-latency mode (TCP_NODELAY=1, CURL buffer=%d KB)", ROMI_CURL_BUFFER_SIZE / 1024);
    }

    return curl;
}

romi_http* romi_http_get(const char* url, const char* content, uint64_t offset, int use_throughput)
{
    LOG("http get");

    if (!romi_validate_url(url))
    {
        LOG("unsupported URL (%s)", url);
        return NULL;
    }

    romi_http* http = NULL;
    for (size_t i = 0; i < 4; i++)
    {
        if (g_http[i].used == 0)
        {
            http = &g_http[i];
            break;
        }
    }

    if (!http)
    {
        LOG("too many simultaneous http requests");
        return NULL;
    }

    http->curl = romi_curl_init_throughput(use_throughput);
    if (!http->curl)
    {
        LOG("curl init error");
        return NULL;
    }
    curl_easy_setopt(http->curl, CURLOPT_URL, url);

    // NOTE: No Referer header - plain curl doesn't send it, and Myrient rate-limits browser-like requests

    LOG("starting http GET request for %s", url);

    if (offset != 0)
    {
        LOG("setting http offset %ld", offset);
        /* resuming upload at this position */
        curl_easy_setopt(http->curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t) offset);
    }

    http->used = 1;
    return(http);
}

int romi_http_response_length(romi_http* http, int64_t* length)
{
    CURLcode res;

    // do the download request without getting the body
    curl_easy_setopt(http->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 1L);

    // Perform the request
    res = curl_easy_perform(http->curl);

    if(res != CURLE_OK)
    {
        // Check if error is proxy-related
        if ((res == CURLE_COULDNT_RESOLVE_PROXY ||
             res == CURLE_COULDNT_CONNECT) &&
            config.proxy_url[0] && !proxy_failed)
        {
            LOG("CURL: Proxy connection failed (%s), falling back to direct", curl_easy_strerror(res));
            proxy_failed = 1;

            // Get the URL before cleanup
            char *url = NULL;
            curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &url);

            // Cleanup and retry without proxy
            curl_easy_cleanup(http->curl);
            http->curl = romi_curl_init_throughput(1);
            if (!http->curl)
            {
                LOG("curl init error on retry");
                return 0;
            }

            // Re-apply request configuration
            if (url)
                curl_easy_setopt(http->curl, CURLOPT_URL, url);
            curl_easy_setopt(http->curl, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 1L);

            // Retry request
            res = curl_easy_perform(http->curl);
        }

        if(res != CURLE_OK)
        {
            LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
            return 0;
        }
    }

    long status = 0;
    curl_easy_getinfo(http->curl, CURLINFO_RESPONSE_CODE, &status);
    LOG("http status code = %d", status);

    curl_easy_getinfo(http->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, length);
    LOG("http response length = %llu", *length);
    http->size = *length;

    return 1;
}

int romi_http_read(romi_http* http, void* write_func, void* write_data, void* xferinfo_func)
{
    CURLcode res;

    curl_easy_setopt(http->curl, CURLOPT_NOBODY, 0L);
    // The function that will be used to write the data
    curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, write_func);
    // The data file descriptor which will be written to
    curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, write_data);

    if (xferinfo_func)
    {
        /* pass the struct pointer into the xferinfo function */
        curl_easy_setopt(http->curl, CURLOPT_XFERINFOFUNCTION, xferinfo_func);
        curl_easy_setopt(http->curl, CURLOPT_XFERINFODATA, NULL);
        curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 0L);
    }

    // Perform the request
    res = curl_easy_perform(http->curl);

    if(res != CURLE_OK)
    {
        // Check if error is proxy-related
        if ((res == CURLE_COULDNT_RESOLVE_PROXY ||
             res == CURLE_COULDNT_CONNECT) &&
            config.proxy_url[0] && !proxy_failed)
        {
            LOG("CURL: Proxy connection failed (%s), falling back to direct", curl_easy_strerror(res));
            proxy_failed = 1;

            // Get the URL before cleanup
            char *url = NULL;
            curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &url);

            // Cleanup and retry without proxy
            curl_easy_cleanup(http->curl);
            http->curl = romi_curl_init_throughput(1);
            if (!http->curl)
            {
                LOG("curl init error on retry");
                return 0;
            }

            // Re-apply request configuration
            if (url)
                curl_easy_setopt(http->curl, CURLOPT_URL, url);
            curl_easy_setopt(http->curl, CURLOPT_NOBODY, 0L);
            curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, write_func);
            curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, write_data);
            if (xferinfo_func)
            {
                curl_easy_setopt(http->curl, CURLOPT_XFERINFOFUNCTION, xferinfo_func);
                curl_easy_setopt(http->curl, CURLOPT_XFERINFODATA, NULL);
                curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 0L);
            }

            // Retry request
            res = curl_easy_perform(http->curl);
        }

        if(res != CURLE_OK)
        {
            LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
            return 0;
        }
    }

    // Diagnostic logging to identify rate limiting vs PS3 hardware issues
    char *effective_url = NULL;
    long redirect_count = 0;
    double total_time = 0;
    double download_speed = 0;
    char *primary_ip = NULL;

    curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &effective_url);
    curl_easy_getinfo(http->curl, CURLINFO_REDIRECT_COUNT, &redirect_count);
    curl_easy_getinfo(http->curl, CURLINFO_TOTAL_TIME, &total_time);
    curl_easy_getinfo(http->curl, CURLINFO_SPEED_DOWNLOAD, &download_speed);
    curl_easy_getinfo(http->curl, CURLINFO_PRIMARY_IP, &primary_ip);

    LOG("=== Download Diagnostics ===");
    LOG("Effective URL: %s", effective_url ? effective_url : "unknown");
    LOG("Redirects: %ld", redirect_count);
    LOG("Server IP: %s", primary_ip ? primary_ip : "unknown");
    LOG("Total time: %.2f seconds", total_time);
    LOG("CURL reported speed: %.2f KB/s (%.2f MB/s)",
        download_speed / 1024.0, download_speed / (1024.0 * 1024.0));
    LOG("Expected for file size: %.2f KB/s",
        total_time > 0 ? (http->size / 1024.0) / total_time : 0);
    LOG("===========================");

    return 1;
}

void romi_http_close(romi_http* http)
{
    LOG("http close");
    curl_easy_cleanup(http->curl);

    http->used = 0;
}

int romi_mkdirs(const char* dir)
{
    char path[256];
    romi_snprintf(path, sizeof(path), "%s", dir);
    LOG("romi_mkdirs for %s", path);
    char* ptr = path;
    ptr++;
    while (*ptr)
    {
        while (*ptr && *ptr != '/')
        {
            ptr++;
        }
        char last = *ptr;
        *ptr = 0;

        if (!romi_dir_exists(path))
        {
            LOG("mkdir %s", path);
            int err = mkdir(path, 0777);
            if (err < 0)
            {
                LOG("mkdir %s err=0x%08x", path, (uint32_t)err);
                return 0;
            }
        }
        
        *ptr++ = last;
        if (last == 0)
        {
            break;
        }
    }

    return 1;
}

void romi_rm(const char* file)
{
    struct stat sb;
    if (stat(file, &sb) == 0) {
        LOG("removing file %s", file);

        int err = unlink(file);
        if (err < 0)
        {
            LOG("error removing %s file, err=0x%08x", err);
        }
    }
}

int64_t romi_get_size(const char* path)
{
    struct stat st;
    int err = stat(path, &st);
    if (err < 0)
    {
        LOG("cannot get size of %s, err=0x%08x", path, err);
        return -1;
    }
    return st.st_size;
}

void* romi_create(const char* path)
{
    LOG("fopen create on %s", path);
    FILE* fd = fopen(path, "wb");
    if (!fd)
    {
        LOG("cannot create %s, err=0x%08x", path, fd);
        return NULL;
    }
    setvbuf(fd, NULL, _IOFBF, ROMI_FILE_BUFFER_SIZE);
    LOG("fopen returned fd=%d", fd);

    return (void*)fd;
}

void* romi_open(const char* path)
{
    LOG("fopen open rb on %s", path);
    FILE* fd = fopen(path, "rb");
    if (!fd)
    {
        LOG("cannot open %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("fopen returned fd=%d", fd);

    return (void*)fd;
}

void* romi_append(const char* path)
{
    LOG("fopen append on %s", path);
    FILE* fd = fopen(path, "ab");
    if (!fd)
    {
        LOG("cannot append %s, err=0x%08x", path, fd);
        return NULL;
    }
    setvbuf(fd, NULL, _IOFBF, ROMI_FILE_BUFFER_SIZE);
    LOG("fopen returned fd=%d", fd);

    return (void*)fd;
}

int romi_read(void* f, void* buffer, uint32_t size)
{
    LOG("asking to read %u bytes", size);
    size_t read = fread(buffer, 1, size, (FILE*)f);
    if (read < 0)
    {
        LOG("fread error 0x%08x", read);
    }
    else
    {
        LOG("read %d bytes", read);
    }
    return read;
}

int romi_write(void* f, const void* buffer, uint32_t size)
{
//    LOG("asking to write %u bytes", size);
    size_t write = fwrite(buffer, size, 1, (FILE*)f);
    if (write < 0)
    {
        LOG("fwrite error 0x%08x", write);
    }
    else
    {
//        LOG("wrote %d bytes", write);
    }
    return (write == 1);
}

void romi_close(void* f)
{
    FILE *fd = (FILE*)f;
    LOG("closing file %d", fd);
    int err = fclose(fd);
    if (err < 0)
    {
        LOG("close error 0x%08x", err);
    }
}

static size_t curl_write_memory(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    curl_memory_t *mem = (curl_memory_t *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr)
    {
        /* out of memory! */
        LOG("not enough memory (realloc)");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

char * romi_http_download_buffer(const char* url, uint32_t* buf_size)
{
    CURL *curl;
    CURLcode res;
    curl_memory_t chunk;

    curl = curl_easy_init();
    if(!curl)
    {
        LOG("cURL init error");
        return NULL;
    }
    
    chunk.memory = malloc(1);   /* will be grown as needed by the realloc above */
    chunk.size = 0;             /* no data at this point */

    romi_curl_init(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    // The function that will be used to write the data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_memory);
    // The data file descriptor which will be written to
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    // Perform the request
    res = curl_easy_perform(curl);

    if(res != CURLE_OK)
    {
        LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return NULL;
    }

    LOG("%lu bytes retrieved", (unsigned long)chunk.size);
    // clean-up
    curl_easy_cleanup(curl);

    *buf_size = chunk.size;
    return (chunk.memory);
}

const char * romi_get_user_language()
{
    int language;

    if(sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_LANG, &language) < 0)
        return "en";

    switch (language)
    {
    case SYSUTIL_LANG_JAPANESE:             //  0   Japanese
        return "ja";

    case SYSUTIL_LANG_ENGLISH_US:           //  1   English (United States)
    case SYSUTIL_LANG_ENGLISH_GB:           // 18   English (United Kingdom)
        return "en";

    case SYSUTIL_LANG_FRENCH:               //  2   French
        return "fr";

    case SYSUTIL_LANG_SPANISH:              //  3   Spanish
        return "es";

    case SYSUTIL_LANG_GERMAN:               //  4   German
        return "de";

    case SYSUTIL_LANG_ITALIAN:              //  5   Italian
        return "it";

    case SYSUTIL_LANG_DUTCH:                //  6   Dutch
        return "nl";

    case SYSUTIL_LANG_RUSSIAN:              //  8   Russian
        return "ru";

    case SYSUTIL_LANG_KOREAN:               //  9   Korean
        return "ko";

    case SYSUTIL_LANG_CHINESE_T:            // 10   Chinese (traditional)
    case SYSUTIL_LANG_CHINESE_S:            // 11   Chinese (simplified)
        return "ch";

    case SYSUTIL_LANG_FINNISH:              // 12   Finnish
        return "fi";

    case SYSUTIL_LANG_SWEDISH:              // 13   Swedish
        return "sv";

    case SYSUTIL_LANG_DANISH:               // 14   Danish
        return "da";

    case SYSUTIL_LANG_NORWEGIAN:            // 15   Norwegian
        return "no";

    case SYSUTIL_LANG_POLISH:               // 16   Polish
        return "pl";

    case SYSUTIL_LANG_PORTUGUESE_PT:        //  7   Portuguese (Portugal)
    case SYSUTIL_LANG_PORTUGUESE_BR:        // 17   Portuguese (Brazil)
        return "pt";

    case SYSUTIL_LANG_TURKISH:              // 19   Turkish
        return "tr";

    default:
        break;
    }

    return "en";
}
