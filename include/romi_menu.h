#pragma once

#include "romi_db.h"
#include "romi_dialog.h"


typedef enum {
    MenuResultSearch,
    MenuResultDownloads,
    MenuResultSearchClear,
    MenuResultAccept,
    MenuResultCancel,
    MenuResultRefresh,
} MenuResult;

int romi_menu_is_open(void);
void romi_menu_get(Config* config);
MenuResult romi_menu_result(void);

void romi_menu_start(int search_clear, const Config* config);

int romi_do_menu(romi_input* input);
