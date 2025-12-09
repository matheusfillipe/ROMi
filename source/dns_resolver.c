#include <string.h>
#include <arpa/inet.h>
#include <net/socket.h>
#include <net/netdb.h>

extern void dbglogger_log(const char* fmt, ...);
extern int socket(int, int, int);
extern int connect(int, const struct sockaddr*, socklen_t);
extern ssize_t send(int, const void*, size_t, int);
extern ssize_t recv(int, void*, size_t, int);
extern int socketclose(int);

static struct hostent g_dns_result;
static char* g_dns_addr_list[2];
static char g_dns_addr_buf[4];
static char g_dns_name_buf[256];

static unsigned short dns_id = 0x1234;

struct dns_header {
    unsigned short id;
    unsigned short flags;
    unsigned short qdcount;
    unsigned short ancount;
    unsigned short nscount;
    unsigned short arcount;
};

static int build_dns_query(unsigned char* buf, const char* hostname)
{
    struct dns_header* hdr = (struct dns_header*)buf;
    hdr->id = htons(dns_id++);
    hdr->flags = htons(0x0100); // Standard query, recursion desired
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;

    unsigned char* qname = buf + sizeof(struct dns_header);
    const char* p = hostname;
    unsigned char* len_ptr = qname++;
    int label_len = 0;

    while (*p) {
        if (*p == '.') {
            *len_ptr = label_len;
            len_ptr = qname++;
            label_len = 0;
        } else {
            *qname++ = *p;
            label_len++;
        }
        p++;
    }
    *len_ptr = label_len;
    *qname++ = 0; // Null terminator

    // QTYPE = A (1), QCLASS = IN (1)
    *qname++ = 0; *qname++ = 1; // Type A
    *qname++ = 0; *qname++ = 1; // Class IN

    return qname - buf;
}

static int parse_dns_response(unsigned char* buf, int len, unsigned char* ipaddr)
{
    struct dns_header* hdr = (struct dns_header*)buf;

    if (ntohs(hdr->ancount) == 0) {
        dbglogger_log("DNS: No answers in response");
        return -1;
    }

    unsigned char* p = buf + sizeof(struct dns_header);

    // Skip question section
    while (*p != 0) {
        if ((*p & 0xC0) == 0xC0) {
            p += 2;
            break;
        }
        p += *p + 1;
    }
    if (*p == 0) p++;
    p += 4; // Skip QTYPE and QCLASS

    // Parse answer section
    for (int i = 0; i < ntohs(hdr->ancount); i++) {
        // Skip name (compression pointer)
        if ((*p & 0xC0) == 0xC0) {
            p += 2;
        } else {
            while (*p != 0) p += *p + 1;
            p++;
        }

        unsigned short type = (p[0] << 8) | p[1];
        unsigned short rdlength = (p[8] << 8) | p[9];
        p += 10;

        if (type == 1 && rdlength == 4) { // A record
            memcpy(ipaddr, p, 4);
            dbglogger_log("DNS: Resolved to %d.%d.%d.%d", ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
            return 0;
        }
        p += rdlength;
    }

    return -1;
}

struct hostent* simple_dns_resolve(const char* hostname)
{
    dbglogger_log("DNS: Resolving %s via UDP to 8.8.8.8:53", hostname);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        dbglogger_log("DNS: socket() failed");
        return NULL;
    }

    struct sockaddr_in dns_server;
    memset(&dns_server, 0, sizeof(dns_server));
    dns_server.sin_family = AF_INET;
    dns_server.sin_port = htons(53);
    dns_server.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr*)&dns_server, sizeof(dns_server)) < 0) {
        dbglogger_log("DNS: connect() failed");
        socketclose(sock);
        return NULL;
    }

    unsigned char query[512];
    int query_len = build_dns_query(query, hostname);

    if (send(sock, query, query_len, 0) < 0) {
        dbglogger_log("DNS: send() failed");
        socketclose(sock);
        return NULL;
    }

    unsigned char response[512];
    int resp_len = recv(sock, response, sizeof(response), 0);
    socketclose(sock);

    if (resp_len < 0) {
        dbglogger_log("DNS: recv() failed");
        return NULL;
    }

    unsigned char ipaddr[4];
    if (parse_dns_response(response, resp_len, ipaddr) < 0) {
        dbglogger_log("DNS: Failed to parse response");
        return NULL;
    }

    // Populate hostent structure
    memcpy(g_dns_addr_buf, ipaddr, 4);
    g_dns_addr_list[0] = g_dns_addr_buf;
    g_dns_addr_list[1] = NULL;

    strncpy(g_dns_name_buf, hostname, sizeof(g_dns_name_buf) - 1);
    g_dns_name_buf[sizeof(g_dns_name_buf) - 1] = 0;

    g_dns_result.h_name = g_dns_name_buf;
    g_dns_result.h_aliases = NULL;
    g_dns_result.h_addrtype = AF_INET;
    g_dns_result.h_length = 4;
    g_dns_result.h_addr_list = g_dns_addr_list;

    dbglogger_log("DNS: SUCCESS");
    return &g_dns_result;
}
