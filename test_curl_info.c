#include <stdio.h>
#include <curl/curl.h>

extern void dbglogger_log(const char* fmt, ...);

int main(void)
{
    curl_version_info_data *ver;

    dbglogger_log("=== CURL VERSION INFO ===");

    curl_global_init(CURL_GLOBAL_ALL);

    ver = curl_version_info(CURLVERSION_NOW);

    dbglogger_log("curl version: %s", ver->version);
    dbglogger_log("SSL version: %s", ver->ssl_version ? ver->ssl_version : "NONE");
    dbglogger_log("libz version: %s", ver->libz_version ? ver->libz_version : "NONE");
    dbglogger_log("Features: 0x%x", ver->features);

    // Check specific features
    dbglogger_log("SSL support: %s", (ver->features & CURL_VERSION_SSL) ? "YES" : "NO");
    dbglogger_log("HTTP2 support: %s", (ver->features & CURL_VERSION_HTTP2) ? "YES" : "NO");
    dbglogger_log("IPv6 support: %s", (ver->features & CURL_VERSION_IPV6) ? "YES" : "NO");

    // List all protocols
    if (ver->protocols) {
        dbglogger_log("Protocols:");
        for (const char **p = ver->protocols; *p != NULL; p++) {
            dbglogger_log("  - %s", *p);
        }
    }

    dbglogger_log("=== END VERSION INFO ===");

    curl_global_cleanup();
    return 0;
}
