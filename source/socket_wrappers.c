#include <net/socket.h>
#include <net/select.h>
#include <net/poll.h>
#include <net/netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/net.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

// ioctl constants (not in PSL1GHT headers)
#define FIONBIO  0x8004667E
#define FIONREAD 0x4004667F

extern int __netSocket(int, int, int);
extern int __netBind(int, const struct sockaddr*, socklen_t);
extern int __netConnect(int, const struct sockaddr*, socklen_t);
extern int __netListen(int, int);
extern int __netAccept(int, struct sockaddr*, socklen_t*);
extern int __netSelect(int, fd_set*, fd_set*, fd_set*, struct timeval*);
extern int __netPoll(struct pollfd[], unsigned int, int);
extern ssize_t __netRecv(int, void*, size_t, int);
extern ssize_t __netRecvFrom(int, void*, size_t, int, struct sockaddr*, socklen_t*);
extern ssize_t __netSend(int, const void*, size_t, int);
extern ssize_t __netSendTo(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
extern int __netClose(int);
extern int __netShutdown(int, int);
extern int __netGetSockName(int, struct sockaddr*, socklen_t*);
extern int __netGetPeerName(int, struct sockaddr*, socklen_t*);
extern int __netSetSockOpt(int, int, int, const void*, socklen_t);
extern int __netGetSockOpt(int, int, int, void*, socklen_t*);
extern struct hostent* __netGetHostByName(const char*);
extern struct hostent* __netGetHostByAddr(const char*, socklen_t, int);

// Track which FDs are network sockets (max 64 concurrent network sockets)
#define MAX_NET_FDS 64
static unsigned char net_fd_table[MAX_NET_FDS];
static int net_fd_initialized = 0;

static void init_net_fd_table(void) {
    if (!net_fd_initialized) {
        for (int i = 0; i < MAX_NET_FDS; i++)
            net_fd_table[i] = 0;
        net_fd_initialized = 1;
    }
}

static void mark_net_fd(int fd) {
    init_net_fd_table();
    if (fd >= 0 && fd < MAX_NET_FDS)
        net_fd_table[fd] = 1;
}

static void unmark_net_fd(int fd) {
    if (fd >= 0 && fd < MAX_NET_FDS)
        net_fd_table[fd] = 0;
}

static int is_net_fd(int fd) {
    init_net_fd_table();
    return (fd >= 0 && fd < MAX_NET_FDS) ? net_fd_table[fd] : 0;
}

// Keep old macros for backward compatibility during transition
#define SOCKET_FD_MASK 0x40000000
#define FD(socket) ((socket) & ~SOCKET_FD_MASK)

static int net_errno_map(int net_ret)
{
    if (net_ret >= 0)
        return net_ret;

    switch (net_ret) {
        case 0x80410101: errno = EACCES; break;
        case 0x80410102: errno = EINVAL; break;
        case 0x80410103: errno = EMFILE; break;
        case 0x80410104: errno = ENOBUFS; break;
        case 0x80410105: errno = EBADF; break;
        case 0x80410106: errno = EISCONN; break;
        case 0x80410107: errno = ENOTCONN; break;
        case 0x80410108: errno = ECONNREFUSED; break;
        case 0x80410109: errno = ETIMEDOUT; break;
        case 0x8041010A: errno = ECONNRESET; break;
        case 0x8041010B: errno = EADDRINUSE; break;
        case 0x8041010C: errno = ENETUNREACH; break;
        case 0x8041010D: errno = EHOSTUNREACH; break;
        case 0x8041010E: errno = EINPROGRESS; break;
        case 0x8041010F: errno = EALREADY; break;
        case 0x80410110: errno = EDESTADDRREQ; break;
        case 0x80410111: errno = EPROTOTYPE; break;
        case 0x80410112: errno = ENOPROTOOPT; break;
        case 0x80410113: errno = EPROTONOSUPPORT; break;
        case 0x80410114: errno = EOPNOTSUPP; break;
        case 0x80410115: errno = EPFNOSUPPORT; break;
        case 0x80410116: errno = EAFNOSUPPORT; break;
        case 0x80410117: errno = EADDRNOTAVAIL; break;
        case 0x80410118: errno = ENETDOWN; break;
        case 0x80410119: errno = EMSGSIZE; break;
        // PSL1GHT low-level errors (0x80010xxx range)
        case 0x80010224: errno = EINPROGRESS; break;  // Non-blocking connect in progress
        case 0x80010223: errno = EWOULDBLOCK; break;  // Would block
        default: errno = EIO; break;
    }
    return -1;
}

int socket(int domain, int type, int protocol)
{
    extern void dbglogger_log(const char* fmt, ...);

    int ret = __netSocket(domain, type, protocol);

    const char* type_str = (type == 1) ? "STREAM" : (type == 2) ? "DGRAM" : "OTHER";
    dbglogger_log("socket(AF_INET, SOCK_%s, %d) = %d", type_str, protocol, ret);

    if (ret < 0)
        return net_errno_map(ret);

    mark_net_fd(ret);
    return ret;  // Return raw FD, no masking
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    extern void dbglogger_log(const char* fmt, ...);

    (void)domain;
    (void)type;
    (void)protocol;

    dbglogger_log("socketpair() CALLED - using TCP loopback emulation");

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    int listener = __netSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        dbglogger_log("socketpair() listener socket FAILED: %d", listener);
        return -1;
    }
    dbglogger_log("socketpair() listener fd=%d", listener);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001);  // 127.0.0.1
    addr.sin_port = 0;  // Let OS pick a port

    int ret = __netBind(listener, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        dbglogger_log("socketpair() bind FAILED: %d", ret);
        __netClose(listener);
        return -1;
    }
    dbglogger_log("socketpair() bind returned: %d", ret);

    ret = __netGetSockName(listener, (struct sockaddr*)&addr, &addrlen);
    if (ret < 0) {
        dbglogger_log("socketpair() getsockname FAILED: %d", ret);
        __netClose(listener);
        return -1;
    }
    unsigned char* ip = (unsigned char*)&addr.sin_addr.s_addr;
    dbglogger_log("socketpair() bound to %d.%d.%d.%d:%d",
        ip[0], ip[1], ip[2], ip[3], ntohs(addr.sin_port));

    ret = __netListen(listener, 1);
    if (ret < 0) {
        dbglogger_log("socketpair() listen FAILED: %d", ret);
        __netClose(listener);
        return -1;
    }
    dbglogger_log("socketpair() listen returned: %d", ret);

    int client = __netSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client < 0) {
        dbglogger_log("socketpair() client socket FAILED: %d", client);
        __netClose(listener);
        return -1;
    }
    dbglogger_log("socketpair() client fd=%d", client);

    ret = __netConnect(client, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        dbglogger_log("socketpair() connect FAILED: %d", ret);
        __netClose(client);
        __netClose(listener);
        return -1;
    }
    dbglogger_log("socketpair() connect returned: %d", ret);

    int server = __netAccept(listener, NULL, NULL);
    if (server < 0) {
        dbglogger_log("socketpair() accept FAILED: %d", server);
        __netClose(client);
        __netClose(listener);
        return -1;
    }
    dbglogger_log("socketpair() accept returned: fd=%d", server);

    __netClose(listener);

    mark_net_fd(client);
    mark_net_fd(server);
    sv[0] = client;
    sv[1] = server;

    dbglogger_log("socketpair() SUCCESS: sv[0]=%d, sv[1]=%d", sv[0], sv[1]);

    return 0;
}

