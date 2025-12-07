#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <sys/types.h>
#include <stdint.h>

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr {
  in_addr_t s_addr;
};

struct sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  in_port_t sin_port;
  struct in_addr sin_addr;
  char sin_zero[8];
};

struct sockaddr_in6 {
  uint8_t sin6_len;
  uint8_t sin6_family;
  in_port_t sin6_port;
  uint32_t sin6_flowinfo;
  struct in6_addr {
    uint8_t s6_addr[16];
  } sin6_addr;
  uint32_t sin6_scope_id;
};

#define AF_INET 2
#define AF_INET6 10
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

// Forward declarations for select
struct timeval;
typedef struct fd_set fd_set;
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#endif