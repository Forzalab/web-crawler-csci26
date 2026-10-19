// gateway.cc - see gateway.h
#include "net/gateway.h"

#include <cctype>
#include <map>

namespace wc {

std::string json_string(const std::string& json, const std::string& key) {
    size_t k = json.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    size_t i = json.find_first_not_of(" \t\r\n", k + key.size() + 2);
    if (i == std::string::npos || json[i] != ':') return "";
    i = json.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos || json[i] != '"') return "";
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) ++i;  // \" \\ \/ -> the char itself
        out += json[i];
    }
    return out;
}

namespace {

// POST body to base + "?" + query; a 200 with "url" = done, else the gateway's "error".
GatewayReply post_to_gateway(HttpClient& http, const std::string& base, const std::string& query, const std::string& body,
                             const std::string& content_type, const std::string& token) {
    GatewayReply out;
    auto u = parse_url(base + (base.find('?') == std::string::npos ? "?" : "&") + query);
    if (!u) {
        out.error = "bad gateway url: " + base;
        return out;
    }
    std::map<std::string, std::string> headers;
    if (!token.empty()) headers["X-Gateway-Token"] = token;
    HttpResponse r = http.post(*u, body, content_type, headers);
    if (!r.error.empty()) {
        out.error = r.error;
        return out;
    }
    if (r.status == 200 && !(out.url = json_string(r.body, "url")).empty()) {
        out.expires_at = json_string(r.body, "expires_at");
        return out;
    }
    out.url.clear();
    out.error = json_string(r.body, "error");
    if (out.error.empty()) out.error = "gateway: HTTP " + std::to_string(r.status);
    return out;
}

}  // namespace

GatewayReply gateway_upload(HttpClient& http, const std::string& gateway, const std::string& assignment, int sub,
                            const std::string& json, const std::string& token) {
    return post_to_gateway(http, gateway, "assignment=" + assignment + "&sub=" + std::to_string(sub), json,
                           "application/json", token);
}

GatewayReply pdf_upload(HttpClient& http, const std::string& endpoint, const std::string& pdf, const std::string& name,
                        const std::string& token) {
    std::string q = "name=";
    for (char c : name) q += std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' ? c : '_';
    return post_to_gateway(http, endpoint, q, pdf, "application/pdf", token);
}

std::string pdf_endpoint(const char* pdf_url, const char* gateway_url) {
    if (pdf_url && *pdf_url) return pdf_url;
    std::string g = gateway_url ? gateway_url : "";
    while (!g.empty() && g.back() == '/') g.pop_back();
    const std::string tail = "/api/bridges";
    if (g.size() < tail.size() || g.compare(g.size() - tail.size(), tail.size(), tail)) return "";
    return g.substr(0, g.size() - tail.size()) + "/api/pdf";
}

}  // namespace wc
