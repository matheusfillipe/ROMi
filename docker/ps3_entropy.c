#include "common.h"

#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)

#include <stdlib.h>
#include <time.h>

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    static unsigned int seed_initialized = 0;
    size_t i;

    (void)data;

    if (!seed_initialized) {
        srand((unsigned int)time(NULL));
        seed_initialized = 1;
    }

    for (i = 0; i < len; i++) {
        output[i] = (unsigned char)(rand() & 0xFF);
    }

    *olen = len;
    return 0;
}

#endif
