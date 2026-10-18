#include "net/http.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>

#ifdef WC_HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace wc {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

bool timed_out() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }

// Returns a connected fd with send/recv timeouts, or -1 and sets err.
int connect_tcp(const Url& u, int timeout_ms, std::string& err) {
    std::string host = u.host;
    if (!host.empty() && host[0] == '[') host = host.substr(1, host.size() - 2);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (int rc = getaddrinfo(host.c_str(), std::to_string(u.port).c_str(), &hints, &res)) {
        err = std::string("dns: ") + gai_strerror(rc);
        return -1;
    }
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    int fd = -1;
    err = "connect: no address";
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) continue;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);  // Linux: also bounds connect()
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        err = timed_out() ? "timeout: connect" : std::string("connect: ") + std::strerror(errno);
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) err.clear();
    return fd;
}

// Headers + full body already in `raw`? Lets us stop without waiting for the
// server to close (some ignore "Connection: close").
bool complete(const std::string& raw) {
    size_t he = raw.find("\r\n\r\n");
    if (he == std::string::npos) return false;
    std::string head = lower(raw.substr(0, he));
    if (head.find("transfer-encoding:") != std::string::npos && head.find("chunked") != std::string::npos)
        return raw.size() >= 5 && raw.compare(raw.size() - 5, 5, "0\r\n\r\n") == 0;
    size_t cl = head.find("\r\ncontent-length:");
    if (cl == std::string::npos) return false;
    return raw.size() - (he + 4) >= std::strtoull(head.c_str() + cl + 17, nullptr, 10);
}

bool decode_chunked(const std::string& in, std::string& out) {
    size_t i = 0;
    while (true) {
        size_t eol = in.find("\r\n", i);
        if (eol == std::string::npos) return false;
        std::string len = in.substr(i, eol - i);
        len = len.substr(0, len.find(';'));  // chunk extensions
        len = trim(len);
        if (len.empty() || len.size() > 15 || len.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            return false;
        size_t n = std::stoull(len, nullptr, 16);
        i = eol + 2;
        if (n == 0) return true;  // trailers ignored
        if (i + n > in.size()) return false;
        out.append(in, i, n);
        i += n + 2;  // data + CRLF
    }
}

}  // namespace

