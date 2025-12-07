#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <sys/types.h>
#include <stdint.h>

typedef uint32_t in_addr_t;

uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
int inet_pton(int af, const char *src, void *dst);

#endif