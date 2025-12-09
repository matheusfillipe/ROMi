#include <stdio.h>
#include <curl/curl.h>

extern void dbglogger_log(const char* fmt, ...);

int main(void)
{
    CURL *curl;
    CURLcode res;

    dbglogger_log("TEST: Starting minimal HTTP test");

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if(curl) {
        // Try plain HTTP first (no TLS)
        curl_easy_setopt(curl, CURLOPT_URL, "http://example.com");
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        dbglogger_log("TEST: Performing HTTP request");
        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            dbglogger_log("TEST: curl_easy_perform() failed: %s (code %d)",
                         curl_easy_strerror(res), res);
        } else {
            dbglogger_log("TEST: SUCCESS!");
        }

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return 0;
}