HttpResponse parse_http_response(const std::string& raw) {
    HttpResponse r;
    size_t he = raw.find("\r\n\r\n");
    if (raw.rfind("HTTP/", 0) != 0 || he == std::string::npos) {
        r.error = "bad_response: no status line / headers";
        return r;
    }
    size_t eol = raw.find("\r\n");
    size_t sp = raw.find(' ');
    if (sp == std::string::npos || sp > eol || sp + 4 > raw.size() ||
        raw.substr(sp + 1, 3).find_first_not_of("0123456789") != std::string::npos) {
        r.error = "bad_response: status line";
        return r;
    }
    r.status = std::stoi(raw.substr(sp + 1, 3));
    for (size_t i = eol + 2; i < he;) {
        size_t e = raw.find("\r\n", i);
        std::string line = raw.substr(i, e - i);
        if (size_t c = line.find(':'); c != std::string::npos)
            r.headers[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
        i = e + 2;
    }
    std::string body = raw.substr(he + 4);
    auto te = r.headers.find("transfer-encoding");
    auto cl = r.headers.find("content-length");
    if (te != r.headers.end() && lower(te->second).find("chunked") != std::string::npos) {
        if (!decode_chunked(body, r.body)) r.error = "bad_response: chunked body";
    } else if (cl != r.headers.end() && !cl->second.empty() &&
               cl->second.find_first_not_of("0123456789") == std::string::npos) {
        r.body = body.substr(0, std::strtoull(cl->second.c_str(), nullptr, 10));
    } else {
        r.body = std::move(body);
    }
    return r;
}

#ifdef WC_HAVE_OPENSSL
struct HttpClient::Tls {
    SSL_CTX* ctx = nullptr;
    ~Tls() { SSL_CTX_free(ctx); }
};
#else
struct HttpClient::Tls {};
#endif

HttpClient::HttpClient(HttpOptions opt) : opt_(std::move(opt)) {
    std::signal(SIGPIPE, SIG_IGN);  // a server hanging up mid-write must not kill us
}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::get(const Url& url) { return request(url, "GET", "", ""); }

HttpResponse HttpClient::post(const Url& url, const std::string& body, const std::string& content_type,
                              const std::map<std::string, std::string>& headers) {
    std::string extra = "\r\nContent-Type: " + content_type + "\r\nContent-Length: " + std::to_string(body.size());
    for (const auto& [k, v] : headers) extra += "\r\n" + k + ": " + v;
    return request(url, "POST", extra, body);
}

HttpResponse HttpClient::request(const Url& url, const char* method, const std::string& extra, const std::string& body) {
    HttpResponse r;
    bool https = url.scheme == "https";
#ifndef WC_HAVE_OPENSSL
    if (https) {
        r.error = "tls: built without OpenSSL";
        return r;
    }
#endif
    int fd = connect_tcp(url, opt_.timeout_ms, r.error);
    if (fd < 0) return r;

    std::string req = std::string(method) + " " + url.target() + " HTTP/1.1\r\nHost: " + url.host_port() +
                      "\r\nUser-Agent: " + opt_.user_agent +
                      "\r\nAccept: text/html,application/xhtml+xml;q=0.9,*/*;q=0.5"
                      "\r\nAccept-Encoding: identity\r\nConnection: close" + extra + "\r\n\r\n" + body;

    // One read/write pair for both transports. Returns bytes, 0 = EOF, -1 = error (sets r.error).
    std::function<long(char*, size_t)> rd;
    std::function<bool(const std::string&)> wr;
#ifdef WC_HAVE_OPENSSL
    SSL* ssl = nullptr;
    if (https) {
        std::call_once(tls_once_, [&] {  // get() runs on many crawl threads
            tls_ = std::make_unique<Tls>();
            tls_->ctx = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_min_proto_version(tls_->ctx, TLS1_2_VERSION);
            SSL_CTX_set_verify(tls_->ctx, SSL_VERIFY_PEER, nullptr);
            SSL_CTX_set_default_verify_paths(tls_->ctx);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
            SSL_CTX_set_options(tls_->ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);  // servers that just close
#endif
        });
        ssl = SSL_new(tls_->ctx);
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, url.host.c_str());  // SNI
        SSL_set1_host(ssl, url.host.c_str());             // cert must name this host
        if (SSL_connect(ssl) != 1) {
            long v = SSL_get_verify_result(ssl);
            if (v != X509_V_OK) r.error = std::string("tls: ") + X509_verify_cert_error_string(v);
            else if (timed_out()) r.error = "timeout: tls handshake";
            else {
                char err[256];  // own buffer: ERR_error_string(.., nullptr) shares a static one
                ERR_error_string_n(ERR_get_error(), err, sizeof err);
                r.error = std::string("tls: ") + err;
            }
            ERR_clear_error();
            SSL_free(ssl);
            close(fd);
            return r;
        }
        rd = [&](char* buf, size_t n) -> long {
            int got = SSL_read(ssl, buf, static_cast<int>(n));
            if (got > 0) return got;
            int e = SSL_get_error(ssl, got);
            if (e == SSL_ERROR_ZERO_RETURN || (e == SSL_ERROR_SYSCALL && errno == 0)) return 0;
            r.error = timed_out() ? "timeout: read" : "io: tls read";
            return -1;
        };
        wr = [&](const std::string& s) { return SSL_write(ssl, s.data(), static_cast<int>(s.size())) > 0; };
    }
#endif
    if (!https) {
        rd = [&](char* buf, size_t n) -> long {
            long got = recv(fd, buf, n, 0);
            if (got < 0) r.error = timed_out() ? "timeout: read" : std::string("io: ") + std::strerror(errno);
            return got;
        };
        wr = [&](const std::string& s) {
            for (size_t off = 0; off < s.size();) {
                long n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
                if (n <= 0) return false;
                off += n;
            }
            return true;
        };
    }

    std::string raw;
    if (!wr(req)) {
        r.error = timed_out() ? "timeout: write" : "io: write failed";
    } else {
        char buf[16384];
        long n;
        while ((n = rd(buf, sizeof buf)) > 0) {
            raw.append(buf, n);
            if (raw.size() > opt_.max_bytes) {
                r.error = "too_large: over " + std::to_string(opt_.max_bytes) + " bytes";
                break;
            }
            if (complete(raw)) break;
        }
    }
#ifdef WC_HAVE_OPENSSL
    SSL_free(ssl);
#endif
    close(fd);
    if (!r.error.empty()) return r;
    return parse_http_response(raw);
}

}  // namespace wc