int bind(int sock, const struct sockaddr* address, socklen_t address_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    if (address->sa_family == AF_INET) {
        struct sockaddr_in* addr_in = (struct sockaddr_in*)address;
        unsigned char* ip = (unsigned char*)&addr_in->sin_addr.s_addr;
        int port = ntohs(addr_in->sin_port);
        dbglogger_log("bind() to %d.%d.%d.%d:%d (sock=%d, fd=%d)",
            ip[0], ip[1], ip[2], ip[3], port, sock, sock);
    }

    int ret = __netBind(sock, address, address_len);

    dbglogger_log("bind() returned: %d (0x%08x)", ret, ret);

    return net_errno_map(ret);
}

int connect(int sock, const struct sockaddr* address, socklen_t address_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    if (address->sa_family == AF_INET) {
        struct sockaddr_in* addr_in = (struct sockaddr_in*)address;
        unsigned char* ip = (unsigned char*)&addr_in->sin_addr.s_addr;
        int port = ntohs(addr_in->sin_port);
        dbglogger_log("connect() to %d.%d.%d.%d:%d (sock=%d, fd=%d)",
            ip[0], ip[1], ip[2], ip[3], port, sock, sock);
    }

    int ret = __netConnect(sock, address, address_len);

    dbglogger_log("connect() returned: %d (0x%08x)", ret, ret);

    return net_errno_map(ret);
}

