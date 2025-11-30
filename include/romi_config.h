#pragma once

#include "romi_db.h"

void romi_load_config(Config* config);
void romi_save_config(const Config* config);

const char* romi_get_user_language(void);
