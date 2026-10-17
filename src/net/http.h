// http.h - one HTTP/1.1 GET over a plain socket (http) or OpenSSL (https).
// No redirects here: HttpsFetcher follows them. No proxy support.
// TLS verifies the certificate + hostname against the system CA store
// (OpenSSL honours SSL_CERT_FILE / SSL_CERT_DIR to point elsewhere).
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "net/url.h"

namespace wc {

inline constexpr const char* kUserAgent =
    "CSCI26-WebCrawler/0.1 (student project; +https://github.com/Forzalab/web-crawler-csci26)";

struct HttpResponse {
    int status = 0;                              // 0 = no HTTP response (see error)
    std::map<std::string, std::string> headers;  // keys lowercase; repeated header: last wins
    std::string body;                            // de-chunked
    std::string error;                           // "" = got a response. Else "<kind>: detail",
                                                 // kind = dns | connect | tls | timeout | io | too_large | bad_response
};

struct HttpOptions {
    int timeout_ms = 10000;            // per connect / send / recv call
    size_t max_bytes = 8u << 20;       // whole response; more = "too_large"
    std::string user_agent = kUserAgent;
};

class HttpClient {
public:
    explicit HttpClient(HttpOptions opt = {});
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse get(const Url& url);

private:
    struct Tls;  // OpenSSL context, created once on the first https request (thread safe)
    HttpOptions opt_;
    std::once_flag tls_once_;
    std::unique_ptr<Tls> tls_;
};

// Raw response bytes -> HttpResponse (status line, headers, chunked/length body).
// Exposed for tests.
HttpResponse parse_http_response(const std::string& raw);

}  // namespace wc
