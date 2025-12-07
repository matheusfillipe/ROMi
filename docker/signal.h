#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

#define SIGTERM 15

int kill(pid_t pid, int sig);

#endif