int listen(int sock, int backlog)
{
    extern void dbglogger_log(const char* fmt, ...);

    dbglogger_log("listen(sock=%d, fd=%d, backlog=%d)", sock, sock, backlog);

    int ret = __netListen(sock, backlog);

    dbglogger_log("listen() returned: %d (0x%08x)", ret, ret);

    return net_errno_map(ret);
}

int accept(int sock, struct sockaddr* address, socklen_t* address_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    dbglogger_log("accept(sock=%d, fd=%d) CALLED", sock, sock);

    int ret = __netAccept(sock, address, address_len);

    if (ret < 0) {
        dbglogger_log("accept() FAILED: %d (0x%08x)", ret, ret);
        return net_errno_map(ret);
    }

    mark_net_fd(ret);
    dbglogger_log("accept() returned: fd=%d", ret);

    return ret;
}

ssize_t recv(int sock, void* buffer, size_t length, int flags)
{
    extern void dbglogger_log(const char* fmt, ...);

    ssize_t ret = (ssize_t)__netRecv(sock, buffer, length, flags);

    dbglogger_log("recv(sock=%d, fd=%d, len=%d) = %d (0x%08x)",
        sock, sock, (int)length, (int)ret, (int)ret);

    if (ret > 0 && ret <= 16 && buffer) {
        unsigned char* data = (unsigned char*)buffer;
        dbglogger_log("recv data: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
            data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
    }

    return net_errno_map((int)ret);
}

ssize_t recvfrom(int sock, void* buffer, size_t length, int flags,
                 struct sockaddr* from, socklen_t* fromlen)
{
    return (ssize_t)net_errno_map(__netRecvFrom(sock, buffer, length, flags, from, fromlen));
}

ssize_t send(int sock, const void* message, size_t length, int flags)
{
    extern void dbglogger_log(const char* fmt, ...);

    if (length <= 16 && message) {
        const unsigned char* data = (const unsigned char*)message;
        dbglogger_log("send data: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
            data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
    }

    ssize_t ret = (ssize_t)__netSend(sock, message, length, flags);

    dbglogger_log("send(sock=%d, fd=%d, len=%d) = %d (0x%08x)",
        sock, sock, (int)length, (int)ret, (int)ret);

    return net_errno_map((int)ret);
}

ssize_t sendto(int sock, const void* message, size_t length, int flags,
               const struct sockaddr* dest_addr, socklen_t dest_len)
{
    return (ssize_t)net_errno_map(__netSendTo(sock, message, length, flags, dest_addr, dest_len));
}

int shutdown(int sock, int how)
{
    return net_errno_map(__netShutdown(sock, how));
}

int getsockname(int sock, struct sockaddr* address, socklen_t* address_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    dbglogger_log("getsockname(sock=%d, fd=%d) CALLED", sock, sock);

    int ret = __netGetSockName(sock, address, address_len);

    if (ret < 0) {
        dbglogger_log("getsockname() FAILED: %d (0x%08x)", ret, ret);
    } else if (address && address->sa_family == AF_INET) {
        struct sockaddr_in* addr_in = (struct sockaddr_in*)address;
        unsigned char* ip = (unsigned char*)&addr_in->sin_addr.s_addr;
        int port = ntohs(addr_in->sin_port);
        dbglogger_log("getsockname() SUCCESS: %d.%d.%d.%d:%d",
            ip[0], ip[1], ip[2], ip[3], port);
    }

    return net_errno_map(ret);
}

int getpeername(int sock, struct sockaddr* address, socklen_t* address_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    dbglogger_log("getpeername(sock=%d, fd=%d) CALLED", sock, sock);

    int ret = __netGetPeerName(sock, address, address_len);

    dbglogger_log("getpeername() returned: %d (0x%08x)", ret, ret);

    if (ret < 0) {
        dbglogger_log("getpeername() FAILED - curl may interpret this as connection failure!");
    } else if (address && address->sa_family == AF_INET) {
        struct sockaddr_in* addr_in = (struct sockaddr_in*)address;
        unsigned char* ip = (unsigned char*)&addr_in->sin_addr.s_addr;
        int port = ntohs(addr_in->sin_port);
        dbglogger_log("getpeername() SUCCESS: %d.%d.%d.%d:%d",
            ip[0], ip[1], ip[2], ip[3], port);
    }

    return net_errno_map(ret);
}

int setsockopt(int sock, int level, int option_name, const void* option_value, socklen_t option_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    const char* level_str = (level == 0xFFFF) ? "SOL_SOCKET" :
                           (level == 6) ? "IPPROTO_TCP" : "OTHER";

    if (level == 0xFFFF && option_name == 0x1100) {
        int val = option_value ? *(int*)option_value : -1;
        dbglogger_log("setsockopt(sock=%d, %s, SO_NBIO, %d)", sock, level_str, val);
    } else if (level == 6 && option_name == 1) { // TCP_NODELAY
        int val = option_value ? *(int*)option_value : -1;
        dbglogger_log("setsockopt(sock=%d, IPPROTO_TCP, TCP_NODELAY, %d)", sock, val);
    } else {
        dbglogger_log("setsockopt(sock=%d, level=0x%x, opt=0x%x, len=%d)",
            sock, level, option_name, (int)option_len);
    }

    int ret = __netSetSockOpt(sock, level, option_name, option_value, option_len);

    if (ret < 0) {
        dbglogger_log("setsockopt() FAILED: %d (0x%08x)", ret, ret);
    }

    return net_errno_map(ret);
}

int getsockopt(int sock, int level, int option_name, void* option_value, socklen_t* option_len)
{
    extern void dbglogger_log(const char* fmt, ...);

    int ret = __netGetSockOpt(sock, level, option_name, option_value, option_len);

    if (level == 0xFFFF && option_name == 0x1007) { // SOL_SOCKET, SO_ERROR
        int err = option_value ? *(int*)option_value : -1;
        dbglogger_log("getsockopt(SO_ERROR) = %d, error_value=%d (0x%08x)", ret, err, err);
    }

    return net_errno_map(ret);
}

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout)
{
    extern void dbglogger_log(const char* fmt, ...);

    // PSL1GHT's __netSelect() is buggy - it corrupts fd_sets
    // Implement select() using poll() instead

    struct pollfd fds[64];
    int npollfds = 0;

    // Convert fd_sets to pollfd array
    for (int i = 0; i < nfds && i < 64; i++) {
        short events = 0;

        if (readfds && FD_ISSET(i, readfds))
            events |= POLLIN;
        if (writefds && FD_ISSET(i, writefds))
            events |= POLLOUT;
        if (exceptfds && FD_ISSET(i, exceptfds))
            events |= POLLERR;

        if (events) {
            fds[npollfds].fd = i;
            fds[npollfds].events = events;
            fds[npollfds].revents = 0;
            npollfds++;
        }
    }

    int timeout_ms = timeout ? (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;

    dbglogger_log("select(nfds=%d) -> poll(npollfds=%d, timeout=%dms)", nfds, npollfds, timeout_ms);
    for (int i = 0; i < npollfds && i < 10; i++) {
        dbglogger_log("  poll[%d]: fd=%d, events=0x%x %s%s%s", i, fds[i].fd, fds[i].events,
            (fds[i].events & POLLIN) ? "READ " : "",
            (fds[i].events & POLLOUT) ? "WRITE " : "",
            (fds[i].events & POLLERR) ? "ERR" : "");
    }

    int ret = __netPoll(fds, (unsigned int)npollfds, timeout_ms);

    if (ret < 0) {
        dbglogger_log("select/poll() returned error: %d", ret);
        return net_errno_map(ret);
    }

    if (ret > 0) {
        dbglogger_log("poll() returned %d, checking revents:", ret);
        for (int i = 0; i < npollfds && i < 10; i++) {
            if (fds[i].revents) {
                dbglogger_log("  poll[%d]: fd=%d, revents=0x%x %s%s%s%s%s", i, fds[i].fd, fds[i].revents,
                    (fds[i].revents & POLLIN) ? "READ " : "",
                    (fds[i].revents & POLLOUT) ? "WRITE " : "",
                    (fds[i].revents & POLLERR) ? "ERR " : "",
                    (fds[i].revents & POLLHUP) ? "HUP " : "",
                    (fds[i].revents & POLLNVAL) ? "NVAL" : "");
            }
        }
    }

    // Clear all fd_sets
    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);

    // Convert poll results back to fd_sets
    int ready_count = 0;
    for (int i = 0; i < npollfds; i++) {
        if (fds[i].revents) {
            if ((fds[i].revents & POLLIN) && readfds) {
                FD_SET(fds[i].fd, readfds);
                dbglogger_log("select: FD %d ready for reading", fds[i].fd);
            }
            if ((fds[i].revents & POLLOUT) && writefds) {
                FD_SET(fds[i].fd, writefds);
                dbglogger_log("select: FD %d ready for writing", fds[i].fd);
            }
            if ((fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) && exceptfds) {
                FD_SET(fds[i].fd, exceptfds);
                dbglogger_log("select: FD %d has error/exception", fds[i].fd);
            }
            ready_count++;
        }
    }

    dbglogger_log("select() returning %d ready FDs", ready_count);
    return ready_count;
}

int poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    extern void dbglogger_log(const char* fmt, ...);

    dbglogger_log("poll(nfds=%d, timeout=%d)", (int)nfds, timeout);

    for (unsigned int i = 0; i < nfds; i++) {
        dbglogger_log("  poll[%d]: fd=%d, events=0x%x", i, fds[i].fd, fds[i].events);
    }

    int ret = __netPoll(fds, (unsigned int)nfds, timeout);

    dbglogger_log("poll() returned: %d", ret);
    if (ret > 0) {
        for (unsigned int i = 0; i < nfds; i++) {
            if (fds[i].revents) {
                dbglogger_log("  poll[%d]: revents=0x%x", i, fds[i].revents);
            }
        }
    }

    return net_errno_map(ret);
}

