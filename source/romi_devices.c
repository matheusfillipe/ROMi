#include "romi_devices.h"
#include "romi.h"

#include <stdio.h>
#include <string.h>
#include <ppu-lv2.h>
#include <sys/file.h>
#include <sys/stat.h>

#define ROMI_MAX_DEVICES 17

static RomiDevice g_devices[ROMI_MAX_DEVICES];
static int g_device_count = 0;
static int g_selected_device = 0;
static int g_initialized = 0;

static int check_usb_device(int index);
static void add_hdd_device(void);
static void add_usb_device(int index);
static void get_device_space(const char* path, uint64_t* free, uint64_t* total);
static void romi_devices_create_folders(void);

#ifdef HAS_NTFS_SUPPORT
static int check_ntfs_device(int index);
static void add_ntfs_device(int index);
#endif

int romi_devices_init(void)
{
    if (g_initialized) {
        return 0;
    }

    LOG("initializing device system");

    g_device_count = 0;
    g_selected_device = 0;
    g_initialized = 1;

    romi_devices_scan();

    LOG("device system initialized, found %d devices", g_device_count);
    return 0;
}

void romi_devices_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    LOG("shutting down device system");
    g_initialized = 0;
    g_device_count = 0;
    g_selected_device = 0;
}

int romi_devices_scan(void)
{
    if (!g_initialized) {
        LOG("device system not initialized, cannot scan");
        return 0;
    }

    LOG("scanning for available devices");
    g_device_count = 0;

    add_hdd_device();

    for (int i = 0; i < 8; i++) {
        if (check_usb_device(i)) {
            add_usb_device(i);
        }
    }

#ifdef HAS_NTFS_SUPPORT
    for (int i = 0; i < 8; i++) {
        if (check_ntfs_device(i)) {
            add_ntfs_device(i);
        }
    }
#endif

    LOG("scan complete, found %d devices", g_device_count);
    return g_device_count;
}

int romi_devices_count(void)
{
    return g_device_count;
}

const RomiDevice* romi_devices_get(int index)
{
    if (index < 0 || index >= g_device_count) {
        return NULL;
    }
    return &g_devices[index];
}

const RomiDevice* romi_devices_get_selected(void)
{
    if (g_selected_device < 0 || g_selected_device >= g_device_count) {
        return NULL;
    }
    return &g_devices[g_selected_device];
}

int romi_devices_get_selected_index(void)
{
    return g_selected_device;
}

void romi_devices_set_selected(int index)
{
    if (index < 0 || index >= g_device_count) {
        LOG("invalid device index %d, ignoring", index);
        return;
    }

    LOG("setting selected device to %d (%s)", index, g_devices[index].label);
    g_selected_device = index;

    romi_devices_create_folders();
}

static void romi_devices_create_folders(void)
{
    const RomiDevice* dev = romi_devices_get_selected();
    if (!dev || !dev->available) {
        LOG("selected device not available, skipping folder creation");
        return;
    }

    const char* base = dev->path;
    char path[512];

    const char* folders[] = {
        "PSXISO",
        "PS2ISO",
        "PS3ISO",
        "ROMS",
        "ROMS/NES",
        "ROMS/SNES",
        "ROMS/GB",
        "ROMS/GBC",
        "ROMS/GBA",
        "ROMS/Genesis",
        "ROMS/SMS",
        "ROMS/ATARI2600",
        "ROMS/ATARI5200",
        "ROMS/ATARI7800",
        "ROMS/LYNX",
        "ROMS/MAME"
    };

    for (int i = 0; i < (int)(sizeof(folders) / sizeof(folders[0])); i++) {
        snprintf(path, sizeof(path), "%s%s", base, folders[i]);
        LOG("creating folder: %s", path);
        romi_mkdirs(path);
    }
}

void romi_devices_set_selected_by_path(const char* path)
{
    if (!path) {
        return;
    }

    for (int i = 0; i < g_device_count; i++) {
        if (strcmp(g_devices[i].path, path) == 0) {
            romi_devices_set_selected(i);
            return;
        }
    }

    LOG("device with path %s not found, keeping current selection", path);
}

int romi_devices_check_available(int index)
{
    if (index < 0 || index >= g_device_count) {
        return 0;
    }

    RomiDevice* dev = &g_devices[index];

    if (dev->type == DeviceTypeHDD0) {
        return 1;
    }

    s32 fd;
    int result = sysLv2FsOpenDir(dev->path, &fd);
    if (result == 0) {
        sysLv2FsCloseDir(fd);
        return 1;
    }

    return 0;
}

const char* romi_devices_get_base_path(void)
{
    const RomiDevice* dev = romi_devices_get_selected();
    if (!dev) {
        return "/dev_hdd0/";
    }
    return dev->path;
}

static int check_usb_device(int index)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev_usb%03d", index);

    s32 fd;
    int result = sysLv2FsOpenDir(path, &fd);
    if (result == 0) {
        sysLv2FsCloseDir(fd);
        LOG("found USB device at %s", path);
        return 1;
    }

    return 0;
}

static void add_hdd_device(void)
{
    if (g_device_count >= ROMI_MAX_DEVICES) {
        return;
    }

    RomiDevice* dev = &g_devices[g_device_count];
    dev->type = DeviceTypeHDD0;
    romi_strncpy(dev->path, sizeof(dev->path), "/dev_hdd0/");
    romi_strncpy(dev->label, sizeof(dev->label), "Internal HDD");
    dev->available = 1;
    dev->device_index = 0;

    get_device_space(dev->path, &dev->free_space, &dev->total_space);

    LOG("added device: %s (%s)", dev->label, dev->path);
    g_device_count++;
}

static void add_usb_device(int index)
{
    if (g_device_count >= ROMI_MAX_DEVICES) {
        return;
    }

    RomiDevice* dev = &g_devices[g_device_count];
    dev->type = DeviceTypeUSB;
    dev->device_index = index;

    snprintf(dev->path, sizeof(dev->path), "/dev_usb%03d/", index);
    snprintf(dev->label, sizeof(dev->label), "USB Drive %d", index);

    dev->available = 1;
    get_device_space(dev->path, &dev->free_space, &dev->total_space);

    LOG("added device: %s (%s)", dev->label, dev->path);
    g_device_count++;
}

static void get_device_space(const char* path, uint64_t* free, uint64_t* total)
{
    sysFSStat stat;

    *free = 0;
    *total = 0;

    if (sysLv2FsStat(path, &stat) != 0) {
        LOG("failed to get space info for %s", path);
        return;
    }

    *total = stat.st_size;
    *free = stat.st_blksize * 1024;

    LOG("device %s: %.2f GB free / %.2f GB total",
        path,
        (double)*free / (1024.0 * 1024.0 * 1024.0),
        (double)*total / (1024.0 * 1024.0 * 1024.0));
}

#ifdef HAS_NTFS_SUPPORT
static int check_ntfs_device(int index)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev_NTFS%d", index);

    s32 fd;
    int result = sysLv2FsOpenDir(path, &fd);
    if (result == 0) {
        sysLv2FsCloseDir(fd);
        LOG("found NTFS device at %s", path);
        return 1;
    }

    return 0;
}

static void add_ntfs_device(int index)
{
    if (g_device_count >= ROMI_MAX_DEVICES) {
        return;
    }

    RomiDevice* dev = &g_devices[g_device_count];
    dev->type = DeviceTypeNTFS;
    dev->device_index = index;

    snprintf(dev->path, sizeof(dev->path), "/dev_NTFS%d/", index);
    snprintf(dev->label, sizeof(dev->label), "NTFS Drive %d", index);

    dev->available = 1;
    get_device_space(dev->path, &dev->free_space, &dev->total_space);

    LOG("added device: %s (%s)", dev->label, dev->path);
    g_device_count++;
}
#endif
