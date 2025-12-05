#pragma once

#include <stdint.h>

typedef enum {
    DeviceTypeHDD0 = 0,
    DeviceTypeUSB,
    DeviceTypeNTFS,
    DeviceTypeCount
} RomiDeviceType;

typedef struct {
    RomiDeviceType type;
    char path[256];
    char label[64];
    uint64_t free_space;
    uint64_t total_space;
    int available;
    int device_index;
} RomiDevice;

int romi_devices_init(void);
void romi_devices_shutdown(void);

int romi_devices_scan(void);
int romi_devices_count(void);

const RomiDevice* romi_devices_get(int index);
const RomiDevice* romi_devices_get_selected(void);
int romi_devices_get_selected_index(void);

void romi_devices_set_selected(int index);
void romi_devices_set_selected_by_path(const char* path);

int romi_devices_check_available(int index);
const char* romi_devices_get_base_path(void);
