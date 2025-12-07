#include <stdint.h>
#include <sys/types.h>

struct hostent {
  char *h_name;
  char **h_aliases;
  int h_addrtype;
  int h_length;
  char **h_addr_list;
};

struct hostent *gethostbyname(const char *name) { 
  return 0; 
}

unsigned short htons(unsigned short hostshort) { 
  return hostshort; 
}

unsigned short ntohs(unsigned short netshort) { 
  return netshort; 
}

int inet_pton(int af, const char *src, void *dst) { 
  return 0; 
}

int kill(int pid, int sig) {
  return 0;
}

struct timeval;
typedef struct fd_set fd_set;

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  return 0;
}