int socketclose(int sock)
{
    extern void dbglogger_log(const char* fmt, ...);

    dbglogger_log("socketclose(sock=%d, fd=%d)", sock, sock);

    int ret = __netClose(sock);

    if (ret < 0) {
        dbglogger_log("socketclose() FAILED: %d (0x%08x)", ret, ret);
    }

    return net_errno_map(ret);
}

extern struct hostent* simple_dns_resolve(const char* hostname);

struct hostent* gethostbyname(const char* name)
{
    extern void dbglogger_log(const char* fmt, ...);
    dbglogger_log("gethostbyname called for: %s", name);

    // PSL1GHT DNS is broken, use our UDP-based resolver
    return simple_dns_resolve(name);
}

struct hostent* gethostbyaddr(const char* addr, socklen_t len, int type)
{
    return __netGetHostByAddr(addr, len, type);
}

int fcntl(int fd, int cmd, ...)
{
    extern void dbglogger_log(const char* fmt, ...);

    va_list args;
    va_start(args, cmd);

    const char* cmd_str = (cmd == F_GETFL) ? "F_GETFL" :
                         (cmd == F_SETFL) ? "F_SETFL" :
                         (cmd == F_GETFD) ? "F_GETFD" :
                         (cmd == F_SETFD) ? "F_SETFD" : "OTHER";

    dbglogger_log("fcntl(fd=%d, cmd=%s/%d)", fd, cmd_str, cmd);

    if (cmd == F_SETFL) {
        int flags = va_arg(args, int);
        dbglogger_log("fcntl(F_SETFL): flags=0x%x (O_NONBLOCK=%s)",
            flags, (flags & O_NONBLOCK) ? "YES" : "NO");

        // Set or clear non-blocking mode
        int val = (flags & O_NONBLOCK) ? 1 : 0;
        int ret = __netSetSockOpt(fd, 0xFFFF, 0x1100, &val, sizeof(val));
        dbglogger_log("fcntl: set SO_NBIO=%d via setsockopt = %d", val, ret);
        va_end(args);
        return net_errno_map(ret);
    } else if (cmd == F_GETFL) {
        int val = 0;
        socklen_t len = sizeof(val);
        int ret = __netGetSockOpt(fd, 0xFFFF, 0x1100, &val, &len);
        dbglogger_log("fcntl(F_GETFL): SO_NBIO=%d, ret=%d", val, ret);
        va_end(args);
        if (ret < 0) return net_errno_map(ret);
        return val ? O_NONBLOCK : 0;
    }

    va_end(args);
    dbglogger_log("fcntl: UNSUPPORTED cmd=%d", cmd);
    errno = EINVAL;
    return -1;
}

int ioctl(int fd, unsigned long request, ...)
{
    extern void dbglogger_log(const char* fmt, ...);

    va_list args;
    va_start(args, request);

    const char* req_str = (request == FIONBIO) ? "FIONBIO" :
                         (request == FIONREAD) ? "FIONREAD" : "OTHER";

    dbglogger_log("ioctl(fd=%d, request=%s/0x%lx)", fd, req_str, request);

    if (request == FIONBIO) {
        int* argp = va_arg(args, int*);
        int val = argp ? *argp : 0;
        dbglogger_log("ioctl(FIONBIO): setting non-blocking=%d", val);

        int ret = __netSetSockOpt(fd, 0xFFFF, 0x1100, &val, sizeof(val));
        dbglogger_log("ioctl: set SO_NBIO via setsockopt = %d", ret);
        va_end(args);
        return net_errno_map(ret);
    } else if (request == FIONREAD) {
        int* argp = va_arg(args, int*);
        socklen_t len = sizeof(int);
        int ret = __netGetSockOpt(fd, SOL_SOCKET, SO_RCVBUF, argp, &len);
        dbglogger_log("ioctl(FIONREAD): ret=%d, bytes=%d", ret, argp ? *argp : -1);
        va_end(args);
        return net_errno_map(ret);
    }

    va_end(args);
    dbglogger_log("ioctl: UNSUPPORTED request=0x%lx", request);
    errno = EINVAL;
    return -1;
}

ssize_t __wrap_read(int fd, void* buf, size_t count)
{
    extern void dbglogger_log(const char* fmt, ...);
    extern ssize_t __real_read(int, void*, size_t);

    if (is_net_fd(fd)) {
        dbglogger_log("read(sock=%d, len=%d) [routing to recv]", fd, (int)count);
        ssize_t ret = (ssize_t)__netRecv(fd, buf, count, 0);
        dbglogger_log("read() via recv = %d (0x%08x)", (int)ret, (int)ret);
        return net_errno_map((int)ret);
    }

    return __real_read(fd, buf, count);
}

ssize_t __wrap_write(int fd, const void* buf, size_t count)
{
    extern void dbglogger_log(const char* fmt, ...);
    extern ssize_t __real_write(int, const void*, size_t);

    if (is_net_fd(fd)) {
        dbglogger_log("write(sock=%d, len=%d) [routing to send]", fd, (int)count);
        ssize_t ret = (ssize_t)__netSend(fd, buf, count, 0);
        dbglogger_log("write() via send = %d (0x%08x)", (int)ret, (int)ret);
        return net_errno_map((int)ret);
    }

    return __real_write(fd, buf, count);
}

int __wrap_close(int fd)
{
    extern void dbglogger_log(const char* fmt, ...);
    extern int __real_close(int);

    if (is_net_fd(fd)) {
        dbglogger_log("close(sock=%d) [routing to socketclose]", fd);

        int ret = __netClose(fd);

        if (ret < 0) {
            dbglogger_log("close() via __netClose FAILED: %d (0x%08x)", ret, ret);
        } else {
            dbglogger_log("close() via __netClose SUCCESS");
            unmark_net_fd(fd);
        }

        return net_errno_map(ret);
    }

    return __real_close(fd);
